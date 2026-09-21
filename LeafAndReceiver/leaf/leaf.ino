/*
 * Heltec Vision Master E290 — HARVEST LEAF (tile / text, single-packet)
 *
 * Wakes on a timer, checks the BQ25570 V_OK line, and if the energy bank is
 * full enough it transmits ONE LoRa packet and updates just its own band on
 * the e-ink (fast mode, no full refresh — see note), then sleeps.
 *
 * GEOMETRY: the panel in PORTRAIT is 128 wide x 296 tall. Split top-to-bottom
 * into 6 bands, each band is 128 wide x ~49 tall. A tile is that band drawn at
 * 2x2 blocks -> 64 x 22 blocks = 176 bytes, which fits one LoRa packet.
 *
 * A message is ONE of two kinds, chosen in the EDIT block below:
 *   MODE_TILE  — a 64x22 one-bit drawing (a "sixth of the screen"), made in the
 *                HTML tool and pasted in as TILE[]. Sent as pixels.
 *   MODE_TEXT  — a short string. Sent as characters; the RECEIVER renders and
 *                wraps the words into its band.
 * Both ride in a single packet. A 1-byte type field tells the receiver which.
 *
 * FAST-MODE / PARTIAL REFRESH NOTE (important on a harvested node):
 *   Fast (partial) update needs an image already in the panel's controller RAM.
 *   A full power-down wipes it, so the FIRST boot after power loss does one
 *   normal refresh to lay a base image; after that fastmodeOn(false) reuses it.
 *
 * V_OK must be divided down (~1M/3.9M) before VOK_PIN: it swings to ~4 V and
 * the S3 GPIO max is 3.6 V.
 *
 * Board: "Heltec Vision Master E290"
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

// forward declaration so Arduino's auto-generated prototypes compile
// (the real definition lives down in PLUMBING). You can ignore this line.
struct SeqRec;

// ===========================================================================
//  SETTINGS  —  safe to edit
// ===========================================================================

const uint64_t POLL_US    = 30ULL * 1000000ULL; // re-check the bank every 30 s
const int      TX_REPEATS = 1;                  // copies sent per message
const bool     UPDATE_OWN_SCREEN = true;        // draw our band locally too
const int      QUIET_MS   = 400;                // settle time before transmit

// Radio — these MUST match the receiver, or nothing is heard
const float   FREQ  = 868.0;   // MHz  (use your region's band)
const float   BW    = 125.0;   // kHz
const uint8_t SF    = 7;       // spreading factor
const uint8_t CR    = 7;
const uint8_t SYNC  = 0x12;    // 0x12 = private network
const int8_t  POWER = 14;      // dBm  (max 14 in EU)
const float   TCXO  = 1.8;

// Wiring
#define VOK_PIN   17
#define LORA_NSS  8
#define LORA_DIO1 14
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_SCK  9
#define LORA_MISO 11
#define LORA_MOSI 10

// Tile geometry — one portrait band at 2x2 blocks. Don't change unless you also
// change the receiver and the HTML tool to match.
const int TILE_W  = 64;                             // blocks across (block = 2 px -> 128 px)
const int TILE_H  = 22;                             // blocks down   (block = 2 px -> 44 px)
const int TILE_BYTES = (TILE_W * TILE_H + 7) / 8;   // = 176 bytes, packed 1bpp

// ===========================================================================
//  WHAT THIS NODE SENDS   <<<<<<  EDIT THIS BLOCK  >>>>>>
// ===========================================================================
//  Pick ONE mode. Leave the other alone.
//
//  MODE_TILE : paste a drawing from the HTML tool into TILE[] below. It is a
//              64x22 grid packed to 176 bytes, row by row, MSB first.
//  MODE_TEXT : set MESSAGE to a short string (a band fits ~21 chars x ~5 lines).
//              The receiver draws and wraps the words.
//
//  Beginners wanting "random words / rotating messages" can build that logic
//  here — e.g. keep a WORDS[] list and pick one by seq — then assign it to
//  MESSAGE before send. That is all user territory.
// ---------------------------------------------------------------------------

#define MODE_TILE 0
#define MODE_TEXT 1

const uint8_t MODE = MODE_TILE;          // <-- choose MODE_TILE or MODE_TEXT

// --- for MODE_TEXT ---
const char* MESSAGE = "hello from the water";

// --- for MODE_TILE ---  (paste from the HTML drawing tool; 176 bytes)
const uint8_t TILE[TILE_BYTES] = {
  0x00, 0x07, 0xFF, 0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x03, 0xC7, 0xFF, 0xFF,
  0xFF, 0xFF, 0xC0, 0x00, 0x06, 0x60, 0x00, 0xFF, 0xFF, 0xFF, 0xC1, 0xC0,
  0x0C, 0x30, 0x00, 0x00, 0x00, 0x0F, 0xC1, 0xE0, 0x08, 0x10, 0x00, 0x78,
  0x00, 0x00, 0x01, 0xF0, 0x08, 0x10, 0x00, 0x7F, 0x0F, 0x80, 0x01, 0xF0,
  0x10, 0x08, 0x00, 0x0F, 0xFF, 0xF8, 0x00, 0xF0, 0x10, 0x08, 0x00, 0x03,
  0xF8, 0xFE, 0x00, 0xF0, 0x10, 0x08, 0x00, 0x00, 0x00, 0x0E, 0x00, 0xF0,
  0x10, 0x0B, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xF0, 0x10, 0x0A, 0x00, 0x00,
  0x00, 0x00, 0x10, 0xE0, 0x10, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x10, 0xE0,
  0x10, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x10, 0xE0, 0x10, 0x0A, 0x00, 0x3F,
  0xFF, 0xFE, 0x10, 0xF0, 0x10, 0x0A, 0x00, 0x3F, 0xFF, 0xFE, 0x10, 0xF0,
  0x08, 0x12, 0x00, 0x3F, 0xFF, 0xFE, 0x10, 0x70, 0x08, 0x12, 0x00, 0x3F,
  0xFF, 0xFE, 0x10, 0x70, 0x0C, 0x32, 0x00, 0x3F, 0xFF, 0xFE, 0x10, 0x78,
  0x06, 0x62, 0x00, 0x00, 0x00, 0x00, 0x10, 0x78, 0x03, 0xC3, 0xFF, 0xFF,
  0xFF, 0xFF, 0xF0, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ===========================================================================
//  PLUMBING  —  you don't need to edit anything below this line
// ===========================================================================

// Wire format, all in one packet:
//   magic(1) nodeId(2) seq(4) type(1)  then the payload bytes.
//   type 0 = tile pixels, type 1 = text characters.
const uint8_t MAGIC       = 0xA5;
const int     HDR_BYTES   = 8;      // magic1 + node2 + seq4 + type1
const int     MAX_PAYLOAD = 200;    // keeps header+payload inside one packet

struct __attribute__((packed)) Header {
  uint8_t  magic;
  uint16_t nodeId;
  uint32_t seq;
  uint8_t  type;
};

SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

EInkDisplay_VisionMasterE290 display;
Preferences prefs;

uint16_t myId;
uint32_t seq;

// ---- corruption-tolerant counter -----------------------------------------
const uint32_t SEQ_FLOOR = 1;
const uint32_t SEQ_CEIL  = 4000000000;

struct SeqRec { uint32_t value; uint32_t writeNo; uint32_t sum; };

static uint32_t checksum(uint32_t v, uint32_t w) { return v * 2654435761UL ^ (w + 0x9E3779B9UL); }
static bool     valid(const SeqRec& r) {
  return r.sum == checksum(r.value, r.writeNo) &&
         r.value >= SEQ_FLOOR && r.value <= SEQ_CEIL;
}

static uint32_t g_writeNo = 0;

uint32_t loadSeq() {
  if (!prefs.begin("leaf", true)) { nvs_flash_erase(); nvs_flash_init(); g_writeNo = 0; return 0; }
  SeqRec a{}, b{};
  prefs.getBytes("s0", &a, sizeof(a));
  prefs.getBytes("s1", &b, sizeof(b));
  prefs.end();
  bool va = valid(a), vb = valid(b);
  const SeqRec* best = nullptr;
  if (va && vb) best = (a.writeNo >= b.writeNo) ? &a : &b;
  else if (va)  best = &a;
  else if (vb)  best = &b;
  if (!best) { g_writeNo = 0; return 0; }
  g_writeNo = best->writeNo;
  return best->value;
}

void saveSeq(uint32_t value) {
  g_writeNo++;
  SeqRec r{ value, g_writeNo, checksum(value, g_writeNo) };
  const char* slot = (g_writeNo & 1) ? "s1" : "s0";
  if (prefs.begin("leaf", false)) { prefs.putBytes(slot, &r, sizeof(r)); prefs.end(); }
}

void sleepAndPoll() {
  esp_sleep_enable_timer_wakeup(POLL_US);
  esp_deep_sleep_start();
}

// Build the outgoing packet into buf[]; return total length (or -1 if too big).
int buildPacket(uint8_t* buf, uint16_t nodeId, uint32_t s) {
  Header h { MAGIC, nodeId, s, MODE };
  memcpy(buf, &h, HDR_BYTES);
  int plen = 0;
  if (MODE == MODE_TILE) {
    plen = TILE_BYTES;
    if (plen > MAX_PAYLOAD) return -1;
    memcpy(buf + HDR_BYTES, TILE, plen);
  } else {
    plen = strlen(MESSAGE);
    if (plen > MAX_PAYLOAD) plen = MAX_PAYLOAD;    // clip over-long text
    memcpy(buf + HDR_BYTES, MESSAGE, plen);
  }
  return HDR_BYTES + plen;
}

// Paint one 2x2-block tile into a band, top-left at (0, y0). Full 128 px wide.
void drawTileInto(const uint8_t* tile, int y0) {
  for (int by = 0; by < TILE_H; by++)
    for (int bx = 0; bx < TILE_W; bx++) {
      int bit = by * TILE_W + bx;
      if (tile[bit >> 3] & (0x80 >> (bit & 7)))
        display.fillRect(bx * 2, y0 + by * 2, 2, 2, BLACK);
    }
}

// Update just this node's own band (band 0, top), in fast mode.
void showLocal() {
  display.portrait();                          // 128 wide x 296 tall
  const int H = display.height();
  int y0 = 0;                                  // leaf always uses band 0
  int bh = H / 6;

  // Cold boot after full power loss: fast mode needs a base image in RAM, so
  // do one normal refresh the first time, then fast mode from then on.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) display.clear();
  display.fastmodeOn(false);

  display.setWindow(0, y0, display.width(), bh);
  DRAW (display) {
    int wy = display.window.top();
    if (MODE == MODE_TILE) {
      drawTileInto(TILE, wy);
    } else {
      display.setTextColor(BLACK);
      display.setTextSize(1);
      display.setCursor(3, wy + 2);
      display.print(MESSAGE);
    }
  }
}

void setup() {
  myId = (uint16_t)(ESP.getEfuseMac() & 0xFFFF);

  pinMode(VOK_PIN, INPUT);
  if (digitalRead(VOK_PIN) == LOW) sleepAndPoll();   // not enough energy yet

  seq = loadSeq() + 1;
  saveSeq(seq);

  delay(QUIET_MS);

  // ---- transmit (highest priority, first) ----
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, 8, TCXO, false);
  if (state == RADIOLIB_ERR_NONE) {
    uint8_t buf[HDR_BYTES + MAX_PAYLOAD];
    int n = buildPacket(buf, myId, seq);
    if (n > 0) {
      for (int i = 0; i < TX_REPEATS; i++) {
        radio.transmit(buf, n);
        radio.finishTransmit();
        if (i < TX_REPEATS - 1) delay(40);
      }
    }
    radio.sleep();
  }

  // ---- own screen (fast-mode band update, after TX) ----
  if (UPDATE_OWN_SCREEN) showLocal();

  sleepAndPoll();
}

void loop() {}