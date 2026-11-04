#include <Arduino.h>
#include <BluetoothSerial.h>
#include <ESP32Servo.h>

/* =====================================================================
   PIN DESCRIPTION / WIRING LEGEND
   =====================================================================
   Pin   Mode          Function
   ---   ------------  ---------------------------------------------------
   13    OUTPUT        Motor 1 IN1  (left motor direction pin A)
   12    OUTPUT        Motor 1 IN2  (left motor direction pin B)
   14    OUTPUT        Motor 2 IN1  (right motor direction pin A)
   27    OUTPUT        Motor 2 IN2  (right motor direction pin B)
   25    OUTPUT (PWM)  Motor 1 speed (ENA)
   26    OUTPUT (PWM)  Motor 2 speed (ENB)

    5    SERVO         Plow servo      - 30 deg = plow DOWN, 120 deg = plow UP
                                          (initial position = 120 deg / UP)
   18    SERVO         Seeding servo   - rests at 30 deg, sweeps to 120 deg
                                          and back to 30 deg on command
                                          (initial position = 30 deg)

   19    INPUT         LDR sensor (digital out) - LOW reading = dark
                                          (invert in code if your module differs)
   21    OUTPUT        Seed indicator  - HIGH = Seed Empty, LOW = Seed ok
                                          (driven from the LDR reading)

   34    ANALOG IN     Water level sensor - reading < 4000 = water LOW
   22    OUTPUT        Water indicator - HIGH = Low water, LOW = Water ok

    4    OUTPUT        Buzzer - shared alarm for "dark" and "water low"
                                          (beeps 100ms every 3s while either is true)

   23    OUTPUT        Water pump relay - HIGH = pump ON, LOW = pump OFF

   BLUETOOTH STATUS BROADCAST
   ---------------------------------------------------------------------
   Every 3 seconds, regardless of alarm state, the ESP32 sends a single
   comma-separated status line over Bluetooth, e.g.:
       "Low water, Seed Empty"
       "Water ok, Seed ok"
   Water status is reported first, then seed status, in that order.
   ===================================================================== */

// ================== BLUETOOTH ==================
BluetoothSerial SerialBT;

// ================== MOTOR PINS ==================
#define M1_IN1 13  // left motor direction pin A
#define M1_IN2 12  // left motor direction pin B
#define M2_IN1 14  // right motor direction pin A
#define M2_IN2 27  // right motor direction pin B
#define PWM1 25    // left motor speed (ENA)
#define PWM2 26    // right motor speed (ENB)

// ================== SERVO PINS ==================
#define PLOW_SERVO_PIN 5   // plow servo: 30deg = down, 120deg = up
#define SEED_SERVO_PIN 18  // seeding servo: rests 30deg, sweeps to 120deg and back

Servo plowServo;
Servo seedServo;

int plowAngle = 120;  // initial position (up)
int seedAngle = 30;   // initial position

// ================== LDR / SEED STATUS ==================
#define LDR_PIN 19      // digital LDR input - LOW reading = dark
#define LDR_OUT_PIN 21  // HIGH = Seed Empty, LOW = Seed ok

// ================== WATER SENSOR ==================
#define WATER_PIN 34      // analog water level input
#define WATER_OUT_PIN 22  // HIGH = Low water, LOW = Water ok
#define WATER_THRESHOLD 4000

// ================== BUZZER ==================
#define BUZZER_PIN 4                       // shared alarm for "dark" and "water low" conditions
const unsigned long BEEP_INTERVAL = 3000;  // beep every 3s
const unsigned long BEEP_DURATION = 100;   // beep length 100ms
unsigned long lastBeepStart = 0;
bool buzzerOn = false;

// ================== PUMP ==================
#define PUMP_PIN 23  // water pump relay - HIGH = ON, LOW = OFF

// ================== SPEED ==================
const int LOW_SPEED = 190;
const int MEDIUM_SPEED = 210;
const int HIGH_SPEED = 250;

int currentSpeed = MEDIUM_SPEED;

// ================== SLOW SERVO MOVE SETTINGS ==================
const int SERVO_STEP_DELAY = 20;  // ms per degree - controls how "slow" it moves

// ================== STATUS BROADCAST (every 3s, independent of buzzer) ==================
const unsigned long STATUS_INTERVAL = 3000;  // send status every 3s
unsigned long lastStatusSend = 0;

// ================== MOTOR CONTROL ==================
void setSpeed(int pwm) {
  analogWrite(PWM1, pwm);
  analogWrite(PWM2, pwm);
}

void carForward() {
  digitalWrite(M1_IN1, LOW);
  digitalWrite(M1_IN2, HIGH);

  digitalWrite(M2_IN1, LOW);
  digitalWrite(M2_IN2, HIGH);
}

void carBackward() {
  digitalWrite(M1_IN1, HIGH);
  digitalWrite(M1_IN2, LOW);

  digitalWrite(M2_IN1, HIGH);
  digitalWrite(M2_IN2, LOW);
}

void carLeft() {
  digitalWrite(M1_IN1, HIGH);
  digitalWrite(M1_IN2, LOW);

  digitalWrite(M2_IN1, LOW);
  digitalWrite(M2_IN2, HIGH);
}

void carRight() {
  digitalWrite(M1_IN1, LOW);
  digitalWrite(M1_IN2, HIGH);

  digitalWrite(M2_IN1, HIGH);
  digitalWrite(M2_IN2, LOW);
}

void carStop() {
  digitalWrite(M1_IN1, LOW);
  digitalWrite(M1_IN2, LOW);

  digitalWrite(M2_IN1, LOW);
  digitalWrite(M2_IN2, LOW);
}

// ================== SLOW SERVO HELPER ==================
// Moves a servo one degree at a time so the motion is slow/smooth.
void moveServoSlow(Servo &servo, int &currentAngle, int targetAngle) {
  if (currentAngle < targetAngle) {
    for (int a = currentAngle; a <= targetAngle; a++) {
      servo.write(a);
      delay(SERVO_STEP_DELAY);
    }
  } else if (currentAngle > targetAngle) {
    for (int a = currentAngle; a >= targetAngle; a--) {
      servo.write(a);
      delay(SERVO_STEP_DELAY);
    }
  }
  currentAngle = targetAngle;
}

// ================== PLOW COMMANDS ==================
void plowDown() {
  // command "a" -> plow down to 30 deg
  Serial.println("PLOW: moving DOWN to 30 deg...");
  moveServoSlow(plowServo, plowAngle, 30);
  Serial.println("PLOW: reached 30 deg (DOWN)");
}

void plowUp() {
  // command "b" -> plow up to 120 deg
  Serial.println("PLOW: moving UP to 120 deg...");
  moveServoSlow(plowServo, plowAngle, 120);
  Serial.println("PLOW: reached 120 deg (UP)");
}

// ================== SEEDING COMMAND ==================
void seedCycle() {
  // command "c" -> slowly go 30 -> 120, wait 1s, slowly back to 30
  Serial.println("SEED: moving 30 -> 120 deg...");
  moveServoSlow(seedServo, seedAngle, 60);
  Serial.println("SEED: reached 120 deg, waiting 1s...");
  //delay(1000);
  Serial.println("SEED: moving 120 -> 30 deg...");
  moveServoSlow(seedServo, seedAngle, 30);
  Serial.println("SEED: reached 30 deg (cycle complete)");
}

// ================== BUZZER (non-blocking) ==================
// Beeps for BEEP_DURATION every BEEP_INTERVAL, only while active == true.
void handleBuzzer(bool active) {
  unsigned long now = millis();

  if (!active) {
    if (buzzerOn) {
      Serial.println("BUZZER: OFF (condition cleared)");
    }
    buzzerOn = false;
    //digitalWrite(BUZZER_PIN, LOW);
    lastBeepStart = now;  // reset cycle so it beeps promptly once active again
    return;
  }

  if (!buzzerOn && (now - lastBeepStart >= BEEP_INTERVAL)) {
    buzzerOn = true;
    lastBeepStart = now;
    //digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("BUZZER: BEEP (100ms)");
  } else if (buzzerOn && (now - lastBeepStart >= BEEP_DURATION)) {
    buzzerOn = false;
    //digitalWrite(BUZZER_PIN, LOW);
    Serial.println("BUZZER: beep end");
  }
}

// ================== BLUETOOTH STATUS BROADCAST (non-blocking) ==================
// Sends a comma-separated status line every STATUS_INTERVAL ms, e.g.:
//   "Low water, Seed Empty"   or   "Water ok, Seed ok"
// isDark   -> pin21 HIGH = "Seed Empty", pin21 LOW = "Seed ok"
// waterLow -> pin22 HIGH = "Low water",  pin22 LOW = "Water ok"
void sendStatus(bool isDark, bool waterLow) {
  unsigned long now = millis();
  if (now - lastStatusSend < STATUS_INTERVAL) return;
  lastStatusSend = now;

  String waterStr = waterLow ? "Low Water" : "Water ok";
  String seedStr = isDark ? "Seed Empty" : "Seed ok";
  String payload = waterStr + ", " + seedStr;

  SerialBT.println(payload);
  Serial.print("BT TX: ");
  Serial.println(payload);
}

// ================== LDR + WATER SENSOR CHECK ==================
void checkSensors() {
  static bool lastIsDark = false;
  static bool lastWaterLow = false;
  static unsigned long lastPrint = 0;

  // ---- LDR ----
  // Many digital LDR modules output LOW when dark. Invert this if your module behaves oppositely.
  bool isDark = (digitalRead(LDR_PIN) == LOW);
  digitalWrite(LDR_OUT_PIN, isDark ? HIGH : LOW);  // HIGH = Seed Empty, LOW = Seed ok

  // ---- Water sensor ----
  int waterValue = analogRead(WATER_PIN);
  bool waterLow = (waterValue > WATER_THRESHOLD);
  digitalWrite(WATER_OUT_PIN, waterLow ? HIGH : LOW);  // HIGH = Low water, LOW = Water ok

  // ---- Print whenever a state changes ----
  if (isDark != lastIsDark) {
    Serial.println(isDark ? "SEED: Empty detected -> pin21 HIGH" : "SEED: ok -> pin21 LOW");
    lastIsDark = isDark;
  }
  if (waterLow != lastWaterLow) {
    Serial.println(waterLow ? "WATER: Low level detected -> pin22 HIGH" : "WATER: ok -> pin22 LOW");
    lastWaterLow = waterLow;
  }

  // ---- Periodic raw sensor readout every 2s (helps with calibration) ----
  unsigned long now = millis();
  if (now - lastPrint >= 2000) {
    lastPrint = now;
    Serial.print("SENSORS -> LDR pin: ");
    Serial.print(digitalRead(LDR_PIN));
    Serial.print(" | Water raw: ");
    Serial.print(waterValue);
    Serial.print(" | isDark: ");
    Serial.print(isDark);
    Serial.print(" | waterLow: ");
    Serial.println(waterLow);
  }

  // ---- Buzzer: beeps if either condition is active ----
  handleBuzzer(isDark || waterLow);

  // ---- Bluetooth status broadcast: every 3s, regardless of alarm state ----
  sendStatus(isDark, waterLow);
}

// ================== COMMAND HANDLER ==================
void handleCommand(String message) {
  message.trim();
  if (message.length() == 0) return;

  Serial.print("RX CMD: ");
  Serial.println(message);

  // ---- Letter commands ----
  char c = message.charAt(0);
  if (c == 'a' || c == 'A') {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    plowDown();
    return;
  }
  if (c == 'b' || c == 'B') {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    plowUp();
    return;
  }
  if (c == 'c' || c == 'C') {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    seedCycle();
    return;
  }
  if (c == 'd' || c == 'D') {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);

    digitalWrite(PUMP_PIN, HIGH);
    Serial.println("PUMP: ON (pin23 HIGH)");
    return;
  }
  if (c == 'e' || c == 'E') {

    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);

    digitalWrite(PUMP_PIN, LOW);
    Serial.println("PUMP: OFF (pin23 LOW)");
    return;
  }

  // ---- Numeric commands (same as before) ----
  int msg = message.toInt();

  switch (msg) {
    case 1:
      // Forward
      setSpeed(currentSpeed);
      carForward();
      Serial.println("MOTOR: FORWARD");
      break;

    case 2:
      // Backward
      setSpeed(currentSpeed);
      carBackward();
      Serial.println("MOTOR: BACKWARD");
      break;

    case 3:
      // Left
      setSpeed(currentSpeed);
      carLeft();
      Serial.println("MOTOR: LEFT");
      break;

    case 4:
      // Right
      setSpeed(currentSpeed);
      carRight();
      Serial.println("MOTOR: RIGHT");
      break;

    case 5:
      // Stop
      carStop();
      Serial.println("MOTOR: STOP");
      break;

    case 7:
      // Low speed
      currentSpeed = LOW_SPEED;
      setSpeed(currentSpeed);
      Serial.print("Speed set LOW: ");
      Serial.println(currentSpeed);
      break;

    case 8:
      // Medium speed
      currentSpeed = MEDIUM_SPEED;
      setSpeed(currentSpeed);
      Serial.print("Speed set MEDIUM: ");
      Serial.println(currentSpeed);
      break;

    case 9:
      // High speed
      currentSpeed = HIGH_SPEED;
      setSpeed(currentSpeed);
      Serial.print("Speed set HIGH: ");
      Serial.println(currentSpeed);
      break;

    default:
      Serial.print("UNKNOWN COMMAND: ");
      Serial.println(message);
      break;
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("===== ESP32 Fire Robot booting =====");

  // -------- RESERVE PWM TIMERS FOR SERVOS (must be first!) --------
  // ESP32 servos and analogWrite() both use the same LEDC PWM hardware.
  // If analogWrite() grabs a timer first, it can override the 50Hz
  // timing the servos need, causing them to freeze/twitch/not move at
  // all even though everything else (motors, buzzer, pump) works fine.
  // Reserving timers 0-3 here guarantees the Servo library gets clean
  // 50Hz timers before the motor PWM pins claim any.
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  // -------- SERVOS (attach BEFORE any analogWrite call) --------
  // Whichever claims a PWM timer first keeps its frequency. Servos need
  // exactly 50Hz, so they must attach before the motors' analogWrite()
  // calls touch the PWM hardware - otherwise the motor's frequency can
  // silently win and the servo signal breaks even though a servo-only
  // test sketch (which never calls analogWrite) works perfectly fine.
  plowServo.setPeriodHertz(50);  // standard 50Hz servo signal
  seedServo.setPeriodHertz(50);

  plowServo.attach(PLOW_SERVO_PIN, 500, 2400);  // pulse width range in microseconds
  seedServo.attach(SEED_SERVO_PIN, 500, 2400);

  plowServo.write(plowAngle);  // 120 initial
  seedServo.write(seedAngle);  // 30 initial
  Serial.print("SERVOS: plow initial=");
  Serial.print(plowAngle);
  Serial.print(" deg, seed initial=");
  Serial.print(seedAngle);
  Serial.println(" deg");

  // -------- MOTOR PINS --------
  pinMode(M1_IN1, OUTPUT);
  pinMode(M1_IN2, OUTPUT);
  pinMode(M2_IN1, OUTPUT);
  pinMode(M2_IN2, OUTPUT);
  pinMode(PWM1, OUTPUT);
  pinMode(PWM2, OUTPUT);

  currentSpeed = MEDIUM_SPEED;
  setSpeed(currentSpeed);
  carStop();
  Serial.println("MOTORS: pins configured, speed=MEDIUM, state=STOP");

  // -------- LDR / SEED STATUS --------
  pinMode(LDR_PIN, INPUT);
  pinMode(LDR_OUT_PIN, OUTPUT);
  digitalWrite(LDR_OUT_PIN, LOW);
  Serial.println("LDR: pin19 input ready, pin21 (seed status) output ready");

  // -------- WATER SENSOR --------
  pinMode(WATER_OUT_PIN, OUTPUT);
  digitalWrite(WATER_OUT_PIN, LOW);
  // WATER_PIN is analog, no pinMode needed
  Serial.println("WATER SENSOR: pin34 analog ready, pin22 (water status) output ready");

  // -------- BUZZER --------
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("BUZZER: pin4 output ready");

  // -------- PUMP --------
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  Serial.println("PUMP: pin23 output ready, state=OFF");

  // -------- BLUETOOTH --------
  SerialBT.begin("ESP32_BT");  // Bluetooth device name
  Serial.println("Bluetooth Started");
  Serial.println("Pair with: ESP32_BT");

  // Prime the status timer so the first broadcast happens after one
  // full STATUS_INTERVAL, not immediately at boot.
  lastStatusSend = millis();

  Serial.println("===== Setup complete, entering loop =====");
}

// ================== LOOP ==================
void loop() {
  // ---- Read Bluetooth commands ----
  // Every command is a single character (1-9, a-e), so we act on it
  // immediately instead of waiting for a newline - most BT terminal
  // apps don't send one unless you turn on "line ending" manually.
  if (SerialBT.available()) {
    char received = SerialBT.read();

    Serial.print("Received: ");
    Serial.println(received);

    // Ignore newline / carriage return / space - not a command
    if (received == '\n' || received == '\r' || received == ' ') {
      // do nothing
    } else {
      handleCommand(String(received));
    }
  }

  // ---- Continuously monitor LDR + water sensor + buzzer + status broadcast ----
  checkSensors();
}
