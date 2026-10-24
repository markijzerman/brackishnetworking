/*
 * Receiver (banded portrait, ring feed) - Heltec Vision Master E290, USB powered.
 *
 * The screen (128 x 296, portrait) is split top-to-bottom into 6 bands, each
 * 128 x ~49 px. Each new packet drops into the next band down, wrapping back
 * to band 0 after the sixth. Placement is driven by the receiver's own
 * running count, so the feed marches in order regardless of each float's seq.
 *
 * Two kinds of message, told apart by a 1-byte type field:
 *   type 0 (tile): a 64x22 one-bit drawing, painted as 2x2 blocks (128x44 px)
 *   type 1 (text): characters, word-wrapped into the band
 *
 * When the ring wraps (band 0 comes round again) the screen does one full
 * refresh to clear e-ink ghosting; other bands use fast mode.
 *
 * Duplicate packets (a float might send the same thing more than once) are
 * ignored based on (nodeId, seq).
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"


// --- settings ---

const int BANDS = 6;

// Must match the float.
const int TILE_W     = 64;
const int TILE_H     = 22;
const int TILE_BYTES = (TILE_W * TILE_H + 7) / 8;   // 176

// Radio - must match the float.
const float FREQ  = 868.0;
const float BW    = 125.0;
const byte  SF    = 7;
const byte  CR    = 7;
const byte  SYNC  = 0x12;
const int   POWER = 14;
const float TCXO  = 1.8;
const byte  MAGIC = 0xA5;   // packet marker; change to run two setups in the same room

// Wiring
#define LORA_NSS  8
#define LORA_DIO1 14
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_SCK  9
#define LORA_MISO 11
#define LORA_MOSI 10


// --- below here you shouldn't need to edit ---

// A tiny guide to the number types used in this sketch:
//   byte = 1 byte, holds 0..255
//   int  = whole number (ESP32: 32-bit)
//   long = larger whole number

// Wire format: must match the float.
//   byte 0:    magic (0xA5)
//   byte 1..2: nodeId (16-bit)
//   byte 3..6: seq    (32-bit)
//   byte 7:    type   (0 = tile, 1 = text)
//   byte 8+:   payload
const byte TYPE_TILE  = 0;
const byte TYPE_TEXT  = 1;
const int  HDR_BYTES  = 8;
const int  MAX_PACKET = HDR_BYTES + 200;

SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

EInkDisplay_VisionMasterE290 display;
Preferences prefs;

// IRAM_ATTR puts this function in RAM so the radio can call it from inside
// an interrupt - interrupt handlers can't live in flash. Just a flag flip.
volatile bool rxFlag = false;
void IRAM_ATTR onDio1() { rxFlag = true; }

long total   = 0;    // total received (persisted); drives band placement
int  lastId  = 0;
long lastSeq = 0;


// --- band rendering ---

// Paint one 64x22 tile into the band whose top is at y0. Each "block" of
// the tile becomes a 2x2 pixel square, so the whole tile covers 128x44.
void drawTile(const byte* tile, int len, int y0) {
  for (int by = 0; by < TILE_H; by++) {
    for (int bx = 0; bx < TILE_W; bx++) {
      int bit = by * TILE_W + bx;
      if (bit / 8 >= len) return;                   // guard against short packets
      // Pick the bit for this block out of the packed byte array. Each
      // byte holds 8 blocks, highest bit first.
      byte mask = 0x80 >> (bit % 8);
      if (tile[bit / 8] & mask) {
        display.fillRect(bx * 2, y0 + by * 2, 2, 2, BLACK);
      }
    }
  }
}

void drawText(const char* txt, int len, int y0, int bh, int w) {
  display.setTextColor(BLACK);
  display.setTextSize(1);
  const int CW = 6, LH = 8;             // 6x8 default font
  int cols = w / CW;                    // ~21 chars per line at w = 128
  int x = 2, y = y0 + 1, col = 0;
  for (int i = 0; i < len; i++) {
    char c = txt[i];
    if (c == '\n' || col >= cols) { col = 0; x = 2; y += LH; }
    if (y + LH > y0 + bh) break;        // ran out of band height
    if (c == '\n') continue;
    display.setCursor(x, y);
    display.print(c);
    x += CW; col++;
  }
}

void drawLabel(int y0, int bh, long s, int rssi) {
  char lbl[24];
  snprintf(lbl, sizeof(lbl), "#%ld %ddB", s, rssi);
  int tw = 6 * (int)strlen(lbl) + 5;
  display.fillRect(0, y0 + bh - 10, tw, 10, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(3, y0 + bh - 9);
  display.print(lbl);
}

void showBand(byte type, const byte* payload, int plen, long s, float rssi) {
  display.portrait();
  const int W = display.width();        // 128
  const int H = display.height();       // 296

  int slot = (int)((total - 1) % BANDS);
  int y0   = (slot * H) / BANDS;
  int bh   = (((slot + 1) * H) / BANDS) - y0;

  // When band 0 comes round again, do one full refresh first to clear
  // e-ink ghosting that has built up over the previous 6 writes.
  bool wrap = (slot == 0);
  if (wrap) { display.fullscreen(); display.clear(); }
  display.fastmodeOn(false);

  display.setWindow(0, y0, W, bh);
  DRAW (display) {
    int wy = display.window.top();
    int wh = display.window.height();
    if (type == TYPE_TILE) drawTile(payload, plen, wy);
    else                   drawText((const char*)payload, plen, wy, wh, W);
    drawLabel(wy, wh, s, (int)rssi);
  }
  Serial.printf("slot %d  seq %ld  %d dBm  type %d  %d B\n",
                slot, s, (int)rssi, type, plen);
}


void showWaiting() {
  display.portrait();
  display.fullscreen();
  display.clear();
  DRAW (display) {
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.setCursor(6, 8);
    display.print("waiting");
    display.setCursor(6, 22);
    display.print("for a float");
    display.setCursor(6, display.height() - 16);
    char buf[20];
    snprintf(buf, sizeof(buf), "total %ld", total);
    display.print(buf);
  }
}


void setup() {
  Serial.begin(115200);

  prefs.begin("rx", false);
  total = prefs.getULong("total", 0);
  prefs.end();

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, 8, TCXO, false);
  if (state != RADIOLIB_ERR_NONE) {
    display.portrait();
    display.fullscreen();
    display.clear();
    DRAW (display) {
      display.setTextColor(BLACK);
      display.setCursor(6, 8);
      char buf[24];
      snprintf(buf, sizeof(buf), "radio FAIL %d", state);
      display.print(buf);
    }
    Serial.printf("radio init failed: %d\n", state);
    while (true) delay(1000);
  }
  radio.setDio1Action(onDio1);
  radio.startReceive();

  showWaiting();
}


void loop() {
  if (!rxFlag) return;
  rxFlag = false;

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);   // reclaim bus from e-ink

  byte buf[MAX_PACKET];
  int len   = radio.getPacketLength();
  int state = radio.readData(buf, sizeof(buf));
  float rssi = radio.getRSSI();
  radio.startReceive();

  if (state != RADIOLIB_ERR_NONE || len < HDR_BYTES) return;

  // Pull the header fields out of the first 8 bytes.
  if (buf[0] != MAGIC) return;
  int  nodeId = buf[1] | (buf[2] << 8);
  long seq    = (long)buf[3]
              | ((long)buf[4] << 8)
              | ((long)buf[5] << 16)
              | ((long)buf[6] << 24);
  byte type   = buf[7];

  // Ignore repeated packets from the same float.
  if (nodeId == lastId && seq == lastSeq) return;
  lastId  = nodeId;
  lastSeq = seq;

  total++;
  prefs.begin("rx", false);
  prefs.putULong("total", total);
  prefs.end();

  int plen = len - HDR_BYTES;
  showBand(type, buf + HDR_BYTES, plen, seq, rssi);
}
