/*
  ultrasonic_test.ino
  ---------------------------------------------------------
  Standalone test sketch for ONE HC-SR04 ultrasonic sensor.
  Use this to verify each of your 3 sensors individually
  BEFORE wiring all three into rover_autonomous.ino.

  Wiring (HC-SR04):
    VCC  -> 5V
    GND  -> GND
    TRIG -> TRIG_PIN (set below)
    ECHO -> ECHO_PIN (set below)

  Open Serial Monitor at 9600 baud to see live readings.
  ---------------------------------------------------------
*/

// ---- change these two pins to test each sensor in turn ----
const uint8_t TRIG_PIN = 9;
const uint8_t ECHO_PIN = 10;
// -------------------------------------------------------------

const unsigned long PULSE_TIMEOUT_US = 25000UL; // ~4.3 m max range, avoids long blocking
const unsigned int  SAMPLES          = 3;       // median-of-N filtering

float readDistanceCM() {
  float readings[SAMPLES];

  for (uint8_t i = 0; i < SAMPLES; i++) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    unsigned long duration = pulseIn(ECHO_PIN, HIGH, PULSE_TIMEOUT_US);

    if (duration == 0) {
      readings[i] = -1; // no echo -> out of range / timeout
    } else {
      readings[i] = duration * 0.0343f / 2.0f; // speed of sound = 343 m/s
    }
    delay(10);
  }

  // simple bubble sort (SAMPLES is tiny, so this is fine)
  for (uint8_t i = 0; i < SAMPLES - 1; i++) {
    for (uint8_t j = 0; j < SAMPLES - 1 - i; j++) {
      if (readings[j] > readings[j + 1]) {
        float t = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = t;
      }
    }
  }

  return readings[SAMPLES / 2]; // median
}

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  Serial.println(F("HC-SR04 test starting..."));
  Serial.print(F("TRIG pin: ")); Serial.println(TRIG_PIN);
  Serial.print(F("ECHO pin: ")); Serial.println(ECHO_PIN);
}

void loop() {
  float d = readDistanceCM();

  if (d < 0) {
    Serial.println(F("No echo (out of range or sensor not wired correctly)"));
  } else {
    Serial.print(F("Distance: "));
    Serial.print(d, 1);
    Serial.println(F(" cm"));
  }

  delay(200);
}
