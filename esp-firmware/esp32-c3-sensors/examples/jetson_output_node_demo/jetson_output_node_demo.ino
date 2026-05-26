/*
  XIAO ESP32-C3 Jetson output node demo

  Grove wiring:
    LED:    D10, or use the XIAO built-in LED if available
    Buzzer: D1
    Servo:  D2 signal, external 5V recommended for larger servos

  The node connects to the Jetson Orin Nano ESP-NET AP and registers with:
    http://10.42.0.1:8080/api/devices/register
*/

#include <HTTPClient.h>
#include <WiFi.h>

const char *WIFI_SSID = "ESP-NET";
const char *WIFI_PASSWORD = "change-this-password";
const char *REGISTRY_URL = "http://10.42.0.1:8080/api/devices/register";

const char *DEVICE_ID = "output-node-01";
const char *DEVICE_TYPE = "esp32-c3-output";
const char *FIRMWARE_NAME = "jetson_output_node_demo";

#ifdef LED_BUILTIN
const int LED_PIN = LED_BUILTIN;
#else
const int LED_PIN = D10;
#endif

const int BUZZER_PIN = D1;
const int SERVO_PIN = D2;

const uint32_t WIFI_TIMEOUT_MS = 30000;
const uint32_t REGISTER_INTERVAL_MS = 60000;
const uint32_t STEP_INTERVAL_MS = 2000;
const uint32_t SERVO_FRAME_US = 20000;

static uint32_t lastRegisterMs = 0;
static uint32_t lastStepMs = 0;
static uint32_t lastServoFrameUs = 0;
static int servoPulseUs = 1500;
static bool ledOn = false;
static bool buzzerOn = false;

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

  Serial.print("IP: ");
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
  body += "\"capabilities\":[\"led\",\"buzzer\",\"servo\"],";
  body += "\"metadata\":{";
  body += "\"led_pin\":\"D10_or_builtin\",";
  body += "\"buzzer_pin\":\"D1\",";
  body += "\"servo_pin\":\"D2\"";
  body += "}}";

  int code = http.POST(body);
  Serial.printf("Registry POST: %d\n", code);
  http.end();
  return code >= 200 && code < 300;
}

static void setServoAngle(int degrees) {
  degrees = constrain(degrees, 0, 180);
  servoPulseUs = map(degrees, 0, 180, 500, 2500);
}

static void updateServoPulse() {
  uint32_t now = micros();
  if (now - lastServoFrameUs >= SERVO_FRAME_US) {
    lastServoFrameUs = now;
    digitalWrite(SERVO_PIN, HIGH);
    delayMicroseconds(servoPulseUs);
    digitalWrite(SERVO_PIN, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  setServoAngle(90);

  Serial.println("XIAO ESP32-C3 Jetson output node demo");
  connectWifi();
  registerWithJetson();
  lastRegisterMs = millis();
}

void loop() {
  updateServoPulse();

  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
  }

  if (millis() - lastRegisterMs >= REGISTER_INTERVAL_MS) {
    lastRegisterMs = millis();
    registerWithJetson();
  }

  if (millis() - lastStepMs >= STEP_INTERVAL_MS) {
    lastStepMs = millis();
    ledOn = !ledOn;
    buzzerOn = !buzzerOn;
    digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);

    static int angleIndex = 0;
    const int angles[] = {0, 90, 180, 90};
    setServoAngle(angles[angleIndex]);
    angleIndex = (angleIndex + 1) % 4;

    Serial.print("led=");
    Serial.print(ledOn ? "on" : "off");
    Serial.print(" buzzer=");
    Serial.print(buzzerOn ? "on" : "off");
    Serial.print(" servo_pulse_us=");
    Serial.print(servoPulseUs);
    Serial.print(" ip=");
    Serial.println(WiFi.localIP());
  }
}
