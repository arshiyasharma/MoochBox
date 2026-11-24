// MoochBox: shared Spotify queue on an ESP32.
// See ../plan.md for the full design.
//
// For now this is a wiring test: it blinks the green and red LEDs
// and prints to Serial Monitor (115200 baud).

const int GREEN_LED = 18;
const int RED_LED = 19;

void setup() {
  Serial.begin(115200);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  Serial.println("MoochBox: LED wiring test");
}

void loop() {
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);
  Serial.println("green on");
  delay(500);

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  Serial.println("red on");
  delay(500);
}
