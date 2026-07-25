/*
  rover_autonomous.ino
  ---------------------------------------------------------
  Full rover controller: L293D motor shield + 3x HC-SR04
  (left / center / right). Runs on the Arduino Uno.

  Talks to the Raspberry Pi over USB Serial (9600 baud) using
  a tiny line-based text protocol -- see PROTOCOL.md in this
  project for the full spec used by rpi-bridge and the GUI.

  Modes (only one drive mode active at a time):
    MANUAL       - joystick from the GUI drives the motors directly
    ASSIST       - manual driving, but forward motion is blocked /
                   eased off automatically if a sensor sees an
                   obstacle inside SAFE_DISTANCE_CM (toggle: OA)
    AUTONOMOUS   - rover drives itself, steering away from
                   obstacles using the 3 sensors (toggle: AUTO)

  MOTOR SHIELD ASSUMPTION
  ------------------------
  The "L293D Motor Driver Shield" from your parts list is the
  classic Adafruit Motor Shield v1 clone, controlled with the
  AFMotor library. Install it first:
    Arduino IDE -> Tools -> Manage Libraries -> search "AFMotor"
    (library name: "Adafruit Motor Shield library")

  That shield uses pins 3,4,5,6,7,8,11,12 internally, which is
  why the ultrasonic sensors below are wired to the pins that
  are left free: 2, 9, 10, 13, A0, A1.
  If your shield/board revision differs, re-check with
  ultrasonic_test.ino before trusting these pin numbers.
  ---------------------------------------------------------
*/

#include <AFMotor.h>

// ================= MOTORS =================
// Adjust which AF_DCMotor number maps to which physical wheel
// to match how you actually wired the shield.
AF_DCMotor motorFL(1);
AF_DCMotor motorFR(2);
AF_DCMotor motorRL(3);
AF_DCMotor motorRR(4);

// ================= ULTRASONIC PINS =================
const uint8_t TRIG_L = A0, ECHO_L = A1;
const uint8_t TRIG_C = 9,  ECHO_C = 10;
const uint8_t TRIG_R = 13, ECHO_R = 2;

const unsigned long PULSE_TIMEOUT_US = 25000UL;

// ================= TUNABLES =================
const float SAFE_DISTANCE_CM   = 30.0;  // ASSIST: slow/stop threshold
const float AUTO_STOP_CM       = 25.0;  // AUTONOMOUS: distance that triggers a turn
const float AUTO_BACKUP_CM     = 12.0;  // AUTONOMOUS: distance that triggers reverse
const uint8_t BASE_SPEED       = 180;   // 0-255
const uint8_t TURN_SPEED       = 160;
const unsigned long TELEMETRY_MS = 150;
const unsigned long SERIAL_WATCHDOG_MS = 800; // stop if GUI link drops

// ================= STATE =================
enum DriveMode { MANUAL, ASSIST, AUTONOMOUS };
DriveMode mode = MANUAL;

int joyX = 0, joyY = 0; // -100..100 from GUI (x = steer, y = throttle)
float distL = -1, distC = -1, distR = -1;

unsigned long lastTelemetry = 0;
unsigned long lastCommand = 0;

// simple state machine for the autonomous behaviour
enum AutoState { AUTO_FORWARD, AUTO_TURN, AUTO_REVERSE };
AutoState autoState = AUTO_FORWARD;
unsigned long autoStateSince = 0;
int8_t autoTurnDir = 1; // 1 = turn right, -1 = turn left

// ================= LOW LEVEL MOTOR HELPERS =================
void setAllMotors(uint8_t speed) {
  motorFL.setSpeed(speed);
  motorFR.setSpeed(speed);
  motorRL.setSpeed(speed);
  motorRR.setSpeed(speed);
}

void driveStop() {
  motorFL.run(RELEASE);
  motorFR.run(RELEASE);
  motorRL.run(RELEASE);
  motorRR.run(RELEASE);
}

void driveForward(uint8_t speed) {
  setAllMotors(speed);
  motorFL.run(FORWARD);
  motorFR.run(FORWARD);
  motorRL.run(FORWARD);
  motorRR.run(FORWARD);
}

void driveBackward(uint8_t speed) {
  setAllMotors(speed);
  motorFL.run(BACKWARD);
  motorFR.run(BACKWARD);
  motorRL.run(BACKWARD);
  motorRR.run(BACKWARD);
}

// spin in place: dir > 0 turns right, dir < 0 turns left
void driveTurn(int8_t dir, uint8_t speed) {
  setAllMotors(speed);
  if (dir > 0) {
    motorFL.run(FORWARD);  motorRL.run(FORWARD);
    motorFR.run(BACKWARD); motorRR.run(BACKWARD);
  } else {
    motorFL.run(BACKWARD); motorRL.run(BACKWARD);
    motorFR.run(FORWARD);  motorRR.run(FORWARD);
  }
}

// differential drive from a joystick vector, x/y in -100..100
void driveFromJoystick(int x, int y) {
  int left  = constrain(y + x, -100, 100);
  int right = constrain(y - x, -100, 100);

  uint8_t leftSpeed  = (uint8_t)map(abs(left), 0, 100, 0, 255);
  uint8_t rightSpeed = (uint8_t)map(abs(right), 0, 100, 0, 255);

  motorFL.setSpeed(leftSpeed);
  motorRL.setSpeed(leftSpeed);
  motorFR.setSpeed(rightSpeed);
  motorRR.setSpeed(rightSpeed);

  motorFL.run(left >= 0 ? FORWARD : BACKWARD);
  motorRL.run(left >= 0 ? FORWARD : BACKWARD);
  motorFR.run(right >= 0 ? FORWARD : BACKWARD);
  motorRR.run(right >= 0 ? FORWARD : BACKWARD);

  if (left == 0 && right == 0) driveStop();
}

// ================= ULTRASONIC =================
float readDistanceCM(uint8_t trig, uint8_t echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  unsigned long duration = pulseIn(echo, HIGH, PULSE_TIMEOUT_US);
  if (duration == 0) return -1;
  return duration * 0.0343f / 2.0f;
}

void updateSensors() {
  distL = readDistanceCM(TRIG_L, ECHO_L);
  distC = readDistanceCM(TRIG_C, ECHO_C);
  distR = readDistanceCM(TRIG_R, ECHO_R);
}

// treat "no echo" as "clear / far away" so it never falsely blocks driving
float safeVal(float d) { return d < 0 ? 999 : d; }

// ================= SERIAL PROTOCOL =================
// Incoming lines from the Raspberry Pi / GUI:
//   J:<x>,<y>      joystick vector, x/y in -100..100
//   OA:1 / OA:0    obstacle-avoidance ASSIST on/off (manual driving)
//   AUTO:1 / AUTO:0 autonomous mode on/off
//   STOP           immediate stop, cancels autonomous mode
//
// Outgoing telemetry (every TELEMETRY_MS), one JSON line:
//   {"left":123.4,"center":45.0,"right":200.0,"oa":1,"auto":0,"mode":"manual"}

bool obstacleAssist = false;

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;
  lastCommand = millis();

  if (line.startsWith("J:")) {
    int comma = line.indexOf(',');
    if (comma > 0) {
      joyX = constrain(line.substring(2, comma).toInt(), -100, 100);
      joyY = constrain(line.substring(comma + 1).toInt(), -100, 100);
      if (mode == AUTONOMOUS) mode = obstacleAssist ? ASSIST : MANUAL; // manual stick overrides auto
    }
  } else if (line.startsWith("OA:")) {
    obstacleAssist = (line.charAt(3) == '1');
    if (mode != AUTONOMOUS) mode = obstacleAssist ? ASSIST : MANUAL;
  } else if (line.startsWith("AUTO:")) {
    bool on = (line.charAt(5) == '1');
    mode = on ? AUTONOMOUS : (obstacleAssist ? ASSIST : MANUAL);
    autoState = AUTO_FORWARD;
    autoStateSince = millis();
  } else if (line == "STOP") {
    joyX = 0; joyY = 0;
    mode = obstacleAssist ? ASSIST : MANUAL;
    driveStop();
  }
}

void readSerial() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      handleCommand(buf);
      buf = "";
    } else if (c != '\r') {
      buf += c;
    }
  }
}

void sendTelemetry() {
  Serial.print(F("{\"left\":"));
  Serial.print(distL, 1);
  Serial.print(F(",\"center\":"));
  Serial.print(distC, 1);
  Serial.print(F(",\"right\":"));
  Serial.print(distR, 1);
  Serial.print(F(",\"oa\":"));
  Serial.print(obstacleAssist ? 1 : 0);
  Serial.print(F(",\"auto\":"));
  Serial.print(mode == AUTONOMOUS ? 1 : 0);
  Serial.print(F(",\"mode\":\""));
  Serial.print(mode == AUTONOMOUS ? "auto" : (mode == ASSIST ? "assist" : "manual"));
  Serial.println(F("\"}"));
}

// ================= DRIVE MODE LOGIC =================
void runManual() {
  driveFromJoystick(joyX, joyY);
}

void runAssist() {
  float l = safeVal(distL), c = safeVal(distC), r = safeVal(distR);
  bool blocked = (joyY > 0) && (c < SAFE_DISTANCE_CM || (l < SAFE_DISTANCE_CM && r < SAFE_DISTANCE_CM));

  if (blocked) {
    driveStop(); // don't let the user drive straight into something
  } else {
    driveFromJoystick(joyX, joyY);
  }
}

void runAutonomous() {
  float l = safeVal(distL), c = safeVal(distC), r = safeVal(distR);
  unsigned long now = millis();

  switch (autoState) {
    case AUTO_FORWARD:
      if (c < AUTO_BACKUP_CM || (l < AUTO_BACKUP_CM && r < AUTO_BACKUP_CM)) {
        autoState = AUTO_REVERSE;
        autoStateSince = now;
        break;
      }
      if (c < AUTO_STOP_CM || l < AUTO_STOP_CM || r < AUTO_STOP_CM) {
        // pick the side with more room to turn away from the obstacle
        autoTurnDir = (r >= l) ? 1 : -1;
        autoState = AUTO_TURN;
        autoStateSince = now;
        driveStop();
        break;
      }
      driveForward(BASE_SPEED);
      break;

    case AUTO_TURN:
      driveTurn(autoTurnDir, TURN_SPEED);
      // keep turning until that direction is clear, or a timeout keeps us from spinning forever
      if ((autoTurnDir > 0 ? r : l) > AUTO_STOP_CM + 10 || (now - autoStateSince > 1500)) {
        autoState = AUTO_FORWARD;
        autoStateSince = now;
      }
      break;

    case AUTO_REVERSE:
      driveBackward(BASE_SPEED);
      if (now - autoStateSince > 600) {
        autoTurnDir = (r >= l) ? 1 : -1;
        autoState = AUTO_TURN;
        autoStateSince = now;
      }
      break;
  }
}

// ================= MAIN =================
void setup() {
  Serial.begin(9600);

  pinMode(TRIG_L, OUTPUT); pinMode(ECHO_L, INPUT);
  pinMode(TRIG_C, OUTPUT); pinMode(ECHO_C, INPUT);
  pinMode(TRIG_R, OUTPUT); pinMode(ECHO_R, INPUT);

  driveStop();
  lastCommand = millis();
}

void loop() {
  readSerial();
  updateSensors();

  // safety watchdog: if the GUI/link goes quiet, stop the motors
  if (mode != AUTONOMOUS && millis() - lastCommand > SERIAL_WATCHDOG_MS) {
    joyX = 0; joyY = 0;
    driveStop();
  }

  switch (mode) {
    case MANUAL:     runManual();     break;
    case ASSIST:     runAssist();     break;
    case AUTONOMOUS: runAutonomous(); break;
  }

  if (millis() - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = millis();
    sendTelemetry();
  }
}
