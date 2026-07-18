/*
 * Phototherapy_Timer — Arduino CLI bring-up sketch.
 * Prints over USB serial and toggles GPIO2 (often onboard LED).
 * Does NOT enable the SSR / lamp path.
 */

static const int LED_PIN = 2;
static const uint32_t BAUD = 115200;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(BAUD);
  delay(500);
  Serial.println();
  Serial.println(F("Phototherapy_Timer arduino_test_firmware / hello_serial"));
  Serial.println(F("SSR stays off in this sketch."));
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println(F("tick"));
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
}
