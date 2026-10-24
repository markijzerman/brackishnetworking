/*
 * Harvest float (basic text) - Heltec Vision Master E290.
 *
 * Wakes on a timer, checks the BQ25570 V_OK pin, and if there's enough
 * energy it sends one short message over LoRa and draws it on its own
 * band of the e-ink. Then sleeps.
 *
 * Change what the float says by editing MESSAGE below.
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"
#include "esp_sleep.h"


// What the float sends. Short string, up to about 21 chars x 5 lines in
// the receiver's band.
const char* MESSAGE = "hello from the water";


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


// --- below here you shouldn't need to edit ---

// A tiny guide to the number types used in this sketch:
//   byte          = 1 byte, holds 0..255
//   int           = whole number, big enough for our counts (ESP32: 32-bit)
//   long          = larger whole number, used for the wake counter

// Wire format: one LoRa packet made of a small header + the message bytes.
//   byte 0:    magic (0xA5), lets the receiver spot our packets
//   byte 1..2: nodeId (16-bit), unique per float
//   byte 3..6: seq    (32-bit), counts wakes
//   byte 7:    type (1 = text)
//   byte 8+:   payload
const byte TYPE_TEXT   = 1;
const int  HDR_BYTES   = 8;
const int  MAX_PAYLOAD = 200;

SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

EInkDisplay_VisionMasterE290 display;
Preferences prefs;

int  myId;
long seq;


// Load the wake counter from flash, add 1, write it back.
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


// Assemble our outgoing LoRa packet into buf. Returns total length.
int buildPacket(byte* buf) {
  buf[0] = MAGIC;
  buf[1] = (byte)(myId & 0xFF);
  buf[2] = (byte)((myId >> 8) & 0xFF);
  buf[3] = (byte)(seq & 0xFF);
  buf[4] = (byte)((seq >> 8) & 0xFF);
  buf[5] = (byte)((seq >> 16) & 0xFF);
  buf[6] = (byte)((seq >> 24) & 0xFF);
  buf[7] = TYPE_TEXT;

  int plen = strlen(MESSAGE);
  if (plen > MAX_PAYLOAD) plen = MAX_PAYLOAD;
  memcpy(buf + HDR_BYTES, MESSAGE, plen);
  return HDR_BYTES + plen;
}


// Draw the message in our own band (top) of the local e-ink.
void showLocal() {
  display.portrait();                   // 128 wide x 296 tall
  int bh = display.height() / 6;        // one band = 1/6 of the screen tall

  display.fastmodeOn(false);
  display.setWindow(0, 0, display.width(), bh);
  DRAW (display) {
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.setCursor(3, 2);
    display.print(MESSAGE);
  }
}


void setup() {
  myId = (int)(ESP.getEfuseMac() & 0xFFFF);

  pinMode(VOK_PIN, INPUT);
  if (digitalRead(VOK_PIN) == LOW) sleepAndPoll();   // not enough energy yet

  seq = loadAndIncSeq();

  delay(QUIET_MS);

  // Transmit first, while the energy bank is at its highest.
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

  // Then update our own screen.
  if (UPDATE_OWN_SCREEN) showLocal();

  sleepAndPoll();
}


void loop() {}
