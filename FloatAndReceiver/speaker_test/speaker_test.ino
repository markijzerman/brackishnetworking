/*
 * speaker_test.ino
 *
 * Plays a short melody on boot and a chirp every few seconds, straight
 * out of an ESP32 GPIO using tone(). No amp, no DAC, no I2S.
 *
 * ---------------------------------------------------------------------
 * WIRING
 * ---------------------------------------------------------------------
 * Dynamic speaker (8-32 ohm, like a tiny salvaged phone speaker):
 *     SPEAKER_PIN --[ 220 ohm resistor ]-- speaker + -- speaker - -- GND
 *
 *   The resistor is REQUIRED. Without it an 8 ohm speaker tries to pull
 *   ~400 mA out of the pin; the pin can only source ~40 mA and it will
 *   damage the chip over time. 220 ohm keeps the pin current near 15 mA
 *   -- audio is quiet but clearly audible in a quiet room.
 *
 * Piezo buzzer / piezo element (high impedance, kilohms):
 *     SPEAKER_PIN -- piezo + -- piezo - -- GND
 *
 *   No resistor needed. Piezos are generally louder than a resistor-
 *   limited dynamic speaker and sound best in the 2-4 kHz range.
 *
 * ---------------------------------------------------------------------
 * PIN CHOICE
 * ---------------------------------------------------------------------
 * Change SPEAKER_PIN below. Almost any GPIO on the ESP32-S3 works, as
 * long as it isn't a USB pin (19, 20), a strapping pin (0, 3, 45, 46),
 * or already used by another peripheral on your board.
 */

#include <Arduino.h>

const int SPEAKER_PIN = 17;

// Pitch table (Hz). NOTE_ prefix because the Heltec board header already
// uses the bare names A4/A5 as analog-pin aliases.
const int REST     = 0;
const int NOTE_C4  = 262, NOTE_D4 = 294, NOTE_E4 = 330, NOTE_F4 = 349;
const int NOTE_G4  = 392, NOTE_A4 = 440, NOTE_B4 = 494;
const int NOTE_C5  = 523, NOTE_D5 = 587, NOTE_E5 = 659, NOTE_F5 = 698;
const int NOTE_G5  = 784, NOTE_A5 = 880;

struct Note { int freq; int ms; };

// A short rising "hello" melody.
Note MELODY[] = {
  {NOTE_C4, 120}, {NOTE_E4, 120}, {NOTE_G4, 120}, {NOTE_C5, 240},
  {REST,     80},
  {NOTE_G4, 120}, {NOTE_C5, 300},
};

void playNote(int freq, int ms) {
  if (freq == REST) noTone(SPEAKER_PIN);
  else              tone(SPEAKER_PIN, freq);
  delay(ms);
  noTone(SPEAKER_PIN);
  delay(20);                 // small gap so adjacent notes stay distinct
}

void setup() {
  Serial.begin(115200);
  Serial.println("speaker test");

  pinMode(SPEAKER_PIN, OUTPUT);

  // Play the startup melody once.
  for (auto& n : MELODY) playNote(n.freq, n.ms);
}

void loop() {
  // Chirp every 3 seconds so you know it's running.
  playNote(NOTE_A4, 80);
  playNote(NOTE_E5, 80);
  delay(3000);
}
