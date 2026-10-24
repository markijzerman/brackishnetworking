/*
 * Harvest float (generative poetry) - Heltec Vision Master E290.
 *
 * Wakes on a timer, checks the BQ25570 V_OK pin, and if there's enough
 * energy it builds a short sentence from word lists, sends it over LoRa
 * and draws it on its own band of the e-ink. Then sleeps.
 *
 * Edit the word lists and buildMessage() below to change what the float
 * can say.
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules, Preferences
 */

#include <RadioLib.h>
#include <SPI.h>
#include <Preferences.h>
#include "heltec-eink-modules.h"
#include "esp_sleep.h"


// MESSAGE is what the float sends and shows this wake. buildMessage()
// below decides what goes in it.
String MESSAGE;

// Word lists used by the default buildMessage() below. Add, remove, or
// change entries. Mind the receiver's band width: about 21 chars per
// line, around 5 lines.
//
// If you edit the lists, keep the matching _COUNT right next to each
// one updated too.

const char* WORD_SUBJECT[] = {
  "the float",  "the tide",  "the water", "the wind",
  "moss",      "salt",      "a fish",    "the shore",
  "the reeds", "the moon",  "the sky",   "a bird",
};
const int WORD_SUBJECT_COUNT = 12;

const char* WORD_VERB[] = {
  "drifts",    "waits",     "sings",     "sleeps",
  "listens",   "watches",   "breathes",  "hums",
  "remembers", "forgets",   "carries",   "counts",
};
const int WORD_VERB_COUNT = 12;

const char* WORD_ENDING[] = {
  "in the reeds",      "with the tide",    "under the moon",
  "against the shore", "through the salt", "into the dark",
  "toward morning",    "beside me",        "for hours",
  "without end",
};
const int WORD_ENDING_COUNT = 10;

// Called once per wake, right before we send. Picks one word from each
// list and joins them with spaces. Edit this to change the shape of the
// sentences, or to send a fixed line instead (just set MESSAGE = "...").
void buildMessage(int nodeId, long seq) {
  randomSeed(seq);
  int subject = random(WORD_SUBJECT_COUNT);
  int verb    = random(WORD_VERB_COUNT);
  int ending  = random(WORD_ENDING_COUNT);
  MESSAGE = String(WORD_SUBJECT[subject]) + " "
          + WORD_VERB[verb] + " "
          + WORD_ENDING[ending];
}


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
//   byte = 1 byte, holds 0..255
//   int  = whole number (ESP32: 32-bit)
//   long = larger whole number, used for the wake counter

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
  buf[7] = TYPE_TEXT;

  int plen = MESSAGE.length();
  if (plen > MAX_PAYLOAD) plen = MAX_PAYLOAD;
  memcpy(buf + HDR_BYTES, MESSAGE.c_str(), plen);
  return HDR_BYTES + plen;
}


void showLocal() {
  display.portrait();                   // 128 wide x 296 tall
  int bh = display.height() / 6;

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
  if (digitalRead(VOK_PIN) == LOW) sleepAndPoll();

  seq = loadAndIncSeq();
  buildMessage(myId, seq);

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
