/*
 * Classic "hello world" for board bring-up: blink the onboard LED.
 *
 * On most ESP32 DevKit / WROOM clones (including our 38-pin board),
 * the blue LED is on GPIO 2. If nothing blinks, try LED_PIN 5 or
 * check the board silkscreen.
 *
 * Does NOT drive the SSR / lamp path.
 */

// Most ESP32 DevKit-style modules: onboard LED on GPIO 2
#ifndef LED_PIN
#define LED_PIN 2
#endif

void setup() {
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}
