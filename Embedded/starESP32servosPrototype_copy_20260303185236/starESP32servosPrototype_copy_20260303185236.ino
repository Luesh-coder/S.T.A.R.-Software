#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// ===================== Wi-Fi =====================
static const char* AP_SSID     = "STAR-ESP32";
static const char* AP_PASS     = "star12345"; // min 8 chars for WPA2

// ===================== REST / WS paths (must match src/api/esp32.ts) =====================
static const char* PATH_STATUS     = "/api/status";
static const char* PATH_MODE       = "/api/mode";
static const char* PATH_TRACKING   = "/api/tracking";
static const char* PATH_NEW_TARGET = "/api/target/new";
static const char* PATH_LIGHT      = "/api/light";
// WS connects on ws://<host>:81  (WS_PORT in src/api/config.ts)

WebServer         server(80);
WebSocketsServer  wsServer(81);

// ===================== Pin assignments =====================
static const int SERVO_PAN_PIN  = 8;
static const int SERVO_TILT_PIN = 9;
static const int LIGHT_PIN      = 2;  // GPIO driving the spotlight relay / LED

// ===================== Servo limits =====================
static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2400;

static const int PAN_CENTER      = 90;
static const int PAN_LEFT_LIMIT  = 20;
static const int PAN_RIGHT_LIMIT = 160;

static const int TILT_CENTER     = 90;
static const int TILT_DOWN_LIMIT = 20;
static const int TILT_UP_LIMIT   = 160;

// Manual WS movement
static const int      MANUAL_STEP_DEG        = 2;
static const uint32_t MANUAL_STEP_INTERVAL_MS = 20;

Servo panServo;
Servo tiltServo;

// ===================== Runtime state =====================
String mode            = "auto";  // "auto" | "manual"
bool   trackingEnabled = false;
bool   lightEnabled    = false;

int currentPan  = PAN_CENTER;
int currentTilt = TILT_CENTER;

bool moveUp    = false;
bool moveDown  = false;
bool moveLeft  = false;
bool moveRight = false;

uint32_t lastManualStepMs = 0;

// ===================== Helpers =====================
static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void writeServos(int pan, int tilt) {
  currentPan  = clampi(pan,  PAN_LEFT_LIMIT,  PAN_RIGHT_LIMIT);
  currentTilt = clampi(tilt, TILT_DOWN_LIMIT, TILT_UP_LIMIT);
  panServo.write(currentPan);
  tiltServo.write(currentTilt);
}

void setStopMotion() {
  moveUp = moveDown = moveLeft = moveRight = false;
}

String readBody() {
  if (!server.hasArg("plain")) return "";
  return server.arg("plain");
}

// Minimal JSON field parsers (no extra library needed)
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

// ===================== HTTP helpers =====================
void sendJson(int code, const String& body) {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.send(code, "application/json", body);
}

void handleOptions() {
  sendJson(200, "{}");
}

// ===================== REST handlers =====================

// GET /api/status  →  StatusResponse (see src/api/types.ts)
void handleStatus() {
  String body =
    "{\"mode\":\""    + mode +
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
  if (mode == "auto") setStopMotion();
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
  Serial.printf("[REST] POST /api/tracking → tracking=%s\n", enabled ? "true" : "false");
  sendJson(200, "{\"ok\":true}");
}

// POST /api/target/new  body: {}
void handleNewTarget() {
  Serial.println("[REST] POST /api/target/new → new target requested");
  // Hook for vision pipeline: reset tracker, pick new detection, etc.
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

  // CORS preflight
  server.on(PATH_STATUS,     HTTP_OPTIONS, handleOptions);
  server.on(PATH_MODE,       HTTP_OPTIONS, handleOptions);
  server.on(PATH_TRACKING,   HTTP_OPTIONS, handleOptions);
  server.on(PATH_NEW_TARGET, HTTP_OPTIONS, handleOptions);
  server.on(PATH_LIGHT,      HTTP_OPTIONS, handleOptions);

  server.onNotFound(handleNotFound);
  server.begin();
}

// ===================== WebSocket (manual D-pad) =====================
// App connects to ws://<host>:81  (ManualWsClient in src/api/ws.ts)
// Commands: { "type":"move","dir":"up|down|left|right" }
//           { "type":"stop" }

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

// ===================== Manual movement loop =====================
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

// ===================== Setup / Loop =====================
void setup() {
  Serial.begin(115200);

  // Servos
  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);
  panServo.attach(SERVO_PAN_PIN,  SERVO_MIN_US, SERVO_MAX_US);
  tiltServo.attach(SERVO_TILT_PIN, SERVO_MIN_US, SERVO_MAX_US);
  writeServos(PAN_CENTER, TILT_CENTER);

  // Light
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);

  // Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[INIT] AP %s  SSID=%s  IP=%s\n",
    apOk ? "OK" : "FAILED", AP_SSID, apIP.toString().c_str());

  setupRestApi();
  setupWebSocket();

  Serial.println("[INIT] REST ready on port 80");
  Serial.println("[INIT] WebSocket ready on port 81");
  Serial.println("[INIT] Waiting for app...");
}

void loop() {
  server.handleClient();
  wsServer.loop();
  applyManualMovement();
}
