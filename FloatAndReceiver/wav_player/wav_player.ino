/*
 * wav_player.ino
 *
 * Plays a WAV file off the ESP32-S3's flash through a GPIO pin, with
 * no DAC and no amp. Uses LEDC PWM at ~40 kHz as a 1-bit DAC; the
 * speaker's own inductance low-pass-filters the carrier so what you
 * hear is the audio.
 *
 * ---------------------------------------------------------------------
 * HOW TO UPLOAD A WAV
 * ---------------------------------------------------------------------
 * 1. Install the Arduino IDE 2.x "LittleFS Upload" plugin once:
 *      https://github.com/earlephilhower/arduino-littlefs-upload
 *    (download the .vsix, drop into ~/.arduinoIDE/plugins/, restart.)
 *
 * 2. Make a folder called `data/` next to this .ino file. Put your
 *    WAV in there as `sound.wav`.
 *
 * 3. Convert the WAV first in Audacity (or ffmpeg) to something small
 *    and friendly for the little PWM "DAC":
 *      - 8-bit unsigned PCM
 *      - MONO
 *      - 8 kHz sample rate (nice and small; 16 kHz also works)
 *    ffmpeg one-liner:
 *      ffmpeg -i input.mp3 -ac 1 -ar 8000 -acodec pcm_u8 data/sound.wav
 *
 * 4. In Arduino IDE: Cmd+Shift+P -> "Upload LittleFS to ESP..."
 *    then flash this sketch the normal way.
 *
 * The sketch plays the file once on boot, then every 5 seconds.
 *
 * ---------------------------------------------------------------------
 * WIRING
 * ---------------------------------------------------------------------
 * Same as speaker_test:
 *   SPEAKER_PIN --[ 220 ohm ]-- speaker + -- speaker - -- GND
 * Piezo: skip the resistor.
 */

#include <Arduino.h>
#include <LittleFS.h>

const int  SPEAKER_PIN  = 17;
const int  PWM_FREQ_HZ  = 40000;   // PWM carrier -- above audible
const int  PWM_RES_BITS = 8;       // 0..255 duty
const char* WAV_PATH    = "/sound.wav";

// ---- WAV parsing ---------------------------------------------------------
// Minimal RIFF/WAVE reader: skips chunks until it finds "fmt " and "data".
// Supports 8-bit unsigned PCM and 16-bit signed PCM, mono or stereo
// (stereo: left channel only).

struct WavInfo {
  int sampleRate;
  int bitsPerSample;
  int numChannels;
  size_t dataBytes;
};

static bool openWav(fs::File& f, WavInfo& info) {
  char tag[5] = {0};
  uint32_t sz;

  if (f.read((uint8_t*)tag, 4) != 4 || strncmp(tag, "RIFF", 4) != 0) return false;
  f.read((uint8_t*)&sz, 4);
  if (f.read((uint8_t*)tag, 4) != 4 || strncmp(tag, "WAVE", 4) != 0) return false;

  bool haveFmt = false;
  while (f.available() >= 8) {
    f.read((uint8_t*)tag, 4);
    f.read((uint8_t*)&sz, 4);
    if (strncmp(tag, "fmt ", 4) == 0) {
      uint8_t fmt[16] = {0};
      size_t toRead = (sz < 16) ? sz : 16;
      f.read(fmt, toRead);
      info.numChannels   = fmt[2]  | (fmt[3]  << 8);
      info.sampleRate    = fmt[4]  | (fmt[5]  << 8) | (fmt[6] << 16) | (fmt[7] << 24);
      info.bitsPerSample = fmt[14] | (fmt[15] << 8);
      if (sz > 16) f.seek(f.position() + (sz - 16));
      haveFmt = true;
    } else if (strncmp(tag, "data", 4) == 0) {
      if (!haveFmt) return false;
      info.dataBytes = sz;
      return true;                                 // positioned at samples
    } else {
      f.seek(f.position() + sz);                   // skip unknown chunk
    }
  }
  return false;
}

// ---- playback ------------------------------------------------------------

void playWav(const char* path) {
  fs::File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.printf("couldn't open %s\n", path);
    return;
  }
  WavInfo w{};
  if (!openWav(f, w)) {
    Serial.println("not a WAV, or unsupported header");
    f.close();
    return;
  }
  Serial.printf("playing %s: %d Hz, %d-bit, %d-ch, %u bytes\n",
                path, w.sampleRate, w.bitsPerSample, w.numChannels,
                (unsigned)w.dataBytes);
  if ((w.bitsPerSample != 8 && w.bitsPerSample != 16)
      || (w.numChannels != 1 && w.numChannels != 2)) {
    Serial.println("unsupported WAV format (want 8/16-bit mono/stereo PCM)");
    f.close();
    return;
  }

  const int    bytesPerFrame = (w.bitsPerSample / 8) * w.numChannels;
  const int    period_us     = 1000000 / w.sampleRate;
  const size_t BUF           = 1024;
  uint8_t      buf[BUF];
  uint32_t     next = micros();

  while (f.available()) {
    int n = f.read(buf, BUF);
    for (int i = 0; i + bytesPerFrame <= n; i += bytesPerFrame) {
      uint8_t duty;
      if (w.bitsPerSample == 8) {
        duty = buf[i];                              // unsigned 0..255
      } else {
        // 16-bit signed LE, left channel only
        int16_t s = (int16_t)(buf[i] | (buf[i + 1] << 8));
        duty = (uint8_t)((s + 32768) >> 8);
      }
      // Wait until the sample's slot comes due. Busy-wait is fine here
      // because the sketch has nothing else to do during playback.
      while ((int32_t)(micros() - next) < 0) { /* spin */ }
      next += period_us;
      ledcWrite(SPEAKER_PIN, duty);
    }
  }
  f.close();
}

// ---- setup / loop --------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);                                       // give USB CDC a moment

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed. Did you upload the filesystem?");
    // Format on the fly so sketch still runs; sound.wav just won't be there.
    LittleFS.begin(true);
  }

  pinMode(SPEAKER_PIN, OUTPUT);
  ledcAttach(SPEAKER_PIN, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcWrite(SPEAKER_PIN, 128);                      // sit at midpoint (silence)

  playWav(WAV_PATH);
  ledcWrite(SPEAKER_PIN, 128);                      // back to silence
}

void loop() {
  delay(5000);
  playWav(WAV_PATH);
  ledcWrite(SPEAKER_PIN, 128);
}
