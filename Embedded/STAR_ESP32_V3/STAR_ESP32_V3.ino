/*
 * ============================================================================
 *  S.T.A.R. – Combined ESP32-S3 Firmware
 * ============================================================================
 *
 *  Hardware:
 *    - ESP32-S3 dev board
 *    - PCA9685 16-ch PWM driver over I2C (addr 0x40)
 *        Channel 0 : Pan servo
 *        Channel 1 : Tilt-Left servo   }  differential pair
 *        Channel 2 : Tilt-Right servo  }  (mirrored angles)
 *    - Spotlight relay / LED on GPIO 2
 *    - UART1 (RX=GPIO44, TX=GPIO43) ← Raspberry Pi CM5 tracking data
 *
 *  Modes:
 *    "auto"   – ESP32 listens for UART packets from the Pi and drives servos
 *               accordingly. The app can toggle tracking on/off and request
 *               a new target via REST.
 *    "manual" – The mobile app sends D-pad commands over WebSocket to
 *               move the gimbal in small steps. UART tracking is ignored.
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
static const char* PATH_LIGHT      = "/api/light";

WebServer        server(80);
WebSocketsServer wsServer(81);

// ========================== PCA9685 Setup ============================
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// Channel assignments
static const uint8_t SERVO_TILT_LEFT_CH  = 0;  // Left tilt servo
static const uint8_t SERVO_TILT_RIGHT_CH = 1;  // Right tilt servo (mirrored)
static const uint8_t SERVO_PAN_CH        = 2;  // Pan servo

// PCA9685 pulse-tick range (at 50 Hz ≈ 20 ms period, 4096 ticks)
#define SERVO_MIN_TICK 110
#define SERVO_MAX_TICK 500

// ========================== Pin Assignments ===========================
static const int LIGHT_PIN = 2;   // GPIO driving spotlight relay / LED

// UART1 pins for Raspberry Pi CM5 communication
static const int RX_PIN = 44;
static const int TX_PIN = 43;

// ========================== Servo / Motion Limits ====================
static const int PAN_CENTER      = 90;
static const int PAN_LEFT_LIMIT  = 20;
static const int PAN_RIGHT_LIMIT = 160;

static const int TILT_CENTER      = 90;
static const int TILT_DOWN_LIMIT  = 20;
static const int TILT_UP_LIMIT    = 160;

// Tracking smoothing parameters
static const float DEADBAND      = 0.06f;
static const float SMOOTH_ALPHA  = 0.25f;

// Manual D-pad movement parameters
static const int      MANUAL_STEP_DEG         = 2;
static const uint32_t MANUAL_STEP_INTERVAL_MS = 20;

// ========================== UART Protocol ============================
// Binary packet from Pi:  [0xAA] [cmd] [x:4B float] [y:4B float] [checksum] [0xFF]
#define START_BYTE 0xAA
#define END_BYTE   0xFF
#define PACKET_LEN 12

static const uint32_t PACKET_TIMEOUT_MS = 500;

static uint8_t calcChecksum(const uint8_t* data, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) c ^= data[i];
  return c;
}

// ========================== Runtime State ============================
// Mode: "auto" (tracking from Pi) or "manual" (app D-pad)
String mode = "auto";

bool trackingEnabled = false;
bool lightEnabled    = false;

// Current servo positions (degrees)
int currentPan  = PAN_CENTER;
int currentTilt = TILT_CENTER;

// Tracking smoothing state
float filtered_x = 0.0f;
float filtered_y = 0.0f;

// UART timeout tracking
uint32_t lastGoodPacketMs   = 0;
bool     centeredOnceInIdle = false;

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
uint32_t lastManualStepMs = 0;

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

// Convert angle (0–180°) to PCA9685 pulse tick
static int angleToPulseTick(int angleDeg) {
  angleDeg = constrain(angleDeg, 0, 180);
  return map(angleDeg, 0, 180, SERVO_MIN_TICK, SERVO_MAX_TICK);
}

// Set a single PCA9685 channel to a given angle
static void setServoDeg(uint8_t ch, int deg) {
  int tick = angleToPulseTick(deg);
  pwm.setPWM(ch, 0, tick);
}

// Drive the pan servo
static void setPan(int deg) {
  currentPan = clampi(deg, PAN_LEFT_LIMIT, PAN_RIGHT_LIMIT);
  setServoDeg(SERVO_PAN_CH, currentPan);
}

// Drive the differential tilt (two servos mirrored for a clean tilt axis)
// From TestingGimbal: left gets `angle`, right gets `180 - angle`
static void setTilt(int deg) {
  currentTilt = clampi(deg, TILT_DOWN_LIMIT, TILT_UP_LIMIT);
  int mirror = 180 - currentTilt;
  setServoDeg(SERVO_TILT_LEFT_CH,  currentTilt);
  setServoDeg(SERVO_TILT_RIGHT_CH, mirror);
}

// Write both pan and tilt at once
static void writeServos(int pan, int tilt) {
  setPan(pan);
  setTilt(tilt);
}

// ========================== Manual D-Pad Helpers =====================
void setStopMotion() {
  moveUp = moveDown = moveLeft = moveRight = false;
}

// ========================== UART / Tracking Logic ====================

static void enterIdle() {
  trackState = TRACK_IDLE;
  centeredOnceInIdle = false;
  filtered_x = 0.0f;
  filtered_y = 0.0f;
}

static void doIdle() {
  // Center once on entering idle, then hold position
  if (!centeredOnceInIdle) {
    writeServos(PAN_CENTER, TILT_CENTER);
    centeredOnceInIdle = true;
    Serial.println("[TRACK] IDLE: centered and holding.");
  }
}

// Read and process a single binary tracking packet from the Pi
bool readAndProcessPacket() {
  if (Serial1.available() < PACKET_LEN) return false;

  // Sync to START_BYTE
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

  // Only process tracking data if tracking is enabled via the app
  if (!trackingEnabled) return true;  // valid packet, but we ignore motion

  float norm_x, norm_y;
  memcpy(&norm_x, &pkt[2], 4);
  memcpy(&norm_y, &pkt[6], 4);

  // Invert X for correct pan direction
  norm_x = -norm_x;
  norm_x = clampf(norm_x, -1.0f, 1.0f);
  norm_y = clampf(norm_y, -1.0f, 1.0f);

  // Apply deadband
  if (fabsf(norm_x) < DEADBAND) norm_x = 0.0f;
  if (fabsf(norm_y) < DEADBAND) norm_y = 0.0f;

  // Exponential moving average filter
  filtered_x = (SMOOTH_ALPHA * norm_x) + ((1.0f - SMOOTH_ALPHA) * filtered_x);
  filtered_y = (SMOOTH_ALPHA * norm_y) + ((1.0f - SMOOTH_ALPHA) * filtered_y);

  // Map filtered [-1,1] → servo angle range
  float tx = (filtered_x + 1.0f) * 0.5f;
  float ty = (filtered_y + 1.0f) * 0.5f;

  int panDeg  = (int)lroundf(PAN_LEFT_LIMIT  + tx * (PAN_RIGHT_LIMIT - PAN_LEFT_LIMIT));
  int tiltDeg = (int)lroundf(TILT_DOWN_LIMIT + ty * (TILT_UP_LIMIT   - TILT_DOWN_LIMIT));

  panDeg  = clampi(panDeg,  PAN_LEFT_LIMIT,  PAN_RIGHT_LIMIT);
  tiltDeg = clampi(tiltDeg, TILT_DOWN_LIMIT, TILT_UP_LIMIT);

  trackState = TRACK_ACTIVE;
  centeredOnceInIdle = false;

  writeServos(panDeg, tiltDeg);

  Serial.printf("[TRACK] nx=%.2f ny=%.2f | pan=%d tilt=%d\n",
                norm_x, norm_y, panDeg, tiltDeg);
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
  String body =
    "{\"mode\":\""     + mode +
    "\",\"tracking\":" + (trackingEnabled ? "true" : "false") +
    ",\"light\":"      + (lightEnabled    ? "true" : "false") +
    ",\"pan\":"        + String(currentPan) +
    ",\"tilt\":"       + String(currentTilt) + "}";
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
    setStopMotion();       // stop any manual movement
    enterIdle();           // reset tracking state so it re-centers
  }
  if (mode == "manual") {
    // Pause tracking; manual D-pad takes over
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
    enterIdle();  // stop tracking, return to center
  }
  Serial.printf("[REST] POST /api/tracking → tracking=%s\n",
                enabled ? "true" : "false");
  sendJson(200, "{\"ok\":true}");
}

// POST /api/target/new  body: {}
void handleNewTarget() {
  Serial.println("[REST] POST /api/target/new → new target requested");
  // Reset smoothing so the gimbal can snap to the new target quickly
  filtered_x = 0.0f;
  filtered_y = 0.0f;
  // The Pi's vision pipeline handles the actual target switch;
  // this endpoint just resets the ESP32's filter state.
  sendJson(200, "{\"ok\":true}");
}

// POST /api/light  body: { "enabled": true|false }
void handleLight() {
  String body = readBody();
  bool enabled;
  if (!jsonGetBool(body, "enabled", enabled)) {
    Serial.printf("[REST] POST /api/light — bad body: %s\n", body.c_str());
    sendJson(400, "{\"ok\":false,\"error\":\"Expected { enabled: boolean }\"}");
    return;
  }
  lightEnabled = enabled;
  digitalWrite(LIGHT_PIN, lightEnabled ? HIGH : LOW);
  Serial.printf("[REST] POST /api/light → light=%s\n", enabled ? "ON" : "OFF");
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
  server.on(PATH_LIGHT,      HTTP_POST,    handleLight);

  // CORS preflight handlers
  server.on(PATH_STATUS,     HTTP_OPTIONS, handleOptions);
  server.on(PATH_MODE,       HTTP_OPTIONS, handleOptions);
  server.on(PATH_TRACKING,   HTTP_OPTIONS, handleOptions);
  server.on(PATH_NEW_TARGET, HTTP_OPTIONS, handleOptions);
  server.on(PATH_LIGHT,      HTTP_OPTIONS, handleOptions);

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
void applyManualMovement() {
  if (mode != "manual") return;

  uint32_t now = millis();
  if (now - lastManualStepMs < MANUAL_STEP_INTERVAL_MS) return;
  lastManualStepMs = now;

  int pan  = currentPan;
  int tilt = currentTilt;

  if (moveLeft)  pan  -= MANUAL_STEP_DEG;
  if (moveRight) pan  += MANUAL_STEP_DEG;
  if (moveUp)    tilt += MANUAL_STEP_DEG;
  if (moveDown)  tilt -= MANUAL_STEP_DEG;

  if (pan != currentPan || tilt != currentTilt) {
    writeServos(pan, tilt);
  }
}

// ========================== Auto Tracking Loop =======================
void applyAutoTracking() {
  if (mode != "auto") return;

  uint32_t now = millis();
  bool got = false;

  // Drain all available packets from the Pi
  while (Serial1.available() >= PACKET_LEN) {
    if (readAndProcessPacket()) {
      got = true;
      lastGoodPacketMs = now;
    } else {
      break;
    }
  }

  // If no packets for too long, fall back to idle
  if (!got && (now - lastGoodPacketMs > PACKET_TIMEOUT_MS)) {
    if (trackState != TRACK_IDLE) enterIdle();
  }

  if (trackState == TRACK_IDLE) {
    doIdle();
  }
}

// ========================== Setup ====================================
void setup() {
  Serial.begin(115200);

  // UART1 for Raspberry Pi CM5 communication (must match Pi baud rate)
  Serial1.begin(460800, SERIAL_8N1, RX_PIN, TX_PIN);

  // PCA9685 I2C servo driver
  Wire.begin();
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(300);

  // Center all servos on startup
  enterIdle();
  writeServos(PAN_CENTER, TILT_CENTER);

  // Spotlight relay / LED
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);

  // Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[INIT] AP %s  SSID=%s  IP=%s\n",
                apOk ? "OK" : "FAILED", AP_SSID, apIP.toString().c_str());

  // REST API + WebSocket
  setupRestApi();
  setupWebSocket();

  lastGoodPacketMs = millis();

  Serial.println("[INIT] REST API ready on port 80");
  Serial.println("[INIT] WebSocket ready on port 81");
  Serial.printf("[INIT] UART1 ready — baud=%d, RX=GPIO%d, TX=GPIO%d\n", 460800, RX_PIN, TX_PIN);
  Serial.println("[INIT] UART1 waiting for Pi packets...");
  Serial.println("[INIT] PCA9685 servos: TiltL(ch0), TiltR(ch1), Pan(ch2)");
  Serial.println("[INIT] S.T.A.R. Combined Firmware ready.");
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

  // Periodic UART diagnostics
  uint32_t now = millis();
  if (now - lastUartStatsMs >= UART_STATS_INTERVAL_MS) {
    lastUartStatsMs = now;
    Serial.printf("[UART] RX packets=%lu errors=%lu | mode=%s tracking=%s\n",
                  (unsigned long)rxPacketCount, (unsigned long)rxErrorCount,
                  mode.c_str(), trackingEnabled ? "ON" : "OFF");
  }
}
