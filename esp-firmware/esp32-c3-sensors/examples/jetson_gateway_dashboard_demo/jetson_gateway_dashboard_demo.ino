/*
  XIAO ESP32-C3 Jetson gateway/dashboard demo

  This board still connects to the Jetson Orin Nano ESP-NET AP. It registers
  itself with the Jetson registry, then serves a tiny local status page on its
  own ESP IP for quick browser checks.
*/

#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>

const char *WIFI_SSID = "ESP-NET";
const char *WIFI_PASSWORD = "change-this-password";
const char *REGISTRY_URL = "http://10.42.0.1:8080/api/devices/register";

const char *DEVICE_ID = "gateway-dashboard-01";
const char *DEVICE_TYPE = "esp32-c3-dashboard";
const char *FIRMWARE_NAME = "jetson_gateway_dashboard_demo";

const uint32_t WIFI_TIMEOUT_MS = 30000;
const uint32_t REGISTER_INTERVAL_MS = 60000;

WebServer server(80);
static uint32_t lastRegisterMs = 0;
static uint32_t bootMs = 0;
static int registerCount = 0;
static int lastRegisterCode = 0;

static String macAddress() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char out[18];
  snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(out);
}

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to %s", WIFI_SSID);
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("Wi-Fi failed, status=%d\n", WiFi.status());
    return false;
  }

  Serial.print("Dashboard URL: http://");
  Serial.println(WiFi.localIP());
  Serial.print("MAC: ");
  Serial.println(macAddress());
  return true;
}

static bool registerWithJetson() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  http.begin(REGISTRY_URL);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  body += "\"device_type\":\"" + String(DEVICE_TYPE) + "\",";
  body += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  body += "\"mac\":\"" + macAddress() + "\",";
  body += "\"firmware\":\"" + String(FIRMWARE_NAME) + "\",";
  body += "\"capabilities\":[\"wifi-status\",\"local-dashboard\"],";
  body += "\"metadata\":{";
  body += "\"dashboard\":\"http://" + WiFi.localIP().toString() + "\"";
  body += "}}";

  int code = http.POST(body);
  lastRegisterCode = code;
  if (code >= 200 && code < 300) {
    registerCount++;
  }
  Serial.printf("Registry POST: %d\n", code);
  http.end();
  return code >= 200 && code < 300;
}

static String statusJson() {
  String out = "{";
  out += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  out += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  out += "\"mac\":\"" + macAddress() + "\",";
  out += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  out += "\"uptime_ms\":" + String(millis() - bootMs) + ",";
  out += "\"last_register_code\":" + String(lastRegisterCode) + ",";
  out += "\"register_count\":" + String(registerCount);
  out += "}";
  return out;
}

static void handleRoot() {
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32-C3 Dashboard</title></head><body>";
  html += "<h1>ESP32-C3 Dashboard</h1>";
  html += "<pre>";
  html += statusJson();
  html += "</pre>";
  html += "<p><a href='/status'>JSON status</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

static void handleStatus() {
  server.send(200, "application/json", statusJson());
}

static void startServer() {
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("HTTP dashboard started");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  bootMs = millis();

  Serial.println("XIAO ESP32-C3 Jetson gateway/dashboard demo");
  connectWifi();
  registerWithJetson();
  startServer();
  lastRegisterMs = millis();
}

void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  if (millis() - lastRegisterMs >= REGISTER_INTERVAL_MS) {
    lastRegisterMs = millis();
    registerWithJetson();
  }
}
