#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

#define SERVO_MIN  110
#define SERVO_MAX  500

int leftServo  = 0;  // PCA9685 channel 0 (left side)
int rightServo = 1;  // PCA9685 channel 1 (right side)
int panServo   = 2;  // PCA9685 channel 2 (pan)

// Pan limits
#define PAN_MIN    40
#define PAN_MAX    150
#define PAN_CENTER 95

// Tilt limits
#define TILT_MIN    110
#define TILT_MAX    140
#define TILT_CENTER 130

// Converts an angle (0–180°) to a PCA9685 pulse length
int angleToPulse(int angle) {
  return map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
}

// Tilts the gimbal by driving both servos differentially.
// angle: TILT_MIN–TILT_MAX, center at TILT_CENTER.
// Left and right move in opposite directions to produce a clean tilt.
void setTilt(int angle) {
  angle = constrain(angle, TILT_MIN, TILT_MAX);
  int mirror = 180 - angle;  // Opposite side is always mirrored
  pwm.setPWM(leftServo,  0, angleToPulse(angle));
  pwm.setPWM(rightServo, 0, angleToPulse(mirror));
}

// Pans the gimbal. angle: PAN_MIN–PAN_MAX, center at PAN_CENTER.
void setPan(int angle) {
  angle = constrain(angle, PAN_MIN, PAN_MAX);
  pwm.setPWM(panServo, 0, angleToPulse(angle));
}

void setup() {
  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(50);

  delay(500);

  setTilt(TILT_CENTER);  // Center tilt on startup
  setPan(PAN_CENTER);  // Center pan on startup
  delay(500);

   // Pan right (90° → 180°)
  for (int pos = PAN_CENTER; pos <= PAN_MAX; pos++) {
    setPan(pos);
    delay(30);
  }
  delay(500);

  // Pan back to center (180° → 90°)
  for (int pos = PAN_MAX; pos >= PAN_CENTER; pos--) {
    setPan(pos);
    delay(30);
  }
  delay(500);

  // Pan left (90° → 0°)
  for (int pos = PAN_CENTER; pos >= PAN_MIN; pos--) {
    setPan(pos);
    delay(30);
  }
  delay(500);

  // Return to center (0° → 90°)
  for (int pos = PAN_MIN; pos <= PAN_CENTER; pos++) {
    setPan(pos);
    delay(30);
  }
  delay(500);
}

void loop() {
  // --- Tilt test ---
  /*
  // Tilt forward (130° → 140°)
  for (int pos = TILT_CENTER; pos <= TILT_MAX; pos++) {
    setTilt(pos);
    delay(30);
  }
  delay(500);

  // Tilt back to center (140° → 130°)
  for (int pos = TILT_MAX; pos >= TILT_CENTER; pos--) {
    setTilt(pos);
    delay(30);
  }
  delay(500);

  // Tilt backward (130° → 110°)
  for (int pos = TILT_CENTER; pos >= TILT_MIN; pos--) {
    setTilt(pos);
    delay(30);
  }
  delay(500);

  // Return to center (110° → 130°)
  for (int pos = TILT_MIN; pos <= TILT_CENTER; pos++) {
    setTilt(pos);
    delay(30);
  }
  delay(500);

  // --- Pan test ---
  */

  /*
  // Pan right (90° → 180°)
  for (int pos = PAN_CENTER; pos <= PAN_MAX; pos++) {
    setPan(pos);
    delay(30);
  }
  delay(500);

  // Pan back to center (180° → 90°)
  for (int pos = PAN_MAX; pos >= PAN_CENTER; pos--) {
    setPan(pos);
    delay(30);
  }
  delay(500);

  // Pan left (90° → 0°)
  for (int pos = PAN_CENTER; pos >= PAN_MIN; pos--) {
    setPan(pos);
    delay(30);
  }
  delay(500);

  // Return to center (0° → 90°)
  for (int pos = PAN_MIN; pos <= PAN_CENTER; pos++) {
    setPan(pos);
    delay(30);
  }
  delay(500);
  */
}
