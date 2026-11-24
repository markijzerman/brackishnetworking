/*
 * Receiver (portrait feed, mailbox) - Heltec Vision Master E290, USB powered.
 *
 * The screen (128 x 296, portrait) is split top-to-bottom into 6 bands, each
 * 128 x ~49 px, showing the last 6 messages like a feed: the newest always on
 * top, older ones below it, newest to oldest. A new message pushes everything
 * down one band and the oldest drops off the bottom. Order is the order the
 * receiver heard them in, regardless of each float's seq.
 *
 * Each message sits in its own box with a label tab like "4 mins ago"; the
 * newest one is drawn inverted (white on black). The receiver keeps
 * time with its own clock (time since it was switched on, no internet or
 * real-time clock needed) and redraws the labels once a minute. After an
 * hour the label counts in hours, so it changes less often.
 *
 * Three kinds of message, told apart by a 1-byte type field:
 *   type 0 (tile):   a 64x22 one-bit drawing, painted as 2x2 blocks (128x44 px)
 *   type 1 (text):   characters, word-wrapped into the band
 *   type 2 (shapes): a list of circles and lines (see drawShapes below)
 *
 * Mailbox: the receiver also remembers the last MAILBOX_SIZE messages. A
 * float (see MU_float) can ask "anything new since message #N?" and the
 * receiver sends back up to MAX_RELAY of the newest ones, skipping the
 * asking float's own messages. Older floats that only send keep working.
 *
 * E-ink: the receiver is USB powered, so by default every update is a full
 * (clean, no ghosting) refresh. Set ALWAYS_FULL_REFRESH = false for fast
 * partial refreshes, with a full one at least every FULL_REFRESH_MINUTES.
 *
 * Every message is shown, including repeats: if a float sends the same
 * thing twice, it appears twice.
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"


// --- settings ---

const int  BANDS                = 6;
const int  MAILBOX_SIZE         = 24;    // messages kept for floats to fetch (in memory, lost on reboot)
const int  MAX_RELAY            = 4;     // most messages handed to one float per request
const long RENDER_QUIET_MS      = 1000;  // wait until the radio has been quiet this long before redrawing
const bool ALWAYS_FULL_REFRESH  = true;  // every update is a clean full refresh (screen flashes, ~2 s)
const long FULL_REFRESH_MINUTES = 30;    // if not: full refresh at least this often while the screen changes
const int  REPLY_DELAY_MS       = 10;    // pause before answering, so the float is listening
const int  REPLY_GAP_MS         = 10;    // pause between answer packets

// Must match the float.
const int TILE_W     = 64;
const int TILE_H     = 22;
const int TILE_BYTES = (TILE_W * TILE_H + 7) / 8;   // 176
const int SHAPE_H    = 44;                          // shapes are drawn in 128 x 44, like a tile

// Radio - must match the floats.
//   FAST_RADIO = true:  MU_float exhibition setting. Short range (floats in
//                       buckets near the receiver), packets ~10x shorter on
//                       air, which saves the floats a lot of energy.
//   FAST_RADIO = false: the original long-range setting the workshop floats
//                       (float_tile, float_generativepoetry, ...) use.
const bool  FAST_RADIO = true;
const float FREQ     = FAST_RADIO ? 868.3 : 868.0;   // 868.3 keeps the 500 kHz channel inside 868.0-868.6
const float BW       = FAST_RADIO ? 500.0 : 125.0;
const byte  SF       = FAST_RADIO ? 6 : 7;
const byte  CR       = FAST_RADIO ? 5 : 7;
const int   PREAMBLE = FAST_RADIO ? 12 : 8;          // SF6 needs at least 12
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
//   unsigned long = whole number that is never negative, used for millis()

// Wire format: must match the float.
//   byte 0:    magic (0xA5)
//   byte 1..2: nodeId (16-bit)
//   byte 3..6: seq    (32-bit)
//   byte 7:    type
//   byte 8+:   payload
//
// Types 0..2 are messages (payload = the content).
// Types 3..5 are the mailbox conversation between a float and this receiver:
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
const int MAX_CONTENT = 200;
const int MAX_PACKET  = 255;     // LoRa limit
const int LABEL_H     = 11;      // height of the "4 mins ago" tab at the bottom of a band

// Shape ops, 5 bytes each: op, a, b, c, d
const byte SHAPE_LINE   = 0;     // line   (a,b) -> (c,d)
const byte SHAPE_CIRCLE = 1;     // circle centre (a,b), radius c
const byte SHAPE_DISC   = 2;     // filled circle centre (a,b), radius c

SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

// The e-ink, changed so the receiver keeps listening while the screen
// refreshes. A full refresh takes 2-3 s and the display library normally
// just waits it out; floats asking for messages in that time would get no
// answer. Here, while it waits, we handle whatever the radio caught. (The
// display and the radio are on separate SPI buses, so this is safe.)
void serviceRadio();
class ReceiverDisplay : public EInkDisplay_VisionMasterE290 {
protected:
  void wait() override {
    while (digitalRead(pin_busy) == HIGH) {   // HIGH while the display is busy
      serviceRadio();
      yield();
    }
  }
};

ReceiverDisplay display;
Preferences prefs;

// One remembered message. (Defined before any function, so the Arduino IDE
// can use it in the function list it builds behind the scenes.)
struct Mail {
  long          id;              // receiver's running count; 0 = empty slot
  int           from;            // float's nodeId
  long          seq;             // float's own counter
  byte          kind;            // TYPE_TILE / TYPE_TEXT / TYPE_SHAPES
  int           len;
  byte          data[MAX_CONTENT];
  unsigned long rxMs;            // millis() when it arrived
};
Mail mailbox[MAILBOX_SIZE];

// IRAM_ATTR puts this function in RAM so the radio can call it from inside
// an interrupt - interrupt handlers can't live in flash. Just a flag flip.
volatile bool rxFlag = false;
void IRAM_ATTR onDio1() { rxFlag = true; }

long total = 0;                  // messages received ever (persisted); also the newest mail id

bool needRender = false;
bool needFull   = false;
unsigned long lastPacketMs = 0;
unsigned long lastTickMs   = 0;
unsigned long lastFullMs   = 0;
int  fastSinceFull = 0;
bool staleRx = false;            // a packet arrived while the screen was being drawn
String drawnLabel[BANDS];        // what each band's label says right now


// --- little helpers for reading / writing numbers inside packets ---

void put16(byte* p, long v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(byte* p, long v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
int  get16(const byte* p)   { return p[0] | (p[1] << 8); }
long get32(const byte* p)   { return (long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24); }

void putHeader(byte* buf, int nodeId, long seq, byte type) {
  buf[0] = MAGIC;
  put16(buf + 1, nodeId);
  put32(buf + 3, seq);
  buf[7] = type;
}


// --- mailbox ---

Mail* findMail(long id) {
  if (id < 1) return nullptr;
  Mail& m = mailbox[(id - 1) % MAILBOX_SIZE];
  return (m.id == id) ? &m : nullptr;
}

// The message shown in a band: band 0 (top) is the newest, band 1 the one
// before it, and so on down.
Mail* bandMail(int slot) {
  return findMail(total - slot);
}

// "just now", "1 min ago", "4 mins ago", "1 hour ago", "3 hours ago", "2 days ago"
String ageLabel(unsigned long rxMs) {
  unsigned long mins = (millis() - rxMs) / 60000UL;
  if (mins < 1)   return "just now";
  if (mins < 60)  return String(mins) + (mins == 1 ? " min ago" : " mins ago");
  unsigned long hours = mins / 60;
  if (hours < 48) return String(hours) + (hours == 1 ? " hour ago" : " hours ago");
  return String(hours / 24) + " days ago";
}

bool labelsChanged() {
  for (int s = 0; s < BANDS; s++) {
    Mail* m = bandMail(s);
    String now = m ? ageLabel(m->rxMs) : "";
    if (now != drawnLabel[s]) return true;
  }
  return false;
}


// --- band rendering ---

// Each band is a box with a one-pixel border and a blank row under it, so
// neighbouring messages are clearly apart. The time label is a small tab in
// the box's bottom-left corner. The newest message is drawn inverted
// (white on black). fg = the colour to draw in.

// Paint one 64x22 tile with its top at y0. Each "block" of the tile becomes
// a 2x2 pixel square, so the whole tile covers 128x44.
void drawTile(const byte* tile, int len, int y0, uint16_t fg) {
  for (int by = 0; by < TILE_H; by++) {
    for (int bx = 0; bx < TILE_W; bx++) {
      int bit = by * TILE_W + bx;
      if (bit / 8 >= len) return;                   // guard against short packets
      // Pick the bit for this block out of the packed byte array. Each
      // byte holds 8 blocks, highest bit first.
      byte mask = 0x80 >> (bit % 8);
      if (tile[bit / 8] & mask) {
        display.fillRect(bx * 2, y0 + by * 2, 2, 2, fg);
      }
    }
  }
}

// Word-wrap text into a box of width w and height h, top at y0.
void drawText(const char* txt, int len, int y0, int h, int w, uint16_t fg) {
  display.setTextColor(fg);
  display.setTextSize(1);
  const int CW = 6, LH = 8;             // 6x8 default font
  int cols = w / CW;                    // ~21 chars per line at w = 128
  int x = 2, y = y0 + 1, col = 0;
  for (int i = 0; i < len; i++) {
    char c = txt[i];
    if (c == '\n' || col >= cols) { col = 0; x = 2; y += LH; }
    if (y + LH > y0 + h) break;         // ran out of room
    if (c == '\n') continue;
    display.setCursor(x, y);
    display.print(c);
    x += CW; col++;
  }
}

// Draw a list of circles and lines inside a w x h box, top at y0. Anything
// that would poke out of the box is pulled back in, so a bad packet can't
// scribble over the neighbouring bands.
void drawShapes(const byte* p, int len, int y0, int h, int w, uint16_t fg) {
  for (int i = 0; i + 5 <= len; i += 5) {
    byte op = p[i];
    int a = p[i + 1], b = p[i + 2], c = p[i + 3], d = p[i + 4];
    if (op == SHAPE_LINE) {
      a = min(a, w - 1); c = min(c, w - 1);
      b = min(b, h - 1); d = min(d, h - 1);
      display.drawLine(a, y0 + b, c, y0 + d, fg);
    } else if (op == SHAPE_CIRCLE || op == SHAPE_DISC) {
      if (a >= w || b >= h) continue;
      int r = min(min(c, a), min(b, min(w - 1 - a, h - 1 - b)));
      if (op == SHAPE_DISC) display.fillCircle(a, y0 + b, r, fg);
      else                  display.drawCircle(a, y0 + b, r, fg);
    }
  }
}

// One message in its box. y0 / bh = the band's top and height.
void drawBand(const Mail& m, int y0, int bh, int w, const String& lbl, bool newest) {
  uint16_t fg = newest ? WHITE : BLACK;
  uint16_t bg = newest ? BLACK : WHITE;
  int boxH   = bh - 1;                     // leave a blank row under the box
  int innerY = y0 + 1;                     // inside the border
  int innerH = boxH - 2;
  int tabY   = y0 + boxH - LABEL_H;        // label tab shares the box's bottom edge

  if (newest) display.fillRect(0, y0, w, boxH, BLACK);

  if      (m.kind == TYPE_TILE)   drawTile(m.data, m.len, innerY, fg);
  else if (m.kind == TYPE_TEXT)   drawText((const char*)m.data, m.len, innerY, tabY - innerY, w, fg);
  else if (m.kind == TYPE_SHAPES) drawShapes(m.data, m.len, innerY, min(SHAPE_H, innerH), w, fg);

  // time label tab, bottom-left, joined to the border
  int tw = 6 * (int)lbl.length() + 6;
  display.fillRect(0, tabY, tw, LABEL_H, bg);
  display.drawRect(0, tabY, tw, LABEL_H, fg);
  display.setTextColor(fg);
  display.setTextSize(1);
  display.setCursor(3, tabY + 2);
  display.print(lbl);

  display.drawRect(0, y0, w, boxH, BLACK); // the border, last so nothing covers it
}

void drawWaiting() {
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(6, 8);
  display.print("waiting");
  display.setCursor(6, 22);
  display.print("for a float");
  display.setCursor(6, display.height() - 16);
  display.print("total ");
  display.print(total);
}

// Redraw the whole screen from the mailbox. full = slow, clean refresh;
// otherwise a fast partial refresh (only changed pixels flip).
void render(bool full) {
  display.portrait();
  display.fullscreen();
  if (full) display.fastmodeOff();
  else      display.fastmodeOn(false);

  const int W = display.width();        // 128
  const int H = display.height();       // 296

  // Work out labels before drawing: DRAW may run its body more than once,
  // and all passes must draw exactly the same thing.
  Mail* shown[BANDS];
  bool any = false;
  for (int s = 0; s < BANDS; s++) {
    shown[s] = bandMail(s);
    drawnLabel[s] = shown[s] ? ageLabel(shown[s]->rxMs) : "";
    if (shown[s]) any = true;
  }

  DRAW (display) {
    if (!any) drawWaiting();
    for (int s = 0; s < BANDS; s++) {
      if (!shown[s]) continue;
      int y0 = (s * H) / BANDS;
      int bh = (((s + 1) * H) / BANDS) - y0;
      drawBand(*shown[s], y0, bh, W, drawnLabel[s], shown[s]->id == total);
    }
  }

  if (full) { lastFullMs = millis(); fastSinceFull = 0; }
  else      fastSinceFull++;

  // The radio is handled while the display refreshes (see ReceiverDisplay),
  // but not while the image is being drawn and sent to it (~0.2 s). Anything
  // still waiting now arrived during that time: a request may already be
  // too old for the float to still be listening.
  staleRx = rxFlag;
}


// --- radio ---

void sendPacket(byte* buf, int n) {
  radio.transmit(buf, n);
}

// A float asked what's new. Answer with up to `want` of the newest messages
// it hasn't seen (never its own), newest first, then an "end" packet.
void answerRequest(int from, const byte* p, int plen) {
  long lastSeen = (plen >= 4) ? get32(p) : 0;
  int  want     = (plen >= 5) ? p[4] : MAX_RELAY;
  if (want > MAX_RELAY) want = MAX_RELAY;
  if (lastSeen > total) lastSeen = 0;    // receiver was reset since the float last asked

  delay(REPLY_DELAY_MS);

  byte buf[MAX_PACKET];
  int sent = 0;
  for (long id = total; id >= 1 && id > total - MAILBOX_SIZE && sent < want; id--) {
    if (id <= lastSeen) break;
    Mail* m = findMail(id);
    if (!m || m->from == from) continue;

    putHeader(buf, m->from, m->seq, TYPE_RELAY);
    put16(buf + 8,  from);
    put32(buf + 10, m->id);
    put32(buf + 14, (millis() - m->rxMs) / 1000UL);
    buf[18] = m->kind;
    memcpy(buf + 19, m->data, m->len);
    sendPacket(buf, 19 + m->len);
    sent++;
    delay(REPLY_GAP_MS);
  }

  putHeader(buf, 0, total, TYPE_END);
  put16(buf + 8, from);
  buf[10] = sent;
  sendPacket(buf, 11);

  rxFlag = false;          // transmitting also fires the DIO1 interrupt
  radio.startReceive();

  Serial.printf("request from %04X (seen #%ld): sent %d\n", from, lastSeen, sent);
}

void storeMessage(int nodeId, long seq, byte type, const byte* payload, int plen, float rssi) {
  if (plen > MAX_CONTENT) plen = MAX_CONTENT;

  total++;
  prefs.begin("rx", false);
  prefs.putULong("total", total);
  prefs.end();

  Mail& m = mailbox[(total - 1) % MAILBOX_SIZE];
  m.id   = total;
  m.from = nodeId;
  m.seq  = seq;
  m.kind = type;
  m.len  = plen;
  m.rxMs = millis();
  memcpy(m.data, payload, plen);

  // One line per message on Serial, for tools/feed.html:
  //   msg: <text>          text messages
  //   tile: <hex bytes>    drawings
  //   shapes: <hex bytes>  circles and lines
  if (type == TYPE_TEXT) {
    Serial.print("msg: ");
    for (int i = 0; i < plen; i++) {
      char c = (char)payload[i];
      Serial.print(c == '\n' ? ' ' : c);   // keep it on one line
    }
    Serial.println();
  } else {
    Serial.print(type == TYPE_SHAPES ? "shapes: " : "tile: ");
    for (int i = 0; i < plen; i++) Serial.printf("%02X", payload[i]);
    Serial.println();
  }

  Serial.printf("#%ld  from %04X  seq %ld  %d dBm  type %d  %d B\n",
                total, nodeId, seq, (int)rssi, type, plen);

  needRender = true;
}

void handlePacket() {
  byte buf[MAX_PACKET + 1];
  int len   = radio.getPacketLength();
  int state = radio.readData(buf, sizeof(buf));
  float rssi = radio.getRSSI();
  radio.startReceive();

  if (state != RADIOLIB_ERR_NONE || len < HDR_BYTES || len > MAX_PACKET) return;

  // Pull the header fields out of the first 8 bytes.
  if (buf[0] != MAGIC) return;
  int  nodeId = get16(buf + 1);
  long seq    = get32(buf + 3);
  byte type   = buf[7];
  int  plen   = len - HDR_BYTES;

  lastPacketMs = millis();
  bool stale = staleRx;
  staleRx = false;

  if (type == TYPE_TILE || type == TYPE_TEXT || type == TYPE_SHAPES) {
    storeMessage(nodeId, seq, type, buf + HDR_BYTES, plen, rssi);
  } else if (type == TYPE_REQUEST) {
    // Floats only listen for a moment, so don't answer a request that
    // waited while the screen was being drawn - nobody is listening.
    if (stale) Serial.printf("request from %04X came in while drawing: skipped\n", nodeId);
    else       answerRequest(nodeId, buf + HDR_BYTES, plen);
  }
  // anything else (relay / end from another receiver, unknown types): ignore
}

// Handle a packet if one came in. Called from loop() and while the screen refreshes.
void serviceRadio() {
  if (rxFlag) {
    rxFlag = false;
    handlePacket();
  }
}


void setup() {
  Serial.begin(115200);

  prefs.begin("rx", false);
  total = prefs.getULong("total", 0);
  prefs.end();

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, PREAMBLE, TCXO, false);
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

  render(true);                 // "waiting for a float"
  lastTickMs = millis();
}


void loop() {
  serviceRadio();

  unsigned long now = millis();

  // Once a minute: have any "... ago" labels changed?
  if (now - lastTickMs >= 60000UL) {
    lastTickMs = now;
    if (labelsChanged()) needRender = true;
    if (fastSinceFull > 0 && now - lastFullMs >= FULL_REFRESH_MINUTES * 60000UL) {
      needRender = true;
      needFull   = true;
    }
  }

  // Redraw only once the radio has gone quiet, so a float that is in the
  // middle of asking for messages gets its answer first.
  if (needRender && now - lastPacketMs >= RENDER_QUIET_MS) {
    render(needFull || ALWAYS_FULL_REFRESH);
    needRender = false;
    needFull   = false;
  }
}
