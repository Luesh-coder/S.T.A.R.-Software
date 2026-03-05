#include <Arduino.h>
#include <ESP32Servo.h>

#define START_BYTE 0xAA
#define END_BYTE   0xFF
#define PACKET_LEN 12  // start(1) + cmd(1) + x(4) + y(4) + checksum(1) + end(1)

static uint8_t calcChecksum(const uint8_t* data, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) c ^= data[i];
  return c;
}

// ===== Servo config =====
static const int SERVO_PAN_PIN  = 8;
static const int SERVO_TILT_PIN = 9;

static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2400;

static const int PAN_CENTER  = 90;
static const int PAN_LEFT_LIMIT  = 20;
static const int PAN_RIGHT_LIMIT = 160;

static const int TILT_CENTER = 90;
static const int TILT_DOWN_LIMIT = 20;
static const int TILT_UP_LIMIT   = 160;

static const float DEADBAND = 0.06f;
static const float SMOOTH_ALPHA = 0.25f;

// ===== Timeout / idle behavior =====
static const uint32_t PACKET_TIMEOUT_MS = 300;   // if no valid packet within this time -> idle
static const uint32_t IDLE_RETURN_MS    = 200;   // how often we "re-center" while idle
static const bool RETURN_TO_CENTER_ON_IDLE = true;  // set false if you only want "hold last"

Servo panServo;
Servo tiltServo;

float filtered_x = 0.0f;
float filtered_y = 0.0f;

uint32_t lastGoodPacketMs = 0;
uint32_t lastIdleActionMs = 0;

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void setup() {
  Serial.begin(115200);

  const int RX_PIN = 44;
  const int TX_PIN = 43;
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);

  panServo.attach(SERVO_PAN_PIN, SERVO_MIN_US, SERVO_MAX_US);
  tiltServo.attach(SERVO_TILT_PIN, SERVO_MIN_US, SERVO_MAX_US);

  panServo.write(PAN_CENTER);
  tiltServo.write(TILT_CENTER);

  lastGoodPacketMs = millis();
  lastIdleActionMs = millis();

  Serial.printf("UART RX=%d TX=%d | Pan GPIO=%d | Tilt GPIO=%d\n",
                RX_PIN, TX_PIN, SERVO_PAN_PIN, SERVO_TILT_PIN);
}

bool readAndProcessPacket() {
  if (Serial1.available() < PACKET_LEN) return false;

  // Resync: scan until START_BYTE
  while (Serial1.available() && Serial1.peek() != START_BYTE) {
    Serial1.read();
  }
  if (Serial1.available() < PACKET_LEN) return false;

  uint8_t pkt[PACKET_LEN];
  size_t n = Serial1.readBytes(pkt, PACKET_LEN);
  if (n != PACKET_LEN) return false;

  if (pkt[0] != START_BYTE || pkt[11] != END_BYTE) return false;

  uint8_t expected = calcChecksum(&pkt[1], 1 + 8); // pkt[1..9]
  uint8_t received = pkt[10];
  if (expected != received) return false;

  uint8_t cmd = pkt[1];
  if (cmd != 0x01) return false;

  float norm_x, norm_y;
  memcpy(&norm_x, &pkt[2], 4);
  memcpy(&norm_y, &pkt[6], 4);

  norm_x = clampf(norm_x, -1.0f, 1.0f);
  norm_y = clampf(norm_y, -1.0f, 1.0f);

  if (fabsf(norm_x) < DEADBAND) norm_x = 0.0f;
  if (fabsf(norm_y) < DEADBAND) norm_y = 0.0f;

  filtered_x = (SMOOTH_ALPHA * norm_x) + ((1.0f - SMOOTH_ALPHA) * filtered_x);
  filtered_y = (SMOOTH_ALPHA * norm_y) + ((1.0f - SMOOTH_ALPHA) * filtered_y);

  float tx = (filtered_x + 1.0f) * 0.5f; // 0..1
  float ty = (filtered_y + 1.0f) * 0.5f; // 0..1

  int panDeg  = (int)lroundf(PAN_LEFT_LIMIT  + tx * (PAN_RIGHT_LIMIT - PAN_LEFT_LIMIT));
  int tiltDeg = (int)lroundf(TILT_DOWN_LIMIT + ty * (TILT_UP_LIMIT   - TILT_DOWN_LIMIT));

  panDeg  = clampi(panDeg,  PAN_LEFT_LIMIT,  PAN_RIGHT_LIMIT);
  tiltDeg = clampi(tiltDeg, TILT_DOWN_LIMIT, TILT_UP_LIMIT);

  panServo.write(panDeg);
  tiltServo.write(tiltDeg);

  Serial.printf("norm_x=%.3f norm_y=%.3f | pan=%d tilt=%d\n", norm_x, norm_y, panDeg, tiltDeg);
  return true;
}

void doIdleBehavior() {
  // If you want “idle” to mean “stop PWM completely” (no holding torque),
  // you can detach() here. But most gimbals want to hold position.
  // We'll re-center (optional) at a limited rate to avoid spamming writes.

  if (!RETURN_TO_CENTER_ON_IDLE) return;

  uint32_t now = millis();
  if (now - lastIdleActionMs >= IDLE_RETURN_MS) {
    panServo.write(PAN_CENTER);
    tiltServo.write(TILT_CENTER);

    // also reset filters so we don't "jump" when packets resume
    filtered_x = 0.0f;
    filtered_y = 0.0f;

    lastIdleActionMs = now;
    Serial.println("IDLE: No packets -> returning to center");
  }
}

void loop() {
  uint32_t now = millis();

  // Consume/process as many packets as are already waiting (keeps latency low)
  bool gotGood = false;
  while (Serial1.available() >= PACKET_LEN) {
    if (readAndProcessPacket()) {
      gotGood = true;
      lastGoodPacketMs = now;
    } else {
      // If packet was bad, we keep looping to try to resync on next bytes
      // (resync happens inside readAndProcessPacket)
      break;
    }
  }

  // If we haven't had a good packet recently, go idle
  if (!gotGood && (now - lastGoodPacketMs > PACKET_TIMEOUT_MS)) {
    doIdleBehavior();
  }
}