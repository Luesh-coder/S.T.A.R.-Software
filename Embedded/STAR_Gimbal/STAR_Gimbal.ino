#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

#define SERVO_MIN  110
#define SERVO_MAX  500

int leftServo  = 0;  // PCA9685 channel 0 (left side)
int rightServo = 1;  // PCA9685 channel 1 (right side)

// Converts an angle (0–180°) to a PCA9685 pulse length
int angleToPulse(int angle) {
  return map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
}

// Tilts the gimbal by driving both servos differentially.
// angle: 0–180°, where 90° is centered.
// Left and right move in opposite directions to produce a clean tilt.
void setTilt(int angle) {
  int mirror = 180 - angle;  // Opposite side is always mirrored
  pwm.setPWM(leftServo,  0, angleToPulse(angle));
  pwm.setPWM(rightServo, 0, angleToPulse(mirror));
}

void setup() {
  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(50);

  delay(500);

  setTilt(90);  // Center both servos on startup
  delay(500);
}

void loop() {
  // Tilt forward (90° → 135°)
  for (int pos = 90; pos <= 135; pos++) {
    setTilt(pos);
    delay(15);
  }

  delay(500);

  // Tilt back to center (135° → 90°)
  for (int pos = 135; pos >= 90; pos--) {
    setTilt(pos);
    delay(15);
  }

  delay(500);

  // Tilt backward (90° → 45°)
  for (int pos = 90; pos >= 45; pos--) {
    setTilt(pos);
    delay(15);
  }

  delay(500);

  // Return to center (45° → 90°)
  for (int pos = 45; pos <= 90; pos++) {
    setTilt(pos);
    delay(15);
  }

  delay(500);
}
