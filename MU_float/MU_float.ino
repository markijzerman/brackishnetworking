/*
 * MU float - exhibition float that talks back. Heltec Vision Master E290.
 *
 * For the MU exhibition the floats sit in buckets near the receiver. While
 * there's too little energy (V_OK low) the float sleeps until V_OK goes high.
 * Then, every round (with a short rest in between) it:
 *   1. asks the receiver for messages other floats have sent since it last
 *      asked, and listens for a few seconds to catch some of them
 *   2. sends its own new message to the receiver
 *   3. redraws its whole screen:
 *        top:    its own message, under a black bar  SENT BY THIS FLOAT
 *        below:  up to 4 messages it got, under      RECEIVED FROM / OTHER FLOATS
 *                each tagged with the float it came from
 *   4. sleeps
 *
 * The received messages are kept in flash, so they stay on screen (and
 * survive a power loss) until newer ones push them out.
 *
 * Pick what this float makes with MODE below:
 *   MODE_PICTURES  sends drawings from a list, one per wake
 *   MODE_POETRY    writes a short generative poem each wake
 *   MODE_SHAPES    makes a generative composition of circles and lines
 *
 * Needs the receiver sketch from this repo (the version with the mailbox).
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"


// =====================================================================
// --- choose the behaviour ---
// =====================================================================

#define MODE_PICTURES 0
#define MODE_POETRY   1
#define MODE_SHAPES   2

const int MODE = MODE_PICTURES;      // MODE_PICTURES, MODE_POETRY or MODE_SHAPES


// Tile geometry: one portrait band at 2x2 blocks. Don't change unless you
// also change the receiver and tile_drawer.html to match.
const int TILE_W     = 64;                            // blocks across (block = 2 px -> 128 px)
const int TILE_H     = 22;                            // blocks down   (block = 2 px -> 44 px)
const int TILE_BYTES = (TILE_W * TILE_H + 7) / 8;     // 176 bytes, packed 1 bit per block


// ---------------------------------------------------------------------
// MODE_PICTURES: a list of drawings
// ---------------------------------------------------------------------
// Make a drawing in tile_drawer.html (in this folder), click copy, paste it
// below, and rename TILE to a new name (PIC_4, PIC_5, ...). Then add that
// name to the PICTURES list underneath. Add as many as you like.

const bool PICTURES_RANDOM = false;   // false: go through the list in order, one per wake
                                      // true:  pick one at random each wake

const uint8_t PIC_1[TILE_BYTES] = {
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

// waves
const uint8_t PIC_2[TILE_BYTES] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0xFC, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x03, 0x03, 0x80,
  0x01, 0xC1, 0xC0, 0x00, 0xC0, 0x0C, 0x00, 0x60, 0x06, 0x00, 0x30, 0x03,
  0x30, 0x30, 0x00, 0x18, 0x18, 0x00, 0x0C, 0x0C, 0x0F, 0xC0, 0x00, 0x07,
  0xE0, 0x00, 0x03, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x7E, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x0F, 0x01, 0x81, 0xC0, 0x00,
  0xE0, 0xE0, 0x00, 0x70, 0x06, 0x00, 0x30, 0x03, 0x00, 0x18, 0x01, 0x80,
  0x18, 0x00, 0x0C, 0x0C, 0x00, 0x06, 0x06, 0x00, 0xE0, 0x00, 0x03, 0xF0,
  0x00, 0x01, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x3F, 0x00, 0x00, 0x0F, 0x80, 0x00, 0x07, 0xE0, 0xC0, 0xE0, 0x00, 0x70,
  0x70, 0x00, 0x38, 0x18, 0x00, 0x18, 0x01, 0x80, 0x0C, 0x00, 0xC0, 0x06,
  0x00, 0x06, 0x06, 0x00, 0x03, 0x03, 0x00, 0x01, 0x00, 0x01, 0xF8, 0x00,
  0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// moon over water
const uint8_t PIC_3[TILE_BYTES] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xC0, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x03, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xC0, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x0F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x1F, 0xC0, 0x03,
  0xF8, 0x00, 0x7F, 0x00, 0xC0, 0x60, 0x38, 0x1C, 0x06, 0x03, 0x80, 0xC0,
  0x3F, 0x80, 0x07, 0xE0, 0x01, 0xFC, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x33, 0x20, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00
};

const uint8_t* PICTURES[] = { PIC_1, PIC_2, PIC_3 };


// ---------------------------------------------------------------------
// MODE_POETRY: word lists for the generative poems
// ---------------------------------------------------------------------
// Each wake picks one of a few poem shapes (see buildPoem further down)
// and fills it with words from these lists. Add, remove or change words
// freely - the counts are worked out automatically.
// Room on screen: about 21 characters per line, 4 lines.

const char* SUBJECTS[] = {
  "the float", "the tide",  "the water", "the wind",
  "moss",      "salt",      "a fish",    "the shore",
  "the reeds", "the moon",  "a bucket",  "a bird",
};

const char* VERBS[] = {
  "drifts",    "waits",     "sings",     "sleeps",
  "listens",   "watches",   "breathes",  "hums",
  "remembers", "forgets",   "carries",   "counts",
};

const char* PLACES[] = {
  "in the reeds",      "with the tide",    "under the moon",
  "against the shore", "through the salt", "into the dark",
  "toward morning",    "beside me",        "for hours",
  "without end",       "in the bucket",    "near the others",
};

const char* IMAGES[] = {
  "salt light",   "slow water",  "a cold spark",
  "green static", "one ripple",  "a soft hum",
  "low current",  "brine",       "a thin signal",
  "wet copper",   "grey morning","a held breath",
};


// ---------------------------------------------------------------------
// MODE_SHAPES: generative circles and lines
// ---------------------------------------------------------------------
// Four styles, see buildShapes further down:
//   0 constellation - dots joined by lines
//   1 ripples       - rings spreading from a few centres
//   2 tide lines    - long wavering lines and a sun or moon
//   3 orbit         - a big ring with satellites and spokes

const int SHAPE_STYLE = -1;           // -1 = a different style each wake, or 0..3 to fix one


// =====================================================================
// --- settings ---
// =====================================================================

// Energy and timing.
//   USE_VOK = true:  harvester float. While V_OK is low the float sleeps until
//                    the pin goes high (no polling). After a round it rests.
//   USE_VOK = false: USB or battery float. V_OK isn't checked at all.
// The rest timer is still needed with V_OK: without it a float with plenty
// of energy would either send non-stop or, with V_OK staying high, never
// wake up again.
const bool USE_VOK            = true;
const bool DEBUG_SERIAL       = false; // true: print what happens each round over USB (costs energy)
const long REST_SECONDS       = 30;    // rest after each round...
const long REST_JITTER_SECONDS = 10;   // ...plus 0..N random seconds, so floats drift apart
const int  TX_REPEATS         = 1;     // copies of our own message sent per wake
const int  QUIET_MS           = 20;    // settle time before using the radio

// Listening. With the fast radio below, an answer is back within ~0.1 s
// and the receiver says "that's all", so the float stops listening early.
// The timeouts only matter when the receiver doesn't answer.
const int  FETCH_MAX          = 4;     // most messages to ask for per wake (screen holds 4)
const int  REQUEST_TRIES      = 1;     // ask again if the receiver doesn't answer (each try costs energy)
const long REPLY_TIMEOUT_MS   = 250;   // how long to wait for the receiver's first answer
const long PACKET_GAP_MS      = 150;   // after an answer, how long to wait for the next one

const int  FULL_REFRESH_EVERY = 10;    // full (clean, but slow: ~2 s) e-ink refresh every N wakes,
                                       // fast (~0.5 s) otherwise. 0 = never.

// Screen labels. Text size 1 fits 21 characters across.
const char* LABEL_SENT        = "SENT BY THIS FLOAT";
const char* LABEL_NOT_SENT    = "THIS FLOAT (NOT SENT)";
const char* LABEL_RECEIVED_1  = "RECEIVED FROM";
const char* LABEL_RECEIVED_2  = "OTHER FLOATS";

// Radio - must match the receiver (FAST_RADIO = true there), or nothing is
// heard. Short range, fast setting: the floats sit close to the receiver,
// so packets can be ~10x shorter on air than the workshop setting
// (868.0 / 125 kHz / SF7 / CR 7), which saves a lot of energy.
const float FREQ     = 868.3;  // MHz; keeps the 500 kHz channel inside 868.0-868.6
const float BW       = 500.0;  // kHz
const byte  SF       = 6;      // spreading factor
const byte  CR       = 5;      // coding rate 4/5
const int   PREAMBLE = 12;     // SF6 needs at least 12
const byte  SYNC  = 0x12;   // private network
const int   POWER = 14;     // dBm (max 14 in EU)
const float TCXO  = 1.8;
const byte  MAGIC = 0xA5;   // packet marker; change to run two setups in the same room

// Wiring
#define VOK_PIN   17
#define VEXT_PIN  18        // board's power switch for the e-ink, HIGH = on
#define LORA_NSS  8
#define LORA_DIO1 14
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_SCK  9
#define LORA_MISO 11
#define LORA_MOSI 10


// =====================================================================
// --- below here you shouldn't need to edit ---
// =====================================================================

// A tiny guide to the number types used in this sketch:
//   byte / uint8_t = 1 byte, holds 0..255
//   int  = whole number (ESP32: 32-bit)
//   long = larger whole number, used for counters

#define COUNT(list) ((int)(sizeof(list) / sizeof(list[0])))

// Wire format: must match the receiver.
//   byte 0:    magic (0xA5)
//   byte 1..2: nodeId (16-bit), unique per float
//   byte 3..6: seq    (32-bit), counts wakes
//   byte 7:    type
//   byte 8+:   payload
// Types 0..2 are messages; 3..5 are the mailbox conversation:
//   3 request  float -> receiver   payload: lastSeen(4) want(1)
//   4 relay    receiver -> float   header nodeId/seq = the original sender's
//                                  payload: to(2) mailId(4) ageSeconds(4) kind(1) content
//   5 end      receiver -> float   payload: to(2) count(1)
const byte TYPE_TILE    = 0;
const byte TYPE_TEXT    = 1;
const byte TYPE_SHAPES  = 2;
const byte TYPE_REQUEST = 3;
const byte TYPE_RELAY   = 4;
const byte TYPE_END     = 5;

const int HDR_BYTES   = 8;
const int RELAY_HDR   = 19;          // header + to + mailId + age + kind
const int MAX_CONTENT = 200;
const int MAX_PACKET  = 255;         // LoRa limit

// Shapes: drawn in a 128 x 44 box (same as a tile). 5 bytes per shape.
const int  SHAPE_W      = 128;
const int  SHAPE_H      = 44;
const int  MAX_SHAPES   = MAX_CONTENT / 5;   // 40
const byte SHAPE_LINE   = 0;         // line   (a,b) -> (c,d)
const byte SHAPE_CIRCLE = 1;         // circle centre (a,b), radius c
const byte SHAPE_DISC   = 2;         // filled circle centre (a,b), radius c

// Screen layout (portrait, 128 x 296).
const int BAR_H      = 10;           // one-line label bar
const int ITEM_H     = 49;           // one message, same height as a receiver band
const int TAG_H      = 10;           // "from float ..." tag under a received message
const int OWN_Y      = BAR_H;                    // 10
const int RECV_BAR_Y = OWN_Y + ITEM_H + 4;       // 63
const int RECV_BAR_H = 19;                       // two-line bar
const int RECV_Y     = RECV_BAR_Y + RECV_BAR_H + 1;   // 83
const int RECV_STEP  = ITEM_H + 4;               // 53: item + dotted divider
const int INBOX_SIZE = 4;                        // received messages that fit on screen

SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

// The e-ink, plus two extras for the fast refresh.
//
// A fast refresh only flips the pixels that changed. To know what changed,
// the display compares the new image with a copy of the previous one in its
// own memory. That memory is lost whenever the display loses power (we cut
// it during sleep, and the harvester cuts everything when V_OK drops), and
// the fast refresh then leaves bits of old image behind or misses new bits.
// So we keep our own copy of the last image in the ESP32's RTC memory (which
// survives deep sleep) and put it back into the display before each fast
// refresh. After a real power loss that copy is gone too: then we do a full
// refresh, which doesn't need it.
class FloatDisplay : public EInkDisplay_VisionMasterE290 {
public:
  static const int FRAME_BYTES = 128 / 8 * 296;     // 4736: the whole panel, 1 bit per pixel

  // Copy the image just drawn (call right after DRAW).
  void saveFrame(uint8_t* dst) { memcpy(dst, page_black, FRAME_BYTES); }

  // Put a saved image into the display's "previous image" memory, without
  // refreshing. Call after fastmodeOn() and before DRAW.
  void restoreFrame(const uint8_t* src) {
    setMemoryArea(1, 0, 16, 295);                   // whole panel (this panel's x starts at 1)
    sendCommand(0x26);                              // write "previous image" memory
    for (int i = 0; i < FRAME_BYTES; i++) sendData(src[i]);
    endImageTxQuiet();
  }
};

FloatDisplay display;
Preferences prefs;

// Last image on screen. RTC memory survives deep sleep, not a power loss.
RTC_NOINIT_ATTR uint8_t lastFrame[FloatDisplay::FRAME_BYTES];
RTC_DATA_ATTR   bool    haveFrame = false;          // back to false after a power loss

// A message: ours, or one received from another float. (Defined before any
// function, so the Arduino IDE can use it in the function list it builds
// behind the scenes.)
struct Item {
  long    mailId;                // receiver's number for it (0 for our own)
  int     from;                  // nodeId of the float that made it
  uint8_t kind;                  // TYPE_TILE / TYPE_TEXT / TYPE_SHAPES
  uint8_t len;
  uint8_t data[MAX_CONTENT];
};

volatile bool radioFlag = false;
void IRAM_ATTR onDio1() { radioFlag = true; }

int  myId;
long seq;
long lastSeen = 0;               // newest receiver mail id we've been given

Item own;                        // what we make this wake
bool sentOk = false;

Item inbox[INBOX_SIZE];          // newest first; kept in flash between wakes
int  inboxCount = 0;
bool isNew[INBOX_SIZE];          // arrived this wake

Item fetched[FETCH_MAX * REQUEST_TRIES];
int  fetchedCount = 0;


// --- little helpers for reading / writing numbers inside packets ---

void put16(byte* p, long v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(byte* p, long v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
int  get16(const byte* p)   { return p[0] | (p[1] << 8); }
long get32(const byte* p)   { return (long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24); }

void putHeader(byte* buf, byte type) {
  buf[0] = MAGIC;
  put16(buf + 1, myId);
  put32(buf + 3, seq);
  buf[7] = type;
}


// =====================================================================
// --- making our own message ---
// =====================================================================

void setOwnText(const String& s) {
  own.kind = TYPE_TEXT;
  own.len  = min((int)s.length(), MAX_CONTENT);
  memcpy(own.data, s.c_str(), own.len);
}

void buildPicture() {
  int n = COUNT(PICTURES);
  int pick = PICTURES_RANDOM ? random(n) : (int)((seq - 1) % n);
  own.kind = TYPE_TILE;
  own.len  = TILE_BYTES;
  memcpy(own.data, PICTURES[pick], TILE_BYTES);
}

const char* pickWord(const char** list, int n) { return list[random(n)]; }
#define PICK(list) pickWord(list, COUNT(list))

// A few poem shapes. '\n' starts a new line on screen.
void buildPoem() {
  String p;
  switch (random(4)) {
    case 0:   // the tide waits / in the bucket
      p = String(PICK(SUBJECTS)) + " " + PICK(VERBS) + "\n" + PICK(PLACES);
      break;
    case 1:   // brine, / one ripple, / the moon listens
      p = String(PICK(IMAGES)) + ",\n" + PICK(IMAGES) + ",\n"
        + PICK(SUBJECTS) + " " + PICK(VERBS);
      break;
    case 2:   // moss hums / toward morning. / a fish forgets.
      p = String(PICK(SUBJECTS)) + " " + PICK(VERBS) + "\n" + PICK(PLACES) + ".\n"
        + PICK(SUBJECTS) + " " + PICK(VERBS) + ".";
      break;
    default:  // salt light / for hours / for hours
      {
        String place = PICK(PLACES);
        p = String(PICK(IMAGES)) + "\n" + place + "\n" + place;
      }
      break;
  }
  setOwnText(p);
}

// Shapes are packed 5 bytes each into own.data.
int shapeCount = 0;

void addShape(byte op, int a, int b, int c, int d) {
  if (shapeCount >= MAX_SHAPES) return;
  byte* p = own.data + shapeCount * 5;
  p[0] = op; p[1] = a; p[2] = b; p[3] = c; p[4] = d;
  shapeCount++;
}

void addLine(int x1, int y1, int x2, int y2) {
  x1 = constrain(x1, 0, SHAPE_W - 1); x2 = constrain(x2, 0, SHAPE_W - 1);
  y1 = constrain(y1, 0, SHAPE_H - 1); y2 = constrain(y2, 0, SHAPE_H - 1);
  addShape(SHAPE_LINE, x1, y1, x2, y2);
}

// Circles that wouldn't fit inside the box are skipped, not squashed.
void addCircle(int cx, int cy, int r, bool filled) {
  if (r < 1) return;
  if (cx - r < 0 || cy - r < 0 || cx + r > SHAPE_W - 1 || cy + r > SHAPE_H - 1) return;
  addShape(filled ? SHAPE_DISC : SHAPE_CIRCLE, cx, cy, r, 0);
}

void buildShapes() {
  shapeCount = 0;
  int style = (SHAPE_STYLE >= 0) ? SHAPE_STYLE : random(4);

  if (style == 0) {                          // constellation
    int n = random(5, 9);
    int px = 0, py = 0;
    for (int i = 0; i < n; i++) {
      int x = random(4, SHAPE_W - 4), y = random(4, SHAPE_H - 4);
      addCircle(x, y, random(1, 3), true);
      if (i > 0) addLine(px, py, x, y);
      px = x; py = y;
    }
  } else if (style == 1) {                   // ripples
    int centres = random(1, 4);
    for (int c = 0; c < centres; c++) {
      int x = random(10, SHAPE_W - 10), y = random(8, SHAPE_H - 8);
      int step = random(3, 6);
      addCircle(x, y, 1, true);
      for (int r = step; r < 40; r += step) addCircle(x, y, r, false);
    }
  } else if (style == 2) {                   // tide lines
    int lines = random(5, 10);
    for (int i = 0; i < lines; i++) {
      int y = 14 + (i * (SHAPE_H - 16)) / lines + random(0, 3);
      int x = 0;
      while (x < SHAPE_W - 1) {              // wavering line in short segments
        int nx = min(x + (int)random(10, 24), SHAPE_W - 1);
        addLine(x, y, nx, y + random(-1, 2));
        x = nx;
      }
    }
    int r = random(4, 8);
    addCircle(random(r + 2, SHAPE_W - r - 2), r + 1, r, random(2) == 0);
  } else {                                   // orbit
    int cx = random(40, SHAPE_W - 40), cy = SHAPE_H / 2;
    int R = random(12, SHAPE_H / 2 - 2);
    addCircle(cx, cy, R, false);
    addCircle(cx, cy, 2, true);
    int sats = random(3, 7);
    for (int i = 0; i < sats; i++) {
      float a = random(0, 628) / 100.0;
      int sx = cx + (int)(cos(a) * R), sy = cy + (int)(sin(a) * R);
      addLine(cx, cy, sx, sy);
      addCircle(sx, sy, random(1, 4), true);
    }
    int far = random(1, 4);                  // a few distant dots
    for (int i = 0; i < far; i++) addCircle(random(3, SHAPE_W - 3), random(3, SHAPE_H - 3), 1, true);
  }

  own.kind = TYPE_SHAPES;
  own.len  = shapeCount * 5;
}

void buildOwn() {
  own.mailId = 0;
  own.from   = myId;
  if      (MODE == MODE_PICTURES) buildPicture();
  else if (MODE == MODE_SHAPES)   buildShapes();
  else                            buildPoem();
}


// =====================================================================
// --- memory between wakes ---
// =====================================================================

void loadState() {
  prefs.begin("mufloat", false);
  seq = prefs.getULong("seq", 0) + 1;
  prefs.putULong("seq", seq);
  lastSeen   = prefs.getULong("seen", 0);
  inboxCount = prefs.getInt("inboxN", 0);
  if (prefs.getBytesLength("inbox") == sizeof(inbox)) prefs.getBytes("inbox", inbox, sizeof(inbox));
  else inboxCount = 0;
  prefs.end();
  inboxCount = constrain(inboxCount, 0, INBOX_SIZE);
  for (int i = 0; i < INBOX_SIZE; i++) isNew[i] = false;
}

void saveInbox() {
  prefs.begin("mufloat", false);
  prefs.putULong("seen", lastSeen);
  prefs.putInt("inboxN", inboxCount);
  prefs.putBytes("inbox", inbox, sizeof(inbox));
  prefs.end();
}

// Put what we fetched this wake at the top of the inbox, newest first.
void mergeFetched() {
  // sort oldest -> newest, then push each one onto the top
  for (int i = 0; i < fetchedCount; i++)
    for (int j = i + 1; j < fetchedCount; j++)
      if (fetched[j].mailId < fetched[i].mailId) { Item t = fetched[i]; fetched[i] = fetched[j]; fetched[j] = t; }

  for (int f = 0; f < fetchedCount; f++) {
    for (int i = INBOX_SIZE - 1; i > 0; i--) { inbox[i] = inbox[i - 1]; isNew[i] = isNew[i - 1]; }
    inbox[0] = fetched[f];
    isNew[0] = true;
    if (inboxCount < INBOX_SIZE) inboxCount++;
    if (fetched[f].mailId > lastSeen) lastSeen = fetched[f].mailId;
  }
}


// =====================================================================
// --- radio ---
// =====================================================================

bool alreadyFetched(long mailId) {
  for (int i = 0; i < fetchedCount; i++) if (fetched[i].mailId == mailId) return true;
  return false;
}

// Read whatever just arrived. Returns 1 for a relay to us, 2 for "end" to us,
// 0 for anything else.
int takePacket() {
  byte buf[MAX_PACKET + 1];
  int len   = radio.getPacketLength();
  int state = radio.readData(buf, sizeof(buf));
  radio.startReceive();

  if (state != RADIOLIB_ERR_NONE || len < HDR_BYTES || len > MAX_PACKET) return 0;
  if (buf[0] != MAGIC) return 0;
  byte type = buf[7];

  if (type == TYPE_END && len >= HDR_BYTES + 3 && get16(buf + 8) == myId) return 2;

  if (type == TYPE_RELAY && len >= RELAY_HDR && get16(buf + 8) == myId) {
    long mailId = get32(buf + 10);
    int  clen   = min(len - RELAY_HDR, MAX_CONTENT);
    if (!alreadyFetched(mailId) && fetchedCount < COUNT(fetched)) {
      Item& it = fetched[fetchedCount++];
      it.mailId = mailId;
      it.from   = get16(buf + 1);
      it.kind   = buf[18];
      it.len    = clen;
      memcpy(it.data, buf + RELAY_HDR, clen);
    }
    return 1;
  }
  return 0;
}

// Ask the receiver for news and listen. Returns true if it answered at all.
bool askReceiver() {
  byte buf[HDR_BYTES + 5];
  putHeader(buf, TYPE_REQUEST);
  put32(buf + 8, lastSeen);
  buf[12] = FETCH_MAX;
  radio.transmit(buf, sizeof(buf));
  radioFlag = false;                     // transmitting also fires the interrupt
  radio.startReceive();

  bool heard = false;
  unsigned long deadline = millis() + REPLY_TIMEOUT_MS;
  while ((long)(millis() - deadline) < 0) {
    if (!radioFlag) { delay(1); continue; }
    radioFlag = false;
    int got = takePacket();
    if (got == 2) return true;                       // receiver says: that's all
    if (got == 1) { heard = true; deadline = millis() + PACKET_GAP_MS; }
  }
  return heard;
}

void sendOwn() {
  byte buf[HDR_BYTES + MAX_CONTENT];
  byte type = own.kind;
  putHeader(buf, type);
  memcpy(buf + HDR_BYTES, own.data, own.len);
  for (int i = 0; i < TX_REPEATS; i++) {
    int st = radio.transmit(buf, HDR_BYTES + own.len);
    if (st == RADIOLIB_ERR_NONE) sentOk = true;
    if (i < TX_REPEATS - 1) delay(40);
  }
}

bool radioAsleep = false;               // put to sleep properly this wake?

void talkToReceiver() {
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, PREAMBLE, TCXO, false);
  if (state != RADIOLIB_ERR_NONE) return;
  radio.setDio1Action(onDio1);

  for (int t = 0; t < REQUEST_TRIES; t++) {
    if (askReceiver()) break;
  }

  // Save what we got right away, before the costly send and screen
  // refresh: if the energy runs out from here on, the messages are still
  // there to show next round.
  mergeFetched();
  if (fetchedCount > 0) saveInbox();

  if (energyOk()) {                       // don't start sending on a sagging cap
    delay(10);                            // give the receiver a moment to listen again
    sendOwn();
  }
  radio.sleep(false);                     // deepest radio sleep; it's set up fresh every wake
  radioAsleep = true;
}


// =====================================================================
// --- drawing ---
// =====================================================================

// Paint one 64x22 tile with its top at y0, as 2x2 pixel blocks (128x44).
void drawTile(const byte* tile, int len, int y0) {
  for (int by = 0; by < TILE_H; by++) {
    for (int bx = 0; bx < TILE_W; bx++) {
      int bit = by * TILE_W + bx;
      if (bit / 8 >= len) return;
      byte mask = 0x80 >> (bit % 8);
      if (tile[bit / 8] & mask) display.fillRect(bx * 2, y0 + by * 2, 2, 2, BLACK);
    }
  }
}

// Word-wrap text into a box of width w and height h, top at y0.
void drawText(const char* txt, int len, int y0, int h, int w) {
  display.setTextColor(BLACK);
  display.setTextSize(1);
  const int CW = 6, LH = 8;
  int cols = w / CW;
  int x = 2, y = y0 + 1, col = 0;
  for (int i = 0; i < len; i++) {
    char c = txt[i];
    if (c == '\n' || col >= cols) { col = 0; x = 2; y += LH; }
    if (y + LH > y0 + h) break;
    if (c == '\n') continue;
    display.setCursor(x, y);
    display.print(c);
    x += CW; col++;
  }
}

// Same as the receiver: circles and lines, kept inside the box.
void drawShapes(const byte* p, int len, int y0, int h, int w) {
  for (int i = 0; i + 5 <= len; i += 5) {
    byte op = p[i];
    int a = p[i + 1], b = p[i + 2], c = p[i + 3], d = p[i + 4];
    if (op == SHAPE_LINE) {
      a = min(a, w - 1); c = min(c, w - 1);
      b = min(b, h - 1); d = min(d, h - 1);
      display.drawLine(a, y0 + b, c, y0 + d, BLACK);
    } else if (op == SHAPE_CIRCLE || op == SHAPE_DISC) {
      if (a >= w || b >= h) continue;
      int r = min(min(c, a), min(b, min(w - 1 - a, h - 1 - b)));
      if (op == SHAPE_DISC) display.fillCircle(a, y0 + b, r, BLACK);
      else                  display.drawCircle(a, y0 + b, r, BLACK);
    }
  }
}

// textH: room for text (less than h when a tag sits underneath).
void drawItem(const Item& it, int y0, int h, int textH, int w) {
  if      (it.kind == TYPE_TILE)   drawTile(it.data, it.len, y0);
  else if (it.kind == TYPE_TEXT)   drawText((const char*)it.data, it.len, y0, textH, w);
  else if (it.kind == TYPE_SHAPES) drawShapes(it.data, it.len, y0, min(SHAPE_H, h), w);
}

// Black bar with white text, one line per string.
void drawBar(int y0, int h, const char* line1, const char* line2) {
  display.fillRect(0, y0, display.width(), h, BLACK);
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(3, y0 + 1);
  display.print(line1);
  if (line2) {
    display.setCursor(3, y0 + 10);
    display.print(line2);
  }
}

// Small white tag with a border, bottom-left of a received message.
void drawTag(int y0, const Item& it, bool fresh) {
  char tag[24];
  snprintf(tag, sizeof(tag), "%sfrom float %04X", fresh ? "NEW " : "", it.from);
  int tw = 6 * (int)strlen(tag) + 5;
  display.fillRect(0, y0, tw, TAG_H, WHITE);
  display.drawRect(0, y0, tw, TAG_H, BLACK);
  display.setTextColor(BLACK);
  display.setCursor(3, y0 + 1);
  display.print(tag);
}

void drawScreen() {
  display.portrait();                   // 128 wide x 296 tall
  display.fullscreen();
  // Full refresh after a power loss (no saved image to compare with), and
  // every FULL_REFRESH_EVERY wakes to clear the faint ghosting fast ones leave.
  bool full = !haveFrame ||
              ((FULL_REFRESH_EVERY > 0) && ((seq - 1) % FULL_REFRESH_EVERY == 0));
  if (full) {
    display.fastmodeOff();
  } else {
    display.fastmodeOn(false);
    display.restoreFrame(lastFrame);
  }

  const int W = display.width();

  DRAW (display) {
    // --- our own message ---
    drawBar(0, BAR_H, sentOk ? LABEL_SENT : LABEL_NOT_SENT, nullptr);
    drawItem(own, OWN_Y, ITEM_H, ITEM_H, W);

    // --- what we got from the others (nothing at all until there is some) ---
    if (inboxCount > 0) drawBar(RECV_BAR_Y, RECV_BAR_H, LABEL_RECEIVED_1, LABEL_RECEIVED_2);

    for (int i = 0; i < inboxCount; i++) {
      int y0 = RECV_Y + i * RECV_STEP;
      drawItem(inbox[i], y0, ITEM_H, ITEM_H - TAG_H, W);
      drawTag(y0 + ITEM_H - TAG_H, inbox[i], isNew[i]);
      if (i < INBOX_SIZE - 1) {                         // dotted divider
        for (int x = 0; x < W; x += 4) display.drawPixel(x, y0 + ITEM_H + 1, BLACK);
      }
    }
  }

  display.saveFrame(lastFrame);
  haveFrame = true;
}


// =====================================================================

// Not enough energy: sleep until V_OK goes high. The long safety timer is
// only there in case the pin signal is missed.
const long SAFETY_WAKE_SECONDS = 600;

// Give the e-ink a moment after its refresh before cutting its power.
const int EINK_OFF_DELAY_MS = 50;

// Is there (still) enough energy to carry on?
bool energyOk() {
  return !USE_VOK || digitalRead(VOK_PIN) == HIGH;
}

// Put the radio to sleep without setting it up first (it starts out awake
// in standby after a power-up, drawing ~0.6 mA). Same trick the e-ink
// library uses on Heltec's Wireless Paper.
void radioSleepRaw() {
  pinMode(LORA_BUSY, INPUT);
  unsigned long t0 = millis();
  while (digitalRead(LORA_BUSY) == HIGH && millis() - t0 < 20) delay(1);
  pinMode(LORA_NSS, OUTPUT);
  pinMode(LORA_SCK, OUTPUT);
  pinMode(LORA_MOSI, OUTPUT);
  digitalWrite(LORA_NSS, HIGH);
  digitalWrite(LORA_SCK, LOW);
  digitalWrite(LORA_NSS, LOW);
  shiftOut(LORA_MOSI, LORA_SCK, MSBFIRST, 0x84);   // SetSleep
  shiftOut(LORA_MOSI, LORA_SCK, MSBFIRST, 0x00);   // cold start: lowest current
  digitalWrite(LORA_NSS, HIGH);
}

bool screenDrawn = false;

// Switch off everything that could keep drawing current while we sleep.
void powerDown() {
  if (!radioAsleep) radioSleepRaw();

  // Radio chip-select must stay high, or the radio can wake up.
  pinMode(LORA_NSS, OUTPUT);
  digitalWrite(LORA_NSS, HIGH);
  gpio_hold_en((gpio_num_t)LORA_NSS);

  // Cut power to the e-ink (the board's Vext switch). The image stays.
  if (screenDrawn) delay(EINK_OFF_DELAY_MS);
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  gpio_hold_en((gpio_num_t)VEXT_PIN);

  gpio_deep_sleep_hold_en();
}

void sleepUntilEnergy() {
  powerDown();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)VOK_PIN, 1);
  esp_sleep_enable_timer_wakeup(SAFETY_WAKE_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}

// Done for now: rest a while before the next round.
void sleepAndRest() {
  powerDown();
  long s = REST_SECONDS + random(0, REST_JITTER_SECONDS + 1);
  esp_sleep_enable_timer_wakeup(s * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  setCpuFrequencyMhz(80);               // plenty for this, and saves energy

  // Release the pins we held during sleep, so the radio and e-ink can use them.
  gpio_hold_dis((gpio_num_t)LORA_NSS);
  gpio_hold_dis((gpio_num_t)VEXT_PIN);

  // After a deep-sleep wake the radio is still asleep from last time. Only
  // after a fresh power-up is it awake in standby.
  radioAsleep = (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED);

  if (USE_VOK) {
    rtc_gpio_deinit((gpio_num_t)VOK_PIN);  // hand the pin back after a V_OK wake-up
    pinMode(VOK_PIN, INPUT);
    if (!energyOk()) sleepUntilEnergy();
  }

  if (DEBUG_SERIAL) Serial.begin(115200);
  myId = (int)((ESP.getEfuseMac() >> 32) & 0xFFFF);   // last 2 bytes of the MAC: different on every board (the first bytes are the maker's, the same on all)

  randomSeed(esp_random());
  loadState();
  buildOwn();

  delay(QUIET_MS);
  radioAsleep = false;                  // talkToReceiver wakes it
  talkToReceiver();

  if (DEBUG_SERIAL) Serial.printf("float %04X wake %ld: sent %s, got %d new\n",
                                  myId, seq, sentOk ? "ok" : "FAILED", fetchedCount);

  // The screen refresh is the biggest cost of a round. If the cap has sagged
  // below V_OK by now, skip it: what we received is saved and shows next time.
  if (!energyOk()) sleepUntilEnergy();

  drawScreen();
  screenDrawn = true;
  sleepAndRest();
}


void loop() {}
