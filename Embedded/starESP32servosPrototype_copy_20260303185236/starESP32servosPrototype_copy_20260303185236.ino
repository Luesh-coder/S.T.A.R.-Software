#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// ===================== Wi-Fi / API configuration =====================
static const char* AP_SSID = "STAR-ESP32";
static const char* AP_PASSWORD = "star12345"; // min 8 chars for WPA2

// REST paths expected by the mobile app (see src/api/esp32.ts)
static const char* PATH_STATUS = "/api/status";
static const char* PATH_MODE = "/api/mode";
static const char* PATH_TRACKING = "/api/tracking";
static const char* PATH_NEW_TARGET = "/api/target/new";
static const char* PATH_LIGHT = "/api/light";

// WS path expected by the mobile app (see src/api/config.ts)
static const char* WS_MANUAL_PATH = "/ws/manual";

WebServer server(80);
WebSocketsServer wsServer(81); // internal socket server port; app still connects on :80 path via proxy pattern not available here

// ===================== Servo configuration =====================
static const int SERVO_PAN_PIN = 8;
static const int SERVO_TILT_PIN = 9;

static const int SERVO_MIN_US = 500;
static const int SERVO_MAX_US = 2400;

static const int PAN_CENTER = 90;
static const int PAN_LEFT_LIMIT = 20;
static const int PAN_RIGHT_LIMIT = 160;

static const int TILT_CENTER = 90;
static const int TILT_DOWN_LIMIT = 20;
static const int TILT_UP_LIMIT = 160;

// Step control for manual WS movement
static const int MANUAL_STEP_DEG = 2;
static const uint32_t MANUAL_STEP_INTERVAL_MS = 20;

Servo panServo;
Servo tiltServo;

// ===================== Runtime state =====================
String mode = "auto";  // "auto" | "manual"
bool trackingEnabled = false;
bool lightEnabled = false;

int currentPan = PAN_CENTER;
int currentTilt = TILT_CENTER;

bool moveUp = false;
bool moveDown = false;
bool moveLeft = false;
bool moveRight = false;

uint32_t lastManualStepMs = 0;

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void writeServos(int pan, int tilt) {
  currentPan = clampi(pan, PAN_LEFT_LIMIT, PAN_RIGHT_LIMIT);
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

bool jsonContainsBool(const String& body, const char* key, bool& outValue) {
  String marker = String("\"") + key + "\"";
  int keyPos = body.indexOf(marker);
  if (keyPos < 0) return false;

  int colon = body.indexOf(':', keyPos + marker.length());
  if (colon < 0) return false;

  String tail = body.substring(colon + 1);
  tail.trim();

  if (tail.startsWith("true")) {
    outValue = true;
    return true;
  }
  if (tail.startsWith("false")) {
    outValue = false;
    return true;
  }
  return false;
}

bool jsonContainsStringMode(const String& body, String& outMode) {
  int keyPos = body.indexOf("\"mode\"");
  if (keyPos < 0) return false;

  int firstQuote = body.indexOf('"', keyPos + 6);
  if (firstQuote < 0) return false;
  firstQuote = body.indexOf('"', firstQuote + 1); // quote after colon
  if (firstQuote < 0) return false;

  int secondQuote = body.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return false;

  outMode = body.substring(firstQuote + 1, secondQuote);
  return true;
}

void sendJson(int code, const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.send(code, "application/json", body);
}

void handleOptions() {
  sendJson(200, "{}");
}

void handleStatus() {
  String body =
      "{\"ok\":true,\"mode\":\"" + mode +
      "\",\"tracking\":" + (trackingEnabled ? "true" : "false") +
      ",\"light\":" + (lightEnabled ? "true" : "false") +
      ",\"pan\":" + String(currentPan) +
      ",\"tilt\":" + String(currentTilt) + "}";
  sendJson(200, body);
}

void handleMode() {
  String body = readBody();
  String reqMode;
  if (!jsonContainsStringMode(body, reqMode) || (reqMode != "auto" && reqMode != "manual")) {
    sendJson(400, "{\"ok\":false,\"error\":\"Expected { mode: auto|manual }\"}");
    return;
  }

  mode = reqMode;
  if (mode == "auto") setStopMotion();
  sendJson(200, "{\"ok\":true}");
}

void handleTracking() {
  bool enabled;
  String body = readBody();
  if (!jsonContainsBool(body, "enabled", enabled)) {
    sendJson(400, "{\"ok\":false,\"error\":\"Expected { enabled: boolean }\"}");
    return;
  }

  trackingEnabled = enabled;
  sendJson(200, "{\"ok\":true}");
}

void handleNewTarget() {
  // Placeholder hook for camera / vision pipeline trigger.
  sendJson(200, "{\"ok\":true}");
}

void handleLight() {
  bool enabled;
  String body = readBody();
  if (!jsonContainsBool(body, "enabled", enabled)) {
    sendJson(400, "{\"ok\":false,\"error\":\"Expected { enabled: boolean }\"}");
    return;
  }

  lightEnabled = enabled;
  // TODO: drive physical LED pin here.
  sendJson(200, "{\"ok\":true}");
}

void handleNotFound() {
  sendJson(404, "{\"ok\":false,\"error\":\"Not found\"}");
}

void setupRestApi() {
  server.on(PATH_STATUS, HTTP_GET, handleStatus);
  server.on(PATH_MODE, HTTP_POST, handleMode);
  server.on(PATH_TRACKING, HTTP_POST, handleTracking);
  server.on(PATH_NEW_TARGET, HTTP_POST, handleNewTarget);
  server.on(PATH_LIGHT, HTTP_POST, handleLight);

  // CORS preflight
  server.on(PATH_STATUS, HTTP_OPTIONS, handleOptions);
  server.on(PATH_MODE, HTTP_OPTIONS, handleOptions);
  server.on(PATH_TRACKING, HTTP_OPTIONS, handleOptions);
  server.on(PATH_NEW_TARGET, HTTP_OPTIONS, handleOptions);
  server.on(PATH_LIGHT, HTTP_OPTIONS, handleOptions);

  server.onNotFound(handleNotFound);
  server.begin();
}

void parseManualCommand(const String& msg) {
  // Expected shapes:
  // {"type":"move","dir":"up|down|left|right"}
  // {"type":"stop"}

  if (msg.indexOf("\"type\":\"stop\"") >= 0) {
    setStopMotion();
    return;
  }

  if (msg.indexOf("\"type\":\"move\"") < 0) return;

  setStopMotion();
  if (msg.indexOf("\"dir\":\"up\"") >= 0) moveUp = true;
  else if (msg.indexOf("\"dir\":\"down\"") >= 0) moveDown = true;
  else if (msg.indexOf("\"dir\":\"left\"") >= 0) moveLeft = true;
  else if (msg.indexOf("\"dir\":\"right\"") >= 0) moveRight = true;
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  (void)num;
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("WS client connected");
      break;
    case WStype_DISCONNECTED:
      Serial.println("WS client disconnected");
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
  // NOTE: WebSocketsServer listens on a dedicated port.
  // If your app currently uses ws://<host>/ws/manual on port 80,
  // change makeWsUrl() to ws://<host>:81 for this sketch.
  wsServer.begin();
  wsServer.onEvent(onWsEvent);
}

void applyManualMovement() {
  if (mode != "manual") return;

  uint32_t now = millis();
  if (now - lastManualStepMs < MANUAL_STEP_INTERVAL_MS) return;
  lastManualStepMs = now;

  int pan = currentPan;
  int tilt = currentTilt;

  if (moveLeft) pan -= MANUAL_STEP_DEG;
  if (moveRight) pan += MANUAL_STEP_DEG;
  if (moveUp) tilt += MANUAL_STEP_DEG;
  if (moveDown) tilt -= MANUAL_STEP_DEG;

  if (pan != currentPan || tilt != currentTilt) {
    writeServos(pan, tilt);
  }
}

void setup() {
  Serial.begin(115200);

  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);
  panServo.attach(SERVO_PAN_PIN, SERVO_MIN_US, SERVO_MAX_US);
  tiltServo.attach(SERVO_TILT_PIN, SERVO_MIN_US, SERVO_MAX_US);
  writeServos(PAN_CENTER, TILT_CENTER);

  WiFi.mode(WIFI_AP);
  bool started = WiFi.softAP(AP_SSID, AP_PASSWORD);
  delay(100);

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("AP started=%s SSID=%s IP=%s\n", started ? "true" : "false", AP_SSID, apIP.toString().c_str());

  setupRestApi();
  setupWebSocket();

  Serial.println("REST ready on port 80");
  Serial.println("WebSocket ready on port 81");
}

void loop() {
  server.handleClient();
  wsServer.loop();
  applyManualMovement();
}
