/*
 * Idle / post-test state: drive the onboard LED (GPIO 2) off and stay there.
 * Does NOT enable the SSR / lamp path.
 */

#ifndef LED_PIN
#define LED_PIN 2
#endif

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  // Keep LED off in case anything else ran before; no serial spam.
  digitalWrite(LED_PIN, LOW);
  delay(1000);
}
