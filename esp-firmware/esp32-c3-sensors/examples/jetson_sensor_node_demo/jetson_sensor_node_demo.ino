/*
  XIAO ESP32-C3 Jetson sensor node demo

  Grove wiring:
    Light sensor: A0
    PIR motion:   D0
    I2C SDA:      D4
    I2C SCL:      D5

  The node connects to the Jetson Orin Nano ESP-NET AP and registers with:
    http://10.42.0.1:8080/api/devices/register
*/

#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>

const char *WIFI_SSID = "ESP-NET";
const char *WIFI_PASSWORD = "change-this-password";
const char *REGISTRY_URL = "http://10.42.0.1:8080/api/devices/register";
const char *SENSOR_READING_URL = "http://10.42.0.1:8080/api/sensors/sensor-node-01/readings";

const char *DEVICE_ID = "sensor-node-01";
const char *DEVICE_TYPE = "esp32-c3-sensor";
const char *FIRMWARE_NAME = "jetson_sensor_node_demo";

const int PIR_PIN = D0;
const int LIGHT_PIN = A0;
const int SDA_PIN = D4;
const int SCL_PIN = D5;

const uint32_t WIFI_TIMEOUT_MS = 30000;
const uint32_t REGISTER_INTERVAL_MS = 60000;
const uint32_t SAMPLE_INTERVAL_MS = 1000;
const uint32_t HEARTBEAT_PUSH_INTERVAL_MS = 60000;
const uint32_t EVENT_PUSH_MIN_INTERVAL_MS = 5000;
const int LIGHT_CHANGE_THRESHOLD = 50;

#ifdef LED_BUILTIN
const int STATUS_LED_PIN = LED_BUILTIN;
#else
const int STATUS_LED_PIN = -1;
#endif
const bool STATUS_LED_ACTIVE_LOW = true;

static uint32_t lastRegisterMs = 0;
static uint32_t lastSampleMs = 0;
static uint32_t lastPushMs = 0;
static int latestLightRaw = 0;
static int latestMotion = LOW;
static int lastPushedLightRaw = -1;
static int lastPushedMotion = -1;
static bool hasPushedReading = false;

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
    Serial.printf("Wi-Fi failed, status=%d\n", WiFi.status());
    statusLed(false);
    return false;
  }

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC: ");
  Serial.println(macAddress());
  statusLed(false);
  return true;
}

static void scanI2cOnce() {
  int devices = 0;
  Serial.println("I2C scan:");
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("  found 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
      devices++;
    }
  }
  if (devices == 0) {
    Serial.println("  no I2C devices found");
  }
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
  body += "\"capabilities\":[\"pir\",\"light\",\"i2c-scan\"],";
  body += "\"metadata\":{";
  body += "\"light_pin\":\"A0\",";
  body += "\"pir_pin\":\"D0\",";
  body += "\"i2c_sda\":\"D4\",";
  body += "\"i2c_scl\":\"D5\",";
  body += "\"sample_interval_ms\":" + String(SAMPLE_INTERVAL_MS) + ",";
  body += "\"heartbeat_push_interval_ms\":" + String(HEARTBEAT_PUSH_INTERVAL_MS) + ",";
  body += "\"event_push_min_interval_ms\":" + String(EVENT_PUSH_MIN_INTERVAL_MS) + ",";
  body += "\"light_change_threshold\":" + String(LIGHT_CHANGE_THRESHOLD);
  body += "}}";

  int code = http.POST(body);
  Serial.printf("Registry POST: %d\n", code);
  http.end();
  statusLed(false);
  return code >= 200 && code < 300;
}

static bool pushReadingToJetson() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  statusLed(true);
  HTTPClient http;
  http.begin(SENSOR_READING_URL);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"reading\":{";
  body += "\"light_raw\":" + String(latestLightRaw) + ",";
  body += "\"motion\":" + String(latestMotion == HIGH ? "true" : "false") + ",";
  body += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  body += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  body += "}}";

  int code = http.POST(body);
  Serial.printf("Device gateway sensor POST: %d\n", code);
  http.end();
  statusLed(false);
  bool ok = code >= 200 && code < 300;
  if (ok) {
    lastPushedLightRaw = latestLightRaw;
    lastPushedMotion = latestMotion;
    hasPushedReading = true;
  }
  return ok;
}

static bool shouldPushReading() {
  if (!hasPushedReading) {
    return lastPushMs == 0 || millis() - lastPushMs >= EVENT_PUSH_MIN_INTERVAL_MS;
  }

  uint32_t now = millis();
  if (latestMotion != lastPushedMotion) {
    return true;
  }

  if (abs(latestLightRaw - lastPushedLightRaw) >= LIGHT_CHANGE_THRESHOLD && now - lastPushMs >= EVENT_PUSH_MIN_INTERVAL_MS) {
    return true;
  }

  return now - lastPushMs >= HEARTBEAT_PUSH_INTERVAL_MS;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  if (STATUS_LED_PIN >= 0) {
    pinMode(STATUS_LED_PIN, OUTPUT);
    statusLed(false);
  }
  pinMode(PIR_PIN, INPUT);
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("XIAO ESP32-C3 Jetson sensor node demo");
  connectWifi();
  scanI2cOnce();
  registerWithJetson();
  lastRegisterMs = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  if (millis() - lastRegisterMs >= REGISTER_INTERVAL_MS) {
    lastRegisterMs = millis();
    registerWithJetson();
  }

  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();
    latestLightRaw = analogRead(LIGHT_PIN);
    latestMotion = digitalRead(PIR_PIN);

    Serial.print("light=");
    Serial.print(latestLightRaw);
    Serial.print(" motion=");
    Serial.print(latestMotion == HIGH ? "detected" : "none");
    Serial.print(" ip=");
    Serial.println(WiFi.localIP());
  }

  if (shouldPushReading()) {
    lastPushMs = millis();
    pushReadingToJetson();
  }
}
