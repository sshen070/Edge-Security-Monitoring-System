/*
  ESP32-C3 hotspot Wi-Fi connection test

  Fill HOTSPOT_SSID and HOTSPOT_PASSWORD, flash the sketch, then open Serial
  Monitor at 115200 baud. The board scans nearby networks, connects to the
  hotspot, and prints IP/RSSI status.
*/

#include <WiFi.h>

const char *HOTSPOT_SSID = "yourHotspotName";
const char *HOTSPOT_PASSWORD = "yourHotspotPassword";

const uint32_t CONNECT_TIMEOUT_MS = 30000;

static const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

static void printMac() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("ESP32-C3 STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void scanNetworks() {
  Serial.println();
  Serial.println("Scanning nearby Wi-Fi...");
  int count = WiFi.scanNetworks();
  if (count <= 0) {
    Serial.println("No networks found.");
    return;
  }

  Serial.printf("Found %d network(s):\n", count);
  for (int i = 0; i < count; i++) {
    Serial.printf(
      "  %2d: %-32s RSSI %4d dBm  CH %2d  ENC %d\n",
      i + 1,
      WiFi.SSID(i).c_str(),
      WiFi.RSSI(i),
      WiFi.channel(i),
      WiFi.encryptionType(i)
    );
  }
}

static bool connectHotspot() {
  WiFi.disconnect(true, true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  printMac();
  scanNetworks();

  Serial.println();
  Serial.printf("Connecting to hotspot: %s\n", HOTSPOT_SSID);
  WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASSWORD);

  uint32_t started = millis();
  wl_status_t lastStatus = WL_IDLE_STATUS;
  while (millis() - started < CONNECT_TIMEOUT_MS) {
    wl_status_t status = WiFi.status();
    if (status != lastStatus) {
      Serial.printf("Wi-Fi status: %s (%d)\n", wifiStatusName(status), status);
      lastStatus = status;
    }
    if (status == WL_CONNECTED) {
      Serial.println();
      Serial.println("Connected to hotspot.");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      Serial.print("Gateway: ");
      Serial.println(WiFi.gatewayIP());
      Serial.print("DNS: ");
      Serial.println(WiFi.dnsIP());
      Serial.print("RSSI: ");
      Serial.println(WiFi.RSSI());
      return true;
    }
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("Connection timed out.");
  Serial.printf("Final Wi-Fi status: %s (%d)\n", wifiStatusName(WiFi.status()), WiFi.status());
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("ESP32-C3 hotspot Wi-Fi test");

  bool connected = connectHotspot();
  if (!connected) {
    Serial.println();
    Serial.println("Check that:");
    Serial.println("  - The hotspot is 2.4 GHz or compatibility mode is enabled.");
    Serial.println("  - SSID/password are exact.");
    Serial.println("  - The hotspot allows new devices.");
    Serial.println("  - The ESP32-C3 MAC is not blocked by the hotspot.");
  }
}

void loop() {
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 5000) {
    lastPrint = millis();
    Serial.printf(
      "Status: %s (%d), IP: %s, RSSI: %d\n",
      wifiStatusName(WiFi.status()),
      WiFi.status(),
      WiFi.localIP().toString().c_str(),
      WiFi.RSSI()
    );
  }
}
