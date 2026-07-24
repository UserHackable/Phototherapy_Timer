/*
 * Probe candidate onboard LED pins (no external components).
 * Each pin blinks at a different rate so you can identify which LED (if any)
 * is GPIO-driven. Serial reports the pattern.
 *
 * Official ESP32-DevKitC often has ONLY a power LED (always on, not GPIO).
 * Many clones put a blue LED on GPIO 2; some use 5 or others.
 */
static const int PINS[] = {2, 5, 4, 12, 13, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
static const int N = sizeof(PINS) / sizeof(PINS[0]);
static const uint32_t HALF_MS[] = {
  100, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600, 650, 700, 750, 800, 850, 900, 950
};

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("led_hunt: toggling candidate GPIOs (SSR not used)"));
  for (int i = 0; i < N; i++) {
    pinMode(PINS[i], OUTPUT);
    digitalWrite(PINS[i], LOW);
    Serial.printf("  GPIO %d half-period %lu ms\n", PINS[i], (unsigned long)HALF_MS[i]);
  }
}

void loop() {
  static uint32_t last[N] = {0};
  static uint8_t level[N] = {0};
  uint32_t now = millis();
  for (int i = 0; i < N; i++) {
    if (now - last[i] >= HALF_MS[i]) {
      last[i] = now;
      level[i] ^= 1;
      digitalWrite(PINS[i], level[i] ? HIGH : LOW);
    }
  }
  static uint32_t tlog = 0;
  if (now - tlog >= 2000) {
    tlog = now;
    Serial.print(F("alive "));
    Serial.println(now);
  }
}
