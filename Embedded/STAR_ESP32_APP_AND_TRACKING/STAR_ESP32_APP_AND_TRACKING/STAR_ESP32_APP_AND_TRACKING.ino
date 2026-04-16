/*
 * ============================================================================
 *  S.T.A.R. – ESP32-S3 App + Tracking Firmware
 * ============================================================================
 *
 *  Combines:
 *    - Smooth float-based servo interpolation from
 *      STAR_ESP32_TO_PI_COMMUNICATION (Pi tracking sketch).
 *    - Wi-Fi AP + REST + WebSocket app interface from STAR_ESP32_V3.
 *
 *  Hardware:
 *    - ESP32-S3 dev board
 *    - PCA9685 16-ch PWM driver over I2C (addr 0x40)
 *        Channel 0 : Tilt-Left servo   }  differential pair
 *        Channel 1 : Tilt-Right servo  }  (mirrored: right = 180 - left)
 *        Channel 2 : Pan servo
 *    - UART1 (RX=GPIO44, TX=GPIO43) ← Raspberry Pi CM5 tracking data
 *
 *  Modes:
 *    "auto"   – Listens for UART packets from the Pi and drives servos
 *               accordingly. App can toggle tracking on/off and request
 *               a new target via REST.
 *    "manual" – App sends D-pad commands over WebSocket to nudge target
 *               angles; the same smooth interpolation loop glides the
 *               servos there.
 *
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// ========================== Wi-Fi AP Config ==========================
static const char* AP_SSID = "STAR-ESP32";
static const char* AP_PASS = "star12345";  // min 8 chars for WPA2

// ========================== REST API Paths ===========================
// Must match src/api/esp32.ts in the mobile app
static const char* PATH_STATUS     = "/api/status";
static const char* PATH_MODE       = "/api/mode";
static const char* PATH_TRACKING   = "/api/tracking";
static const char* PATH_NEW_TARGET = "/api/target/new";

WebServer        server(80);
WebSocketsServer wsServer(81);

// ========================== PCA9685 Setup ============================
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// Channel assignments
static const uint8_t SERVO_TILT_LEFT_CH  = 0;
static const uint8_t SERVO_TILT_RIGHT_CH = 1;
static const uint8_t SERVO_PAN_CH        = 2;

// Servo pulse width range (in microseconds) — standard RC hobby range.
static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2400;

// ========================== Pin Assignments ===========================
static const int RX_PIN = 44;
static const int TX_PIN = 43;

// ========================== Servo / Motion Limits ====================
// Pan: full 0–180° , center at 90°
static const int PAN_MIN    = 30;
static const int PAN_MAX    = 150;
static const int PAN_CENTER = 90;

// Tilt: restricted to 110–140°, center at 130°
static const int TILT_MIN    = 110;
static const int TILT_MAX    = 140;
static const int TILT_CENTER = 130;

// Tracking smoothing parameters
static const float DEADBAND     = 0.04f;
static const float SMOOTH_ALPHA = 0.12f;

// Rate limit (degrees per interpolation tick)
// Lowered to reduce mechanical stress on the build during init, manual control,
// and target-switch recentering. Tilt is kept slower than pan because the tilt
// pair carries more mechanical load.
static const float MAX_PAN_DEGREES_PER_UPDATE  = 0.5f;   // ~50°/sec
static const float MAX_TILT_DEGREES_PER_UPDATE = 0.25f;  // ~25°/sec

// Interpolation tick period
static const uint32_t INTERP_INTERVAL_MS = 10;

// Manual D-pad movement parameters
static const int      MANUAL_STEP_DEG         = 1;
static const uint32_t MANUAL_STEP_INTERVAL_MS = 20;

// ========================== UART Protocol ============================
// Binary packet:  [0xAA] [cmd] [x:4B float] [y:4B float] [checksum] [0xFF]
//   Pi → ESP32:  cmd 0x00 = IDLE,  cmd 0x01 = TRACK
//   ESP32 → Pi:  cmd 0x02 = SWITCH_TARGET (payload unused)
#define START_BYTE        0xAA
#define END_BYTE          0xFF
#define PACKET_LEN        12
#define CMD_SWITCH_TARGET 0x02

static const uint32_t PACKET_TIMEOUT_MS = 500;

static uint8_t calcChecksum(const uint8_t* data, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) c ^= data[i];
  return c;
}

static void sendSwitchTargetPacket() {
  uint8_t body[9];
  body[0] = CMD_SWITCH_TARGET;
  memset(&body[1], 0, 8);
  uint8_t csum = calcChecksum(body, 9);

  uint8_t pkt[PACKET_LEN];
  pkt[0] = START_BYTE;
  memcpy(&pkt[1], body, 9);
  pkt[10] = csum;
  pkt[11] = END_BYTE;

  Serial1.write(pkt, PACKET_LEN);
  Serial.println("[UART] -> SWITCH_TARGET sent to Pi");
}

// ========================== Runtime State ============================
String mode = "auto";

bool trackingEnabled = false;

// Current servo positions (float for sub-degree interpolation)
float currentPan  = (float)PAN_CENTER;
float currentTilt = (float)TILT_CENTER;

// Target positions (set by tracking packets or manual D-pad)
float targetPan  = (float)PAN_CENTER;
float targetTilt = (float)TILT_CENTER;

// Tracking smoothing state
float filtered_x = 0.0f;
float filtered_y = 0.0f;

// Timing
uint32_t lastGoodPacketMs = 0;
uint32_t lastInterpMs     = 0;
uint32_t lastManualStepMs = 0;

// Internal tracking sub-mode: IDLE vs TRACK (within "auto" mode)
enum TrackState : uint8_t { TRACK_IDLE = 0, TRACK_ACTIVE = 1 };
static TrackState trackState = TRACK_IDLE;

// UART diagnostic counters
uint32_t rxPacketCount  = 0;
uint32_t rxErrorCount   = 0;
uint32_t lastUartStatsMs = 0;
static const uint32_t UART_STATS_INTERVAL_MS = 2000;

// Manual D-pad flags
bool moveUp    = false;
bool moveDown  = false;
bool moveLeft  = false;
bool moveRight = false;

// ========================== Utility Functions ========================
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

// ========================== PCA9685 Servo Helpers ====================

// Convert degrees (0–180) to PCA9685 pulse ticks at 50 Hz (20 ms period, 4096 ticks)
static int degreesToPulseTicks(int degrees) {
  int pulseUs = map(degrees, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  return (int)((long)pulseUs * 4096L / 20000L);
}

static void writeServo(int channel, int degrees) {
  degrees = clampi(degrees, 0, 180);
  pwm.setPWM(channel, 0, degreesToPulseTicks(degrees));
}

// Move currentPos toward targetPos by at most maxStep degrees
static float stepToward(float currentPos, float targetPos, float maxStep) {
  float diff = targetPos - currentPos;
  if (diff >  maxStep) diff =  maxStep;
  if (diff < -maxStep) diff = -maxStep;
  return currentPos + diff;
}

// Interpolate current positions toward targets and write to servos.
// Driven by a fixed 10 ms tick in loop(); runs in both auto and manual modes.
static void updateServos() {
  currentPan  = stepToward(currentPan,  targetPan,  MAX_PAN_DEGREES_PER_UPDATE);
  currentTilt = stepToward(currentTilt, targetTilt, MAX_TILT_DEGREES_PER_UPDATE);

  int panDeg  = (int)lroundf(currentPan);
  int tiltDeg = (int)lroundf(currentTilt);

  writeServo(SERVO_PAN_CH,        panDeg);
  writeServo(SERVO_TILT_LEFT_CH,  tiltDeg);
  writeServo(SERVO_TILT_RIGHT_CH, 180 - tiltDeg);
}

// Target setters — clamp to limits, do NOT write PWM directly.
static void setTargetPan(float deg) {
  targetPan = clampf(deg, (float)PAN_MIN, (float)PAN_MAX);
}
static void setTargetTilt(float deg) {
  targetTilt = clampf(deg, (float)TILT_MIN, (float)TILT_MAX);
}

// ========================== Manual D-Pad Helpers =====================
void setStopMotion() {
  moveUp = moveDown = moveLeft = moveRight = false;
}

// ========================== UART / Tracking Logic ====================

static void enterIdle() {
  trackState = TRACK_IDLE;
  filtered_x = 0.0f;
  filtered_y = 0.0f;
}

// Read and process a single binary tracking packet from the Pi
bool readAndProcessPacket() {
  if (Serial1.available() < PACKET_LEN) return false;

  while (Serial1.available() && Serial1.peek() != START_BYTE) {
    Serial1.read();
  }
  if (Serial1.available() < PACKET_LEN) return false;

  uint8_t pkt[PACKET_LEN];
  size_t n = Serial1.readBytes(pkt, PACKET_LEN);
  if (n != PACKET_LEN) { rxErrorCount++; return false; }

  if (pkt[0] != START_BYTE || pkt[11] != END_BYTE) { rxErrorCount++; return false; }

  uint8_t expected = calcChecksum(&pkt[1], 1 + 8);  // cmd + x(4) + y(4)
  if (expected != pkt[10]) { rxErrorCount++; return false; }

  uint8_t cmd = pkt[1];
  rxPacketCount++;

  // CMD 0x00 → IDLE
  if (cmd == 0x00) {
    enterIdle();
    return true;
  }

  // CMD 0x01 → TRACK
  if (cmd != 0x01) return false;

  // Only apply motion if tracking is enabled via the app
  if (!trackingEnabled) return true;

  float norm_x, norm_y;
  memcpy(&norm_x, &pkt[2], 4);
  memcpy(&norm_y, &pkt[6], 4);

  norm_x = clampf(norm_x, -1.0f, 1.0f);
  norm_y = clampf(norm_y, -1.0f, 1.0f);

  if (fabsf(norm_x) < DEADBAND) norm_x = 0.0f;
  if (fabsf(norm_y) < DEADBAND) norm_y = 0.0f;

  filtered_x = (SMOOTH_ALPHA * norm_x) + ((1.0f - SMOOTH_ALPHA) * filtered_x);
  filtered_y = (SMOOTH_ALPHA * norm_y) + ((1.0f - SMOOTH_ALPHA) * filtered_y);

  float tx = (filtered_x + 1.0f) * 0.5f;
  float ty = (filtered_y + 1.0f) * 0.5f;

  float panTarget  = (float)PAN_MAX  - tx * (float)(PAN_MAX  - PAN_MIN);
  float tiltTarget = (float)TILT_MIN + ty * (float)(TILT_MAX - TILT_MIN);

  setTargetPan(panTarget);
  setTargetTilt(tiltTarget);

  trackState = TRACK_ACTIVE;

  Serial.printf("[TRACK] nx=%.2f ny=%.2f | targetPan=%.1f targetTilt=%.1f\n",
                norm_x, norm_y, panTarget, tiltTarget);
  return true;
}

// ========================== JSON Helpers (no library needed) ==========
String readBody() {
  if (!server.hasArg("plain")) return "";
  return server.arg("plain");
}

bool jsonGetBool(const String& body, const char* key, bool& out) {
  String marker = String("\"") + key + "\"";
  int keyPos = body.indexOf(marker);
  if (keyPos < 0) return false;
  int colon = body.indexOf(':', keyPos + marker.length());
  if (colon < 0) return false;
  String tail = body.substring(colon + 1);
  tail.trim();
  if (tail.startsWith("true"))  { out = true;  return true; }
  if (tail.startsWith("false")) { out = false; return true; }
  return false;
}

bool jsonGetMode(const String& body, String& out) {
  int keyPos = body.indexOf("\"mode\"");
  if (keyPos < 0) return false;
  int q1 = body.indexOf('"', body.indexOf(':', keyPos + 6) + 1);
  if (q1 < 0) return false;
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  out = body.substring(q1 + 1, q2);
  return true;
}

// ========================== HTTP Helpers ==============================
void sendJson(int code, const String& body) {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.send(code, "application/json", body);
}

void handleOptions() {
  sendJson(200, "{}");
}

// ========================== REST API Handlers ========================

// GET /api/status → StatusResponse
void handleStatus() {
  int reportedPan  = (int)lroundf(currentPan);
  int reportedTilt = (int)lroundf(currentTilt);
  String body =
    "{\"mode\":\""     + mode +
    "\",\"tracking\":" + (trackingEnabled ? "true" : "false") +
    ",\"pan\":"        + String(reportedPan) +
    ",\"tilt\":"       + String(reportedTilt) + "}";
  Serial.printf("[REST] GET /api/status → %s\n", body.c_str());
  sendJson(200, body);
}

// POST /api/mode  body: { "mode": "auto"|"manual" }
void handleMode() {
  String body = readBody();
  String reqMode;
  if (!jsonGetMode(body, reqMode) || (reqMode != "auto" && reqMode != "manual")) {
    Serial.printf("[REST] POST /api/mode — bad body: %s\n", body.c_str());
    sendJson(400, "{\"ok\":false,\"error\":\"Expected { mode: auto|manual }\"}");
    return;
  }
  mode = reqMode;
  if (mode == "auto") {
    setStopMotion();
    enterIdle();
  }
  if (mode == "manual") {
    trackState = TRACK_IDLE;
  }
  Serial.printf("[REST] POST /api/mode → mode=%s\n", mode.c_str());
  sendJson(200, "{\"ok\":true}");
}

// POST /api/tracking  body: { "enabled": true|false }
void handleTracking() {
  String body = readBody();
  bool enabled;
  if (!jsonGetBool(body, "enabled", enabled)) {
    Serial.printf("[REST] POST /api/tracking — bad body: %s\n", body.c_str());
    sendJson(400, "{\"ok\":false,\"error\":\"Expected { enabled: boolean }\"}");
    return;
  }
  trackingEnabled = enabled;
  if (!trackingEnabled) {
    enterIdle();
  }
  Serial.printf("[REST] POST /api/tracking → tracking=%s\n",
                enabled ? "true" : "false");
  sendJson(200, "{\"ok\":true}");
}

// POST /api/target/new  body: {}
void handleNewTarget() {
  Serial.println("[REST] POST /api/target/new -> new target requested");
  sendSwitchTargetPacket();
  filtered_x = 0.0f;
  filtered_y = 0.0f;
  sendJson(200, "{\"ok\":true}");
}

void handleNotFound() {
  Serial.printf("[REST] 404 %s\n", server.uri().c_str());
  sendJson(404, "{\"ok\":false,\"error\":\"Not found\"}");
}

void setupRestApi() {
  server.on(PATH_STATUS,     HTTP_GET,     handleStatus);
  server.on(PATH_MODE,       HTTP_POST,    handleMode);
  server.on(PATH_TRACKING,   HTTP_POST,    handleTracking);
  server.on(PATH_NEW_TARGET, HTTP_POST,    handleNewTarget);

  server.on(PATH_STATUS,     HTTP_OPTIONS, handleOptions);
  server.on(PATH_MODE,       HTTP_OPTIONS, handleOptions);
  server.on(PATH_TRACKING,   HTTP_OPTIONS, handleOptions);
  server.on(PATH_NEW_TARGET, HTTP_OPTIONS, handleOptions);

  server.onNotFound(handleNotFound);
  server.begin();
}

// ========================== WebSocket (Manual D-Pad) =================
// App connects to ws://<host>:81
// Commands:
//   { "type":"move", "dir":"up|down|left|right" }
//   { "type":"stop" }

void parseManualCommand(const String& msg) {
  if (msg.indexOf("\"type\":\"stop\"") >= 0) {
    Serial.println("[WS] stop");
    setStopMotion();
    return;
  }
  if (msg.indexOf("\"type\":\"move\"") < 0) return;

  setStopMotion();
  if      (msg.indexOf("\"dir\":\"up\"")    >= 0) { moveUp    = true; Serial.println("[WS] move UP");    }
  else if (msg.indexOf("\"dir\":\"down\"")  >= 0) { moveDown  = true; Serial.println("[WS] move DOWN");  }
  else if (msg.indexOf("\"dir\":\"left\"")  >= 0) { moveLeft  = true; Serial.println("[WS] move LEFT");  }
  else if (msg.indexOf("\"dir\":\"right\"") >= 0) { moveRight = true; Serial.println("[WS] move RIGHT"); }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  (void)num;
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[WS] client #%u connected\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[WS] client #%u disconnected\n", num);
      setStopMotion();
      break;
    case WStype_TEXT: {
      String msg;
      msg.reserve(length);
      for (size_t i = 0; i < length; i++) msg += static_cast<char>(payload[i]);
      parseManualCommand(msg);
      break;
    }
    default:
      break;
  }
}

void setupWebSocket() {
  wsServer.begin();
  wsServer.onEvent(onWsEvent);
}

// ========================== Manual Movement Loop =====================
// Nudges target angles; the 10 ms interpolation tick does the physical write,
// giving manual motion the same smoothed feel as auto tracking.
void applyManualMovement() {
  if (mode != "manual") return;

  uint32_t now = millis();
  if (now - lastManualStepMs < MANUAL_STEP_INTERVAL_MS) return;
  lastManualStepMs = now;

  float pan  = targetPan;
  float tilt = targetTilt;

  if (moveLeft)  pan  -= (float)MANUAL_STEP_DEG;
  if (moveRight) pan  += (float)MANUAL_STEP_DEG;
  // Inverted: the tilt servo angle decreases as the gimbal rotates physically
  // upward, so "up" on the D-pad must subtract from the tilt angle.
  if (moveUp)    tilt -= (float)MANUAL_STEP_DEG;
  if (moveDown)  tilt += (float)MANUAL_STEP_DEG;

  setTargetPan(pan);
  setTargetTilt(tilt);
}

// ========================== Auto Tracking Loop =======================
void applyAutoTracking() {
  if (mode != "auto") return;

  uint32_t now = millis();
  bool got = false;

  while (Serial1.available() >= PACKET_LEN) {
    if (readAndProcessPacket()) {
      got = true;
      lastGoodPacketMs = now;
    } else {
      break;
    }
  }

  if (!got && (now - lastGoodPacketMs > PACKET_TIMEOUT_MS)) {
    if (trackState != TRACK_IDLE) enterIdle();
  }
}

// ========================== Setup ====================================
void setup() {
  Serial.begin(460800);

  // UART1 for Raspberry Pi CM5 communication
  Serial1.begin(460800, SERIAL_8N1, RX_PIN, TX_PIN);

  // PCA9685 I2C servo driver
  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(300);

  // Soft-start: begin tilt at the lower safety edge and let the interpolation
  // loop walk it up to center at MAX_DEGREES_PER_UPDATE per tick, so the boot
  // motion is gentle on the mechanical build instead of a hard snap-to-center.
  currentPan  = (float)PAN_CENTER;
  currentTilt = (float)TILT_MIN;
  targetPan   = (float)PAN_CENTER;
  targetTilt  = (float)TILT_CENTER;

  // Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[INIT] AP %s  SSID=%s  IP=%s\n",
                apOk ? "OK" : "FAILED", AP_SSID, apIP.toString().c_str());

  setupRestApi();
  setupWebSocket();

  uint32_t now = millis();
  lastGoodPacketMs = now;
  lastInterpMs     = now;
  lastManualStepMs = now;

  Serial.println("[INIT] REST API ready on port 80");
  Serial.println("[INIT] WebSocket ready on port 81");
  Serial.printf("[INIT] UART1 ready — baud=%d, RX=GPIO%d, TX=GPIO%d\n", 460800, RX_PIN, TX_PIN);
  Serial.printf("[INIT] Pan %d-%d (center %d) | Tilt %d-%d (center %d)\n",
                PAN_MIN, PAN_MAX, PAN_CENTER, TILT_MIN, TILT_MAX, TILT_CENTER);
  Serial.println("[INIT] PCA9685 servos: TiltL(ch0), TiltR(ch1, mirrored), Pan(ch2)");
  Serial.println("[INIT] S.T.A.R. App+Tracking Firmware ready.");
}

// ========================== Main Loop ================================
void loop() {
  // Handle app communication
  server.handleClient();
  wsServer.loop();

  // Mode-specific logic
  if (mode == "auto") {
    applyAutoTracking();
  } else {
    applyManualMovement();
  }

  uint32_t now = millis();

  // Fixed-rate servo interpolation (runs in both auto and manual modes)
  if (now - lastInterpMs >= INTERP_INTERVAL_MS) {
    updateServos();
    lastInterpMs = now;
  }

  // Periodic UART diagnostics
  if (now - lastUartStatsMs >= UART_STATS_INTERVAL_MS) {
    lastUartStatsMs = now;
    Serial.printf("[UART] RX packets=%lu errors=%lu | mode=%s tracking=%s\n",
                  (unsigned long)rxPacketCount, (unsigned long)rxErrorCount,
                  mode.c_str(), trackingEnabled ? "ON" : "OFF");
  }
}
