/*
 * E-ink test - Heltec Vision Master E290.
 *
 * Writes a line of text to the e-ink and keeps moving it to a random spot,
 * so you can see the screen and the library work. No sleep, no V_OK check,
 * no radio - just plug in USB.
 *
 * Board: Heltec Vision Master E290
 * Libraries: heltec-eink-modules
 */

#include "heltec-eink-modules.h"


const char* MESSAGE   = "hello e-ink";
const int   TEXT_SIZE = 2;      // 1 = small, each step up doubles the letters
const int   MOVE_MS   = 1000;   // wait between moves
const int   FULL_EVERY = 20;    // full refresh every N moves to wipe ghosting


EInkDisplay_VisionMasterE290 display;
int moves = 0;


void setup() {
  display.landscape();   // 296 wide x 128 tall
}


void loop() {
  // Fast (partial) refresh for moving, a full refresh now and then.
  if (moves % FULL_EVERY == 0) display.fastmodeOff();
  else                         display.fastmodeOn();

  // Default font: 6 x 8 px per letter at size 1.
  int textW = strlen(MESSAGE) * 6 * TEXT_SIZE;
  int textH = 8 * TEXT_SIZE;
  int x = random(0, display.width()  - textW);
  int y = random(0, display.height() - textH);

  DRAW (display) {
    display.fillScreen(WHITE);
    display.setTextColor(BLACK);
    display.setTextSize(TEXT_SIZE);
    display.setCursor(x, y);
    display.print(MESSAGE);
  }

  moves++;
  delay(MOVE_MS);
}
