// float_workshop.ino - Workshop Float Node with Continuous Message Updates
// Displays different nature messages on e-ink display every loop cycle
// Optimized: non-blocking, SRAM-efficient, defensive programming

#include <Arduino.h>
#include <SPI.h>
#include "heltec-eink-modules.h"

// ============================================================================
// CONSTANTS - No magic numbers
// ============================================================================
#define NUM_MESSAGES 10
#define INITIAL_DELAY_MS 500u
#define REFRESH_INTERVAL_MS 5000u
#define TOP_BAND_DIVISOR 6u           // Use top 1/6 of screen (fixed divisor)
#define MESSAGE_PADDING_X 3

// ============================================================================
// SETUP & VARIABLES - Low memory footprint design
// ============================================================================
EInkDisplay_VisionMasterE290 display;  // E-Ink display instance

const char* random_messages[NUM_MESSAGES] = {
  "the float drifts in the reeds",
  "the tide waits with salt water",
  "the wind sings under the moon",
  "the moss remembers the shore",
  "salt carries the wind",
  "a fish counts without end",
  "water breathes toward morning",
  "moss watches beside me",
  "the reeds remember the sky",
  "the moon hums into darkness"
};

// Message history to avoid rapid flickering
unsigned long last_message_time = 0;

// Random seed accumulator for debugging purposes
unsigned long random_seed_value = 0;

// ============================================================================
// HELPER FUNCTIONS - Defensive programming with error checking
// ============================================================================
// Returns a random message from our list
const char* get_random_message() {
  // Get random index (0 to NUM_MESSAGES-1) using the pre-seeded RNG
  int idx = random(NUM_MESSAGES);
  
  return random_messages[idx];
}

// Displays message on top band of e-ink screen
void show_local(const char* message, unsigned long timestamp) {
  // Set display to portrait orientation
  display.portrait();
  
  const int H = display.height();       // Screen height in pixels
  const int Y0 = 0;                     // Top position (y-axis)
  const int BHEIGHT = H / static_cast<int>(TOP_BAND_DIVISOR);   // Use top 1/6 of screen
  
  // Disable fast mode for better text quality
  display.fastmodeOn(false);
  
  // Set drawing window to top band only and clear it first
  display.setWindow(0, Y0, display.width(), BHEIGHT);
  display.fillRect(0, Y0, display.width(), BHEIGHT, WHITE);
  
  {
    DRAW (display) {
      if (message && message[0] != '\0') {
        // Configure text properties
        display.setTextColor(BLACK);
        display.setTextSize(1);
        display.setCursor(MESSAGE_PADDING_X, Y0 + 2);
        
        // Print the message
        display.print(message);
      } else {
        // If no message, just keep white background (no pattern)
        display.fillRect(0, Y0, display.width(), BHEIGHT, WHITE);
      }
    }
  }
  
  // CRITICAL: Explicitly update the e-ink display to refresh it
  // This is required for most e-ink libraries to actually show changes
  display.update();
}

// ============================================================================
// MAIN CODE - Non-blocking loop design
// ============================================================================
void setup() {
  // Start serial communication for debugging
  Serial.begin(115200);
  Serial.println("Workshop Float Node Starting...");
  
  delay(INITIAL_DELAY_MS);  // Small wait for display to initialize
  
  // Seed the random number generator with high-entropy sources:
  // - Use ESP32 hardware RNG if available (best source)
  // - Multiple ADC reads from noise pins
  // - Microseconds with time spread for additional entropy
  
  unsigned long seed = 0;
  
#if defined(ARDUINO_ARCH_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3 chips - use high-entropy seeding
  seed = micros();
  
  // Read ADC noise from multiple pins for additional entropy
  if (analogRead(39)) {
    seed += analogRead(39);
  }
  if (analogRead(48)) {
    seed += analogRead(48) << 8;
  }
  
  // Add timestamp entropy
  seed += millis() << 12;
#else
  // Generic fallback for all other ESP chips
  unsigned long t1 = micros();
  delay(5);
  unsigned long t2 = micros();
  seed += (t2 ^ t1);
  
  // Read ADC noise from multiple pins
  uint8_t adc_39 = analogRead(39); if (!adc_39) adc_39 = 0;
  uint8_t adc_48 = analogRead(48); if (!adc_48) adc_48 = 0;
  seed += (adc_39 + adc_48) << 16;
  
  // Add timestamp entropy
  seed += millis() << 12;
#endif
  
  // Add more entropy by spreading time-based randomness
  delay(5);
  unsigned long now = millis();
  seed += now * 7;  // Simple multiplication to avoid simple patterns
  
  // Mix all seeds using XOR for better distribution
  seed ^= random(256) << 8;
  
  delay(5);
  unsigned long t3 = micros();
  seed += (t3 >> 16) & 0xFFFF;  // Use high bits of time
  
  randomSeed(seed);                        // Seed the RNG with our collected entropy
  
  Serial.print("Random seed initialized: ");
  Serial.println(seed, HEX);
  
  // Get first random message and display it
  const char* msg = get_random_message();
  
  show_local(msg, millis());
  
  last_message_time = millis();
  
  Serial.println("Setup complete! Messages will update continuously.");
}

void loop() {
  unsigned long current_time = millis();
  
  // Display a new random message after refresh interval (non-blocking)
  if (current_time - last_message_time >= REFRESH_INTERVAL_MS) {
    const char* new_msg = get_random_message();
    show_local(new_msg, current_time);
    
    last_message_time = current_time;
    Serial.print("Displaying: ");
    Serial.println(new_msg);
  }
  
  // Main loop does nothing else - display updates with e-ink's natural refresh rate
  // This is efficient for e-ink as it only updates when we explicitly write to it
}