/*
  XIAO ESP32-C3 Jetson register test

  Use this first to test ESP-NET and the Jetson device gateway without any
  Grove sensors attached.

  Expected Jetson checks:
    curl http://10.42.0.1:8080/api/devices
    curl http://10.42.0.1:8080/api/sensors/c3-register-test-01/latest
*/

#include <HTTPClient.h>
#include <WiFi.h>

const char *WIFI_SSID = "ESP-NET";
const char *WIFI_PASSWORD = "change-this-password";

const char *DEVICE_ID = "c3-register-test-01";
const char *DEVICE_TYPE = "esp32-c3-test";
const char *FIRMWARE_NAME = "jetson_register_test";

const char *REGISTRY_URL = "http://10.42.0.1:8080/api/devices/register";
const char *READING_URL = "http://10.42.0.1:8080/api/sensors/c3-register-test-01/readings";

const uint32_t WIFI_TIMEOUT_MS = 30000;
const uint32_t REGISTER_INTERVAL_MS = 60000;
const uint32_t READING_INTERVAL_MS = 5000;

#ifdef LED_BUILTIN
const int STATUS_LED_PIN = LED_BUILTIN;
#else
const int STATUS_LED_PIN = -1;
#endif
const bool STATUS_LED_ACTIVE_LOW = true;

static uint32_t lastRegisterMs = 0;
static uint32_t lastReadingMs = 0;
static uint32_t bootMs = 0;

static void statusLed(bool on) {
  if (STATUS_LED_PIN < 0) {
    return;
  }
  digitalWrite(STATUS_LED_PIN, (on ^ STATUS_LED_ACTIVE_LOW) ? HIGH : LOW);
}

static String macAddress() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char out[18];
  snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(out);
}

static bool connectWifi() {
  statusLed(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to %s", WIFI_SSID);
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi failed, status=%d\n", WiFi.status());
    statusLed(false);
    return false;
  }

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("MAC: ");
  Serial.println(macAddress());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
  statusLed(false);
  return true;
}

static bool registerWithJetson() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  statusLed(true);
  HTTPClient http;
  http.begin(REGISTRY_URL);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  body += "\"device_type\":\"" + String(DEVICE_TYPE) + "\",";
  body += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  body += "\"mac\":\"" + macAddress() + "\",";
  body += "\"firmware\":\"" + String(FIRMWARE_NAME) + "\",";
  body += "\"capabilities\":[\"wifi-test\",\"registration-test\",\"heartbeat\"],";
  body += "\"metadata\":{";
  body += "\"board\":\"XIAO_ESP32C3\",";
  body += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\"";
  body += "}}";

  int code = http.POST(body);
  String response = http.getString();
  Serial.printf("Registry POST: %d\n", code);
  if (response.length() > 0) {
    Serial.println(response);
  }
  http.end();
  statusLed(false);
  return code >= 200 && code < 300;
}

static bool pushTestReading() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  statusLed(true);
  HTTPClient http;
  http.begin(READING_URL);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"reading\":{";
  body += "\"uptime_ms\":" + String(millis() - bootMs) + ",";
  body += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  body += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  body += "}}";

  int code = http.POST(body);
  String response = http.getString();
  Serial.printf("Reading POST: %d\n", code);
  if (response.length() > 0) {
    Serial.println(response);
  }
  http.end();
  statusLed(false);
  return code >= 200 && code < 300;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  bootMs = millis();
  if (STATUS_LED_PIN >= 0) {
    pinMode(STATUS_LED_PIN, OUTPUT);
    statusLed(false);
  }

  Serial.println("XIAO ESP32-C3 Jetson register test");
  connectWifi();
  registerWithJetson();
  pushTestReading();
  lastRegisterMs = millis();
  lastReadingMs = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  if (millis() - lastRegisterMs >= REGISTER_INTERVAL_MS) {
    lastRegisterMs = millis();
    registerWithJetson();
  }

  if (millis() - lastReadingMs >= READING_INTERVAL_MS) {
    lastReadingMs = millis();
    pushTestReading();
  }
}
