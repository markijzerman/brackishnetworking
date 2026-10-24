/*
 * Harvest float (tile / drawing) - Heltec Vision Master E290.
 *
 * Wakes on a timer, checks the BQ25570 V_OK pin, and if there's enough
 * energy it sends one small 1-bit drawing over LoRa and draws it on its
 * own band of the e-ink. Then sleeps.
 *
 * Open tile_drawer.html in a browser, draw a 64x22 picture, click "copy",
 * and paste the result over the TILE[] block below.
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"
#include "esp_sleep.h"


// --- settings ---

const long POLL_SECONDS      = 30;    // re-check the energy bank every N seconds
const int  TX_REPEATS        = 1;     // copies sent per message
const bool UPDATE_OWN_SCREEN = true;  // draw our band locally too
const int  QUIET_MS          = 200;   // settle time before transmit

// Radio - must match the receiver, or nothing is heard.
const float FREQ  = 868.0;  // MHz (use your region's band)
const float BW    = 125.0;  // kHz
const byte  SF    = 7;      // spreading factor
const byte  CR    = 7;
const byte  SYNC  = 0x12;   // private network
const int   POWER = 14;     // dBm (max 14 in EU)
const float TCXO  = 1.8;
const byte  MAGIC = 0xA5;   // packet marker; change to run two setups in the same room

// Wiring
#define VOK_PIN   17
#define LORA_NSS  8
#define LORA_DIO1 14
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_SCK  9
#define LORA_MISO 11
#define LORA_MOSI 10

// Tile geometry. One portrait band at 2x2 blocks. Don't change unless
// you also change the receiver and the HTML tool to match.
const int TILE_W     = 64;                            // blocks across (block = 2 px -> 128 px)
const int TILE_H     = 22;                            // blocks down   (block = 2 px -> 44 px)
const int TILE_BYTES = (TILE_W * TILE_H + 7) / 8;     // 176 bytes, packed 1 bit per block


// Paste your drawing below (from tile_drawer.html).
const byte TILE[TILE_BYTES] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x3C, 0x3F, 0x80, 0x00, 0x00, 0x40, 0x00,
  0x00, 0x67, 0x20, 0xC0, 0x10, 0x3C, 0x41, 0x00, 0x00, 0x40, 0x00, 0x40,
  0x10, 0x40, 0x41, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x11, 0x40, 0x41, 0x03,
  0x98, 0x0C, 0x0C, 0x00, 0x11, 0x40, 0x41, 0x02, 0x0C, 0x00, 0x00, 0x00,
  0x11, 0x40, 0x41, 0x02, 0x04, 0x00, 0x00, 0x00, 0x11, 0x78, 0x41, 0x02,
  0x02, 0x00, 0x01, 0x00, 0x11, 0x40, 0x41, 0x03, 0x02, 0x00, 0x01, 0x00,
  0x1F, 0x40, 0x41, 0x81, 0x06, 0x10, 0x01, 0x00, 0x11, 0x40, 0x40, 0x81,
  0xCC, 0x10, 0x01, 0x00, 0x11, 0x40, 0x60, 0xC0, 0x78, 0x08, 0x02, 0x00,
  0x11, 0x40, 0x20, 0x78, 0x00, 0x04, 0x04, 0x00, 0x11, 0x60, 0x10, 0x0C,
  0x00, 0x03, 0x98, 0x00, 0x11, 0x20, 0x18, 0x00, 0x00, 0x00, 0xF0, 0x00,
  0x11, 0x3C, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x0F, 0xC0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF8, 0x78, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00, 0x1E,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00
};


// --- below here you shouldn't need to edit ---

// A tiny guide to the number types used in this sketch:
//   byte = 1 byte, holds 0..255
//   int  = whole number (ESP32: 32-bit)
//   long = larger whole number, used for the wake counter

// Wire format: one LoRa packet made of a small header + the drawing bytes.
//   byte 0:    magic (0xA5), lets the receiver spot our packets
//   byte 1..2: nodeId (16-bit), unique per float
//   byte 3..6: seq    (32-bit), counts wakes
//   byte 7:    type (0 = tile)
//   byte 8+:   176 tile bytes
const byte TYPE_TILE   = 0;
const int  HDR_BYTES   = 8;
const int  MAX_PAYLOAD = 200;

SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

EInkDisplay_VisionMasterE290 display;
Preferences prefs;

int  myId;
long seq;


long loadAndIncSeq() {
  prefs.begin("float", false);
  long s = prefs.getULong("seq", 0) + 1;
  prefs.putULong("seq", s);
  prefs.end();
  return s;
}

void sleepAndPoll() {
  esp_sleep_enable_timer_wakeup(POLL_SECONDS * 1000000UL);
  esp_deep_sleep_start();
}


int buildPacket(byte* buf) {
  buf[0] = MAGIC;
  buf[1] = (byte)(myId & 0xFF);
  buf[2] = (byte)((myId >> 8) & 0xFF);
  buf[3] = (byte)(seq & 0xFF);
  buf[4] = (byte)((seq >> 8) & 0xFF);
  buf[5] = (byte)((seq >> 16) & 0xFF);
  buf[6] = (byte)((seq >> 24) & 0xFF);
  buf[7] = TYPE_TILE;
  memcpy(buf + HDR_BYTES, TILE, TILE_BYTES);
  return HDR_BYTES + TILE_BYTES;
}


// Paint the tile into a band of the local e-ink, top-left at (0, y0).
// Each "block" in the tile is drawn as a 2x2 pixel square, so the full
// 64x22 tile covers 128x44 pixels on screen.
void drawTileInto(int y0) {
  for (int by = 0; by < TILE_H; by++) {
    for (int bx = 0; bx < TILE_W; bx++) {
      int bit = by * TILE_W + bx;
      // Pick the bit for this block out of the packed TILE[] byte array.
      // Each byte holds 8 blocks, highest bit first.
      byte mask = 0x80 >> (bit % 8);
      if (TILE[bit / 8] & mask) {
        display.fillRect(bx * 2, y0 + by * 2, 2, 2, BLACK);
      }
    }
  }
}


void showLocal() {
  display.portrait();                   // 128 wide x 296 tall
  int bh = display.height() / 6;

  display.fastmodeOn(false);
  display.setWindow(0, 0, display.width(), bh);
  DRAW (display) {
    drawTileInto(0);
  }
}


void setup() {
  myId = (int)(ESP.getEfuseMac() & 0xFFFF);

  pinMode(VOK_PIN, INPUT);
  if (digitalRead(VOK_PIN) == LOW) sleepAndPoll();

  seq = loadAndIncSeq();

  delay(QUIET_MS);

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, 8, TCXO, false);
  if (state == RADIOLIB_ERR_NONE) {
    byte buf[HDR_BYTES + MAX_PAYLOAD];
    int n = buildPacket(buf);
    for (int i = 0; i < TX_REPEATS; i++) {
      radio.transmit(buf, n);
      radio.finishTransmit();
      if (i < TX_REPEATS - 1) delay(40);
    }
    radio.sleep();
  }

  if (UPDATE_OWN_SCREEN) showLocal();

  sleepAndPoll();
}


void loop() {}
