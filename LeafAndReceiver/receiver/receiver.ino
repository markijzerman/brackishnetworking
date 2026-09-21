/*
 * Heltec Vision Master E290 - RECEIVER (banded portrait, ring feed)  USB powered
 *
 * PORTRAIT: 128 wide x 296 tall. Split top-to-bottom into 6 bands, each 128 x
 * ~49. Each new packet drops into the next band down, wrapping after 6.
 * Placement comes from the receiver's own running count (total), so it marches
 * in order regardless of the leaf's seq.
 *
 * Two kinds of message, told apart by a 1-byte type field:
 *   type 0 (tile) : a 64x22 one-bit drawing -> painted as 2x2 blocks (128x44).
 *   type 1 (text) : characters -> drawn and word-wrapped into the band.
 *
 * When the ring wraps (band 0 comes round again) the whole screen is refreshed
 * once to clear e-ink ghosting; other bands use fast mode.
 *
 * Duplicate packets (the leaf sends each three times) are ignored.
 *
 * Board: "Heltec Vision Master E290"
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"

const int BANDS = 6;

// must match the leaf
const int TILE_W = 64;
const int TILE_H = 22;
const int TILE_BYTES = (TILE_W * TILE_H + 7) / 8;   // 176

EInkDisplay_VisionMasterE290 display;
Preferences prefs;

#define LORA_NSS  8
#define LORA_DIO1 14
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_SCK  9
#define LORA_MISO 11
#define LORA_MOSI 10
SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

const float   FREQ  = 868.0;
const float   BW    = 125.0;
const uint8_t SF    = 7;
const uint8_t CR    = 7;
const uint8_t SYNC  = 0x12;
const int8_t  POWER = 14;
const float   TCXO  = 1.8;

// Wire format must match the leaf exactly.
const uint8_t MAGIC       = 0xA5;
const int     HDR_BYTES   = 8;      // magic1 + node2 + seq4 + type1
const int     MAX_PACKET  = HDR_BYTES + 200;
#define TYPE_TILE 0
#define TYPE_TEXT 1

struct __attribute__((packed)) Header {
  uint8_t  magic;
  uint16_t nodeId;
  uint32_t seq;
  uint8_t  type;
};

volatile bool rxFlag = false;
void IRAM_ATTR onDio1() { rxFlag = true; }

uint32_t total   = 0;     // total received (persisted) - also drives placement
uint16_t lastId  = 0;
uint32_t lastSeq = 0;

// ---- band rendering -------------------------------------------------------

void drawTile(const uint8_t* tile, int len, int y0) {
  for (int by = 0; by < TILE_H; by++)
    for (int bx = 0; bx < TILE_W; bx++) {
      int bit = by * TILE_W + bx;
      if ((bit >> 3) >= len) return;                 // guard short packets
      if (tile[bit >> 3] & (0x80 >> (bit & 7)))
        display.fillRect(bx * 2, y0 + by * 2, 2, 2, BLACK);
    }
}

void drawText(const char* txt, int len, int y0, int bh, int W) {
  display.setTextColor(BLACK);
  display.setTextSize(1);
  const int CW = 6, LH = 8;                           // 6x8 default font
  int cols = W / CW;                                  // ~21 chars per line at W=128
  int x = 2, y = y0 + 1, col = 0;
  for (int i = 0; i < len; i++) {
    char c = txt[i];
    if (c == '\n' || col >= cols) { col = 0; x = 2; y += LH; }
    if (y + LH > y0 + bh) break;                      // ran out of band height
    if (c == '\n') continue;
    display.setCursor(x, y);
    display.print(c);
    x += CW; col++;
  }
}

void drawLabel(int y0, int bh, uint32_t s, int rssi) {
  char lbl[24];
  snprintf(lbl, sizeof(lbl), "#%lu %ddB", (unsigned long)s, rssi);
  int tw = 6 * (int)strlen(lbl) + 5;
  display.fillRect(0, y0 + bh - 10, tw, 10, WHITE);   // clear label strip (overlay)
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(3, y0 + bh - 9);
  display.print(lbl);
}

void showBand(uint8_t type, const uint8_t* payload, int plen,
              uint32_t s, float rssi) {
  display.portrait();
  const int W = display.width();                       // 128
  const int H = display.height();                      // 296

  int slot = (int)((total - 1) % BANDS);
  int y0   = (slot * H) / BANDS;
  int bh   = (((slot + 1) * H) / BANDS) - y0;

  // Wrap point (band 0 comes round again) -> one full refresh to clear ghosting.
  bool wrap = (slot == 0);
  if (wrap) { display.fullscreen(); display.clear(); }
  display.fastmodeOn(false);

  display.setWindow(0, y0, W, bh);
  DRAW (display) {
    int wy = display.window.top();
    int wh = display.window.height();
    if (type == TYPE_TILE) drawTile(payload, plen, wy);
    else                   drawText((const char*)payload, plen, wy, wh, W);
    drawLabel(wy, wh, s, (int)rssi);                   // overlays bottom-left
  }
  Serial.printf("slot %d <- node #%lu  %d dBm  type %d  %d B\n",
                slot, (unsigned long)s, (int)rssi, type, plen);

  Serial.printf("plen=%d\n", plen);
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
    display.print("for a leaf");
    display.setCursor(6, display.height() - 16);
    char buf[20];
    snprintf(buf, sizeof(buf), "total %lu", (unsigned long)total);
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

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);  // reclaim bus from e-ink

  uint8_t buf[MAX_PACKET];
  int len   = radio.getPacketLength();
  int state = radio.readData(buf, sizeof(buf));
  float rssi = radio.getRSSI();
  radio.startReceive();

  if (state != RADIOLIB_ERR_NONE || len < HDR_BYTES) return;

  Header h;
  memcpy(&h, buf, HDR_BYTES);
  if (h.magic != MAGIC) return;

  // Ignore the leaf's 3 repeats of the same message.
  // TEMP TEST
  // if (h.nodeId == lastId && h.seq == lastSeq) return;
  lastId = h.nodeId;
  lastSeq = h.seq;

  total++;
  prefs.begin("rx", false);
  prefs.putULong("total", total);
  prefs.end();

  int plen = len - HDR_BYTES;
  showBand(h.type, buf + HDR_BYTES, plen, h.seq, rssi);
}
