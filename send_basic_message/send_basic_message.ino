/*
 * Send basic message - Heltec Vision Master E290.
 *
 * Sends one fixed message over LoRa the moment the board gets power, and
 * again every SEND_SECONDS for as long as it stays powered. No V_OK check,
 * no deep sleep, so it works the same on USB and on a harvester (e.g. a
 * hand-cranked stepper through the BQ25570): power in -> message out.
 *
 * Kept frugal for harvested power:
 *   - CPU runs at 80 MHz instead of 240 MHz
 *   - radio goes to sleep right after each send
 *   - the e-ink is ONLY touched after a message was sent successfully,
 *     and then only its top band, with a fast (partial) refresh
 *
 * Change what it says by editing MESSAGE below.
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"


// What gets sent. Short string, up to about 21 chars x 5 lines in the
// receiver's band.
const char* MESSAGE = "hello from the water";


// --- settings ---

const long SEND_SECONDS = 30;    // while powered, send again every N seconds
const int  TX_REPEATS   = 1;     // copies sent per message

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
//   long          = larger whole number, used for the send counter

// Wire format: one LoRa packet made of a small header + the message bytes.
//   byte 0:    magic (0xA5), lets the receiver spot our packets
//   byte 1..2: nodeId (16-bit), unique per board
//   byte 3..6: seq    (32-bit), counts sends
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
bool radioOk = false;


// Load the send counter from flash, add 1, write it back. Flash survives
// losing power, so the count keeps going across crank sessions.
long loadAndIncSeq() {
  prefs.begin("float", false);
  long s = prefs.getULong("seq", 0) + 1;
  prefs.putULong("seq", s);
  prefs.end();
  return s;
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


// Send the message. Returns true if every copy went out.
bool sendMessage() {
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);   // reclaim bus from e-ink
  byte buf[HDR_BYTES + MAX_PAYLOAD];
  int n = buildPacket(buf);
  bool ok = true;
  for (int i = 0; i < TX_REPEATS; i++) {
    if (radio.transmit(buf, n) != RADIOLIB_ERR_NONE) ok = false;
    radio.finishTransmit();
    if (i < TX_REPEATS - 1) delay(40);
  }
  radio.sleep();                 // otherwise the radio idles at a few mA
  return ok;
}


// Draw the message and send count in our own band (top 1/6) of the
// e-ink, with a fast partial refresh. The rest of the screen is untouched.
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
    display.setCursor(3, bh - 10);
    display.print("sent #");
    display.print(seq);
  }
}


void setup() {
  setCpuFrequencyMhz(80);        // plenty for this, uses less power than 240
  Serial.begin(115200);
  myId = (int)(ESP.getEfuseMac() & 0xFFFF);

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, 8, TCXO, false);
  radioOk = (state == RADIOLIB_ERR_NONE);
  if (!radioOk) Serial.printf("radio init failed: %d\n", state);
}


void loop() {
  if (radioOk) {
    seq = loadAndIncSeq();
    if (sendMessage()) {
      Serial.printf("sent #%ld: %s\n", seq, MESSAGE);
      showLocal();               // screen only after a successful send
    } else {
      Serial.printf("send failed #%ld\n", seq);
    }
  }

  delay(SEND_SECONDS * 1000);
}
