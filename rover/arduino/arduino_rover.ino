/**
 * ═══════════════════════════════════════════════════════════════
 *  ROVER — Arduino Uno Sketch
 *  Responsibilities:
 *   1. Read 4x HC-SR04 ultrasonic sensors (Front, Back, Left, Right)
 *   2. Drive 4x DC motors via L293D Motor Shield
 *   3. Receive commands from ESP8266 via Serial (pins 2/3 Software Serial)
 *   4. Send sensor readings to ESP8266 via the same SoftwareSerial
 *   5. Implement Obstacle Assist & Autonomous avoidance logic
 * ═══════════════════════════════════════════════════════════════
 *
 *  L293D MOTOR SHIELD PIN MAPPING (standard Adafruit/compatible):
 *    Motor 1 (Front-Left)  : M1
 *    Motor 2 (Front-Right) : M2
 *    Motor 3 (Rear-Left)   : M3
 *    Motor 4 (Rear-Right)  : M4
 *
 *  HC-SR04 CONNECTIONS:
 *    Sensor  TRIG   ECHO
 *    Front    A0     A1
 *    Back     A2     A3
 *    Left      6      7
 *    Right     8      9
 *
 *  ESP8266 Serial (SoftwareSerial):
 *    Arduino Pin 2  → ESP TX (GPIO1)   [cross-wired]
 *    Arduino Pin 3  → ESP RX (GPIO3)   [cross-wired]
 *    (Remember: Arduino TX → ESP RX, Arduino RX → ESP TX)
 *
 *  INSTALL LIBRARIES (Arduino IDE → Sketch → Include Library → Manage Libraries):
 *    • AFMotor  (Adafruit Motor Shield Library v1)
 *    • SoftwareSerial (built-in)
 * ═══════════════════════════════════════════════════════════════
 */

#include <AFMotor.h>
#include <SoftwareSerial.h>

// ── SoftwareSerial to ESP8266 ─────────────────────────────────────────────
SoftwareSerial espSerial(2, 3);   // RX=pin2, TX=pin3

// ── Motors ────────────────────────────────────────────────────────────────
AF_DCMotor motorFL(1);   // Front-Left
AF_DCMotor motorFR(2);   // Front-Right
AF_DCMotor motorRL(3);   // Rear-Left
AF_DCMotor motorRR(4);   // Rear-Right

// ── Ultrasonic sensor pins ────────────────────────────────────────────────
// Front
#define TRIG_F A0
#define ECHO_F A1
// Back
#define TRIG_B A2
#define ECHO_B A3
// Left
#define TRIG_L 6
#define ECHO_L 7
// Right
#define TRIG_R 8
#define ECHO_R 9

// ── Thresholds ────────────────────────────────────────────────────────────
#define OBSTACLE_STOP_CM   20    // stop if anything closer than this in auto
#define OBSTACLE_ASSIST_CM 15    // block manual input closer than this

// ── Timing ────────────────────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS 80    // read sensors every 80 ms

// ── State ─────────────────────────────────────────────────────────────────
bool  autoMode       = false;
bool  obstacleAssist = false;
int   driveVx        = 0;     // -255…255  (lateral; positive=right)
int   driveVy        = 0;     // -255…255  (longitudinal; positive=forward)
bool  stopped        = false;

unsigned long lastSensorRead = 0;

// ── Median filter ─────────────────────────────────────────────────────────
#define BUF 5
float bufF[BUF], bufB[BUF], bufL[BUF], bufR[BUF];
int   bufIdx = 0;

float readCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long dur = pulseIn(echoPin, HIGH, 30000UL);   // 30 ms timeout ≈ 510 cm
  if (dur == 0) return 400.0;                   // no echo → treat as clear
  return dur * 0.01715;                         // cm
}

float median5(float* a) {
  float t[BUF];
  memcpy(t, a, sizeof(t));
  // simple insertion sort
  for (int i=1;i<BUF;i++){
    float k=t[i]; int j=i-1;
    while(j>=0&&t[j]>k){t[j+1]=t[j];j--;}
    t[j+1]=k;
  }
  return t[BUF/2];
}

// ── Drive helpers ─────────────────────────────────────────────────────────
void setAllSpeed(int speed, uint8_t dir) {
  speed = constrain(speed, 0, 255);
  motorFL.setSpeed(speed); motorFL.run(dir);
  motorFR.setSpeed(speed); motorFR.run(dir);
  motorRL.setSpeed(speed); motorRL.run(dir);
  motorRR.setSpeed(speed); motorRR.run(dir);
}

void stopAll() {
  motorFL.run(RELEASE); motorFR.run(RELEASE);
  motorRL.run(RELEASE); motorRR.run(RELEASE);
}

/**
 * Tank-style steering from vx/vy:
 *   vy > 0 → forward
 *   vy < 0 → backward
 *   vx > 0 → turn right (left side faster)
 *   vx < 0 → turn left  (right side faster)
 */
void applyDrive(int vx, int vy) {
  int leftSpeed  = constrain(vy + vx, -255, 255);
  int rightSpeed = constrain(vy - vx, -255, 255);

  auto runMotors = [](AF_DCMotor& m1, AF_DCMotor& m2, int spd) {
    uint8_t dir = spd >= 0 ? FORWARD : BACKWARD;
    int     s   = abs(spd);
    m1.setSpeed(s); m1.run(dir);
    m2.setSpeed(s); m2.run(dir);
  };

  runMotors(motorFL, motorRL, leftSpeed);
  runMotors(motorFR, motorRR, rightSpeed);
}

// ── Autonomous logic ──────────────────────────────────────────────────────
float gF, gB, gL, gR;   // current filtered readings (global for auto logic)

void autonomousStep() {
  // If completely clear — go forward
  if (gF > OBSTACLE_STOP_CM && gL > OBSTACLE_STOP_CM && gR > OBSTACLE_STOP_CM) {
    applyDrive(0, 180);
    return;
  }
  // Obstacle ahead
  if (gF <= OBSTACLE_STOP_CM) {
    stopAll();
    delay(150);
    // Choose turn direction based on which side is clearer
    if (gR >= gL) {
      applyDrive(200, 0);   // turn right in place
    } else {
      applyDrive(-200, 0);  // turn left in place
    }
    delay(300);
    stopAll();
    return;
  }
  // Side obstacles only — gentle steer away
  if (gL <= OBSTACLE_STOP_CM) {
    applyDrive(120, 160);
  } else if (gR <= OBSTACLE_STOP_CM) {
    applyDrive(-120, 160);
  }
}

// ── Serial command parser ─────────────────────────────────────────────────
/**
 * Expected JSON from ESP (compact, one line):
 *   {"type":"drive","vx":100,"vy":200}
 *   {"type":"mode","mode":"auto"}
 *   {"type":"mode","mode":"manual"}
 *   {"type":"obstacle","enabled":true}
 *   {"type":"stop"}
 *
 * We parse manually to avoid JSON lib RAM overhead on Uno.
 */
void parseCommand(const String& s) {
  if (s.indexOf("\"stop\"") >= 0) {
    autoMode = false; driveVx = 0; driveVy = 0;
    stopAll(); return;
  }
  if (s.indexOf("\"drive\"") >= 0) {
    driveVx = extractInt(s, "vx");
    driveVy = extractInt(s, "vy");
    return;
  }
  if (s.indexOf("\"mode\"") >= 0) {
    autoMode = (s.indexOf("\"auto\"") >= 0);
    if (autoMode) { driveVx = 0; driveVy = 0; stopAll(); }
    return;
  }
  if (s.indexOf("\"obstacle\"") >= 0) {
    obstacleAssist = (s.indexOf("true") >= 0);
    return;
  }
}

int extractInt(const String& s, const char* key) {
  int idx = s.indexOf(String("\"") + key + "\":");
  if (idx < 0) return 0;
  idx += strlen(key) + 3;
  // skip whitespace
  while (idx < (int)s.length() && (s[idx]==' '||s[idx]==':')) idx++;
  return s.substring(idx).toInt();
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);          // debug
  espSerial.begin(9600);

  // Sensor pins
  int trigs[] = {TRIG_F, TRIG_B, TRIG_L, TRIG_R};
  int echos[] = {ECHO_F, ECHO_B, ECHO_L, ECHO_R};
  for (int i=0;i<4;i++) {
    pinMode(trigs[i], OUTPUT);
    pinMode(echos[i], INPUT);
  }

  // Pre-fill buffers
  for (int i=0;i<BUF;i++) {
    bufF[i]=bufB[i]=bufL[i]=bufR[i]=400.0;
  }

  stopAll();
  Serial.println("Rover ready");
}

// ── Loop ──────────────────────────────────────────────────────────────────
String espBuffer = "";

void loop() {
  // 1. Read any command from ESP
  while (espSerial.available()) {
    char c = espSerial.read();
    if (c == '\n') {
      espBuffer.trim();
      if (espBuffer.length() > 0) parseCommand(espBuffer);
      espBuffer = "";
    } else {
      espBuffer += c;
    }
  }

  // 2. Read sensors on interval
  unsigned long now = millis();
  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;

    bufF[bufIdx] = readCm(TRIG_F, ECHO_F);
    bufB[bufIdx] = readCm(TRIG_B, ECHO_B);
    bufL[bufIdx] = readCm(TRIG_L, ECHO_L);
    bufR[bufIdx] = readCm(TRIG_R, ECHO_R);
    bufIdx = (bufIdx + 1) % BUF;

    gF = median5(bufF);
    gB = median5(bufB);
    gL = median5(bufL);
    gR = median5(bufR);

    // Send JSON to ESP
    String msg = "{\"type\":\"sensors\",\"front\":" + String((int)gF)
               + ",\"back\":"  + String((int)gB)
               + ",\"left\":"  + String((int)gL)
               + ",\"right\":" + String((int)gR)
               + "}\n";
    espSerial.print(msg);
    Serial.print(msg);   // debug
  }

  // 3. Act on mode
  if (autoMode) {
    autonomousStep();
  } else {
    // Manual with optional obstacle assist
    int vx = driveVx, vy = driveVy;
    if (obstacleAssist) {
      if (vy > 0 && gF < OBSTACLE_ASSIST_CM) vy = 0;
      if (vy < 0 && gB < OBSTACLE_ASSIST_CM) vy = 0;
      if (vx > 0 && gR < OBSTACLE_ASSIST_CM) vx = 0;
      if (vx < 0 && gL < OBSTACLE_ASSIST_CM) vx = 0;
    }
    applyDrive(vx, vy);
  }
}
