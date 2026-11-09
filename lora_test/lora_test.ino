/*
 * LoRa test - Heltec Vision Master E290.
 *
 * Sends a different short text message every few seconds, in the same
 * packet format as the floats, so the receiver shows it in its feed.
 * Each send adds a "message sent" line to this board's own e-ink, one
 * under the other; when the screen is full it wipes and starts at the top.
 * No sleep, no V_OK check - just plug in USB. The Serial Monitor (115200)
 * shows the same.
 *
 * Board: Heltec Vision Master E290
 * Libraries: RadioLib, heltec-eink-modules
 */

#include <RadioLib.h>
#include <SPI.h>
#include "heltec-eink-modules.h"


// Messages to cycle through. Each send also gets its number in front.
const char* MESSAGES[] = {
  "hello receiver",
  "testing testing",
  "is this thing on",
  "still here",
  "one more",
};
const int MESSAGE_COUNT = 5;

const int SEND_EVERY_MS = 10000;   // wait between messages


// Radio - must match the receiver, or nothing is heard.
const float FREQ  = 868.0;  // MHz (use your region's band)
const float BW    = 125.0;  // kHz
const byte  SF    = 7;      // spreading factor
const byte  CR    = 7;
const byte  SYNC  = 0x12;   // private network
const int   POWER = 14;     // dBm (max 14 in EU)
const float TCXO  = 1.8;
const byte  MAGIC = 0xA5;   // packet marker

// Wiring
#define LORA_NSS  8
#define LORA_DIO1 14
#define LORA_RST  12
#define LORA_BUSY 13
#define LORA_SCK  9
#define LORA_MISO 11
#define LORA_MOSI 10


// Wire format: same as the floats.
//   byte 0:    magic (0xA5)
//   byte 1..2: nodeId (16-bit)
//   byte 3..6: seq    (32-bit)
//   byte 7:    type (1 = text)
//   byte 8+:   payload
const byte TYPE_TEXT = 1;
const int  HDR_BYTES = 8;

const int LINE_H = 10;   // pixels per line on screen (8 px text + gap)

SPIClass loraSPI(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

EInkDisplay_VisionMasterE290 display;

int  myId;
long seq;
int  line = 0;   // which screen line the next message goes on


// Write one line of text under the previous one. When the screen is
// full, wipe it (one full refresh) and start at the top again.
void showLine(const char* text) {
  display.landscape();                       // 296 wide x 128 tall
  int lines = display.height() / LINE_H;     // 12 lines

  if (line >= lines) line = 0;
  if (line == 0) { display.fullscreen(); display.clear(); }
  display.fastmodeOn(false);

  display.setWindow(0, line * LINE_H, display.width(), LINE_H);
  DRAW (display) {
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.setCursor(3, display.window.top() + 1);
    display.print(text);
  }
  line++;
}


void setup() {
  Serial.begin(115200);
  myId = (int)(ESP.getEfuseMac() & 0xFFFF);

  // Random start, so after a reset the receiver doesn't mistake the first
  // message for a repeat of the last one it got.
  seq = random(100000);

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  int state = radio.begin(FREQ, BW, SF, CR, SYNC, POWER, 8, TCXO, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("radio init failed: %d\n", state);
    char err[32];
    snprintf(err, sizeof(err), "radio FAIL %d", state);
    showLine(err);
    while (true) delay(1000);
  }
  Serial.printf("radio ok, node %04X\n", myId);
}


void loop() {
  seq++;

  char text[64];
  snprintf(text, sizeof(text), "#%ld %s", seq, MESSAGES[seq % MESSAGE_COUNT]);

  byte buf[HDR_BYTES + sizeof(text)];
  buf[0] = MAGIC;
  buf[1] = (byte)(myId & 0xFF);
  buf[2] = (byte)((myId >> 8) & 0xFF);
  buf[3] = (byte)(seq & 0xFF);
  buf[4] = (byte)((seq >> 8) & 0xFF);
  buf[5] = (byte)((seq >> 16) & 0xFF);
  buf[6] = (byte)((seq >> 24) & 0xFF);
  buf[7] = TYPE_TEXT;
  int plen = strlen(text);
  memcpy(buf + HDR_BYTES, text, plen);

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);   // reclaim bus from e-ink
  int state = radio.transmit(buf, HDR_BYTES + plen);

  char status[48];
  if (state == RADIOLIB_ERR_NONE) snprintf(status, sizeof(status), "message sent #%ld", seq);
  else                            snprintf(status, sizeof(status), "send failed #%ld (%d)", seq, state);
  Serial.printf("%s: %s\n", status, text);
  showLine(status);

  delay(SEND_EVERY_MS);
}
