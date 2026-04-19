/*
 * ============================================================================
 *  S.T.A.R. – ESP32-S3 App + Tracking Firmware  (v2 — velocity control)
 * ============================================================================
 *
 *  Changes vs. previous revision:
 *    - Tracking control law rewritten from "proportional-to-current"
 *      (which integrated error into the target every packet and caused
 *      runaway / overshoot) to proportional VELOCITY control.
 *      norm_x / norm_y now directly command a servo velocity; the 100 Hz
 *      interpolator integrates that velocity into position.
 *    - Rate limits raised from ~5–10°/sec to ~100–150°/sec so the gimbal
 *      can actually keep up with a moving person.
 *    - Sign convention for pan tracking flipped to match camera-right =
 *      pan-right. Verify on hardware; flip PAN_VEL_SIGN if needed.
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
// Pan: restricted to 50–110°, center at 90°
static const int PAN_MIN    = 85;
static const int PAN_MAX    = 115;
static const int PAN_CENTER = 100;

// Tilt: 135° = mechanically level. MIN=115 (up), MAX=155 (down), center=135 (level).
static const int TILT_MIN    = 125;
static const int TILT_MAX    = 145;
static const int TILT_CENTER = 135;

// ----- Tracking parameters (direct position mapping) -----
// Deadband on the normalised error. Below this, servo holds position.
static const float DEADBAND = 0.03f;

// Sign of the position command. Flip if the gimbal moves the wrong way.
//   +1.0 : norm > 0 -> toward MAX   |   -1.0 : norm > 0 -> toward MIN
static const float PAN_POS_SIGN  = -1.0f;
static const float TILT_POS_SIGN = +1.0f;

// --- Pan tuning (direct position mapping, same design as tilt) ---
// norm_x maps to an absolute pan angle; the target is smoothed with an LPF
// to hide the 30 Hz packet stepping, then stepToward caps the slew rate.
static const float PAN_SMOOTH_ALPHA           = 0.45f;  // LPF on position target  (higher = faster, lower = smoother)
static const float MAX_PAN_DEGREES_PER_UPDATE = 2.5f;   // 250 deg/s max slew rate at 100 Hz

// --- Tilt tuning (direct position mapping) ---
static const float TILT_SMOOTH_ALPHA          = 0.55f;  // higher = target catches up faster (reduces undershoot lag)
static const float MAX_TILT_DEGREES_PER_UPDATE = 2.8f;  // 280 deg/s cap

// Upward aim correction applied to the mapped tilt target, in degrees.
// Negative = spotlight aims higher (toward TILT_MIN). Compensates for the
// torso-center sampling point sitting below the subject's true center of mass.
static const float TILT_AIM_BIAS_DEG = -3.0f;

// Interpolation tick period
static const uint32_t INTERP_INTERVAL_MS = 10;

// Manual D-pad movement parameters — intentionally slow for precise control
static const float    MANUAL_STEP_DEG         = 0.5f;  // 0.5°/tick → 25 deg/s (gentler than auto)
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

// Last received normalized tracking coordinates from the Pi
float lastNormX = 0.0f;
float lastNormY = 0.0f;

// Smoothed position targets — LPF hides 30 Hz packet stepping for both axes
float smoothPanTarget  = (float)PAN_CENTER;
float smoothTiltTarget = (float)TILT_CENTER;

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

// Convert degrees (0–180) to PCA9685 pulse ticks at 50 Hz (20 ms period, 4096 ticks).
// Float version preserves sub-degree resolution; at ~2.16 ticks/°, integer
// rounding would throw away ~half a tick — visible as jitter during slow pans.
static int degreesToPulseTicksF(float degrees) {
  if (degrees < 0.0f)   degrees = 0.0f;
  if (degrees > 180.0f) degrees = 180.0f;
  float pulseUs = (float)SERVO_MIN_US +
                  (degrees / 180.0f) * (float)(SERVO_MAX_US - SERVO_MIN_US);
  return (int)lroundf(pulseUs * 4096.0f / 20000.0f);
}

static void writeServoF(int channel, float degrees) {
  pwm.setPWM(channel, 0, degreesToPulseTicksF(degrees));
}

// Integer-degree overload for tilt / manual call sites (routes through float path).
static void writeServo(int channel, int degrees) {
  writeServoF(channel, (float)degrees);
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
// Both pan and tilt use stepToward (slew-rate limiting) so the servo can't
// snap instantaneously to a new target — motion stays smooth regardless of
// how large or sudden the target update is.
static void updateServos() {
  currentPan  = stepToward(currentPan,  targetPan,  MAX_PAN_DEGREES_PER_UPDATE);
  currentTilt = stepToward(currentTilt, targetTilt, MAX_TILT_DEGREES_PER_UPDATE);

  float tiltOutput = clampf(currentTilt, (float)TILT_MIN, (float)TILT_MAX);
  int tiltDeg = (int)lroundf(tiltOutput);
  writeServoF(SERVO_PAN_CH,        currentPan);
  writeServo (SERVO_TILT_LEFT_CH,  tiltDeg);
  writeServo (SERVO_TILT_RIGHT_CH, 180 - tiltDeg);
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
  // On idle, stop commanding new motion. Target = wherever we are now so
  // the servo gently stops instead of continuing toward a stale setpoint.
  targetPan        = currentPan;
  targetTilt       = currentTilt;
  smoothPanTarget  = currentPan;
  smoothTiltTarget = currentTilt;
  lastNormX        = 0.0f;
  lastNormY        = 0.0f;
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

  // ---------- Pan: direct position mapping + exponential smoothing ----------
  // norm_x maps to an absolute angle within [PAN_MIN, PAN_MAX]. The LPF on
  // smoothPanTarget prevents the 30 Hz packet rate from producing stepped
  // motion; stepToward in updateServos() caps the physical slew rate.
  float signedNormX = PAN_POS_SIGN * norm_x;
  float rawPanTarget;
  if (signedNormX >= 0.0f) {
    rawPanTarget = (float)PAN_CENTER + signedNormX * (float)(PAN_MAX - PAN_CENTER);
  } else {
    rawPanTarget = (float)PAN_CENTER + signedNormX * (float)(PAN_CENTER - PAN_MIN);
  }
  smoothPanTarget = PAN_SMOOTH_ALPHA * rawPanTarget + (1.0f - PAN_SMOOTH_ALPHA) * smoothPanTarget;
  setTargetPan(smoothPanTarget);

  // ---------- Tilt: direct position mapping + exponential smoothing ----------
  float signedNormY = TILT_POS_SIGN * norm_y;
  float rawTiltTarget;
  if (signedNormY >= 0.0f) {
    rawTiltTarget = TILT_CENTER + signedNormY * (float)(TILT_MAX - TILT_CENTER);
  } else {
    rawTiltTarget = TILT_CENTER + signedNormY * (float)(TILT_CENTER - TILT_MIN);
  }
  rawTiltTarget += TILT_AIM_BIAS_DEG;
  smoothTiltTarget = TILT_SMOOTH_ALPHA * rawTiltTarget + (1.0f - TILT_SMOOTH_ALPHA) * smoothTiltTarget;
  setTargetTilt(smoothTiltTarget);

  lastNormX   = norm_x;
  lastNormY   = norm_y;
  trackState  = TRACK_ACTIVE;

  static uint8_t trackLogCounter = 0;
  if (++trackLogCounter >= 10) {
    trackLogCounter = 0;
    Serial.printf("[TRACK] nx=%.2f ny=%.2f | tgtPan=%.1f tgtTilt=%.1f | cur=%.1f,%.1f\n",
                  norm_x, norm_y, targetPan, targetTilt, currentPan, currentTilt);
  }
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
    ",\"tilt\":"       + String(reportedTilt) +
    ",\"norm_x\":"     + String(lastNormX, 3) +
    ",\"norm_y\":"     + String(lastNormY, 3) + "}";
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
  // Heartbeat: send ping every 15 s, expect pong within 3 s, drop after 2 missed pongs.
  // Keeps idle connections alive through NAT/Wi-Fi-AP timeouts.
  wsServer.enableHeartbeat(15000, 3000, 2);
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

  if (moveLeft  && pan  < (float)PAN_MAX)  pan  += MANUAL_STEP_DEG;
  if (moveRight && pan  > (float)PAN_MIN)  pan  -= MANUAL_STEP_DEG;
  if (moveUp    && tilt > (float)TILT_MIN) tilt -= MANUAL_STEP_DEG;
  if (moveDown  && tilt < (float)TILT_MAX) tilt += MANUAL_STEP_DEG;

  setTargetPan(pan);
  setTargetTilt(tilt);
}

// ========================== Auto Tracking Loop =======================
void applyAutoTracking() {
  if (mode != "auto") return;

  uint32_t now = millis();

  // Process at most one packet per loop() iteration so that
  // server.handleClient() and wsServer.loop() are never starved by a
  // burst of UART packets from the Pi.
  if (Serial1.available() >= PACKET_LEN) {
    if (readAndProcessPacket()) {
      lastGoodPacketMs = now;
    }
  }

  if (now - lastGoodPacketMs > PACKET_TIMEOUT_MS) {
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
  Serial.printf("[INIT] Pan: position map sign %+.0f | Tilt: position map sign %+.0f\n",
                PAN_POS_SIGN, TILT_POS_SIGN);
  Serial.println("[INIT] PCA9685 servos: TiltL(ch0), TiltR(ch1, mirrored), Pan(ch2)");
  Serial.println("[INIT] S.T.A.R. App+Tracking Firmware (velocity-control) ready.");
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