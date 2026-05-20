/*
  ESP32-S3 school Wi-Fi connection test

  Use this sketch before debugging the camera firmware. It only scans Wi-Fi,
  tries to join the configured network, and prints detailed status to Serial.

  1. Open this sketch in Arduino IDE.
  2. Set SCHOOL_WIFI_MODE below:
       WIFI_MODE_ENTERPRISE for eduroam / WPA2 Enterprise
       WIFI_MODE_PERSONAL   for normal password Wi-Fi / hotspot
  3. Fill the credentials below.
  4. Open Serial Monitor at 115200 baud.
*/

#include <WiFi.h>
#include <string.h>
#include "esp_wifi.h"

#if __has_include("esp_eap_client.h")
#include "esp_eap_client.h"
#define USE_ESP_EAP_CLIENT 1
#else
#include "esp_wpa2.h"
#define USE_ESP_EAP_CLIENT 0
#endif

#define WIFI_MODE_PERSONAL   0
#define WIFI_MODE_ENTERPRISE 1

#define SCHOOL_WIFI_MODE WIFI_MODE_ENTERPRISE

// For eduroam, the SSID is usually "eduroam".
const char *WIFI_SSID = "eduroam";

// For WPA2 Enterprise:
// - EAP_OUTER_IDENTITY is the anonymous/outer identity. "anonymous@school.edu" is common.
// - EAP_USERNAME is your login identity. Some schools want netid@school.edu, others want only netid.
// - EAP_PASSWORD is your campus password.
const char *EAP_OUTER_IDENTITY = "anonymous@ucr.edu";
const char *EAP_USERNAME = "yourNetID@ucr.edu";
const char *EAP_PASSWORD = "yourCampusPassword";

// For normal WPA/WPA2 Personal networks.
const char *WIFI_PASSWORD = "yourWifiPassword";

const uint32_t CONNECT_TIMEOUT_MS = 45000;

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
  Serial.printf("STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void scanNetworks() {
  Serial.println();
  Serial.println("Scanning for Wi-Fi networks...");
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

static void configureEnterprise() {
  Serial.println("Configuring WPA2 Enterprise credentials...");

#if USE_ESP_EAP_CLIENT
  esp_eap_client_set_identity((const unsigned char *)EAP_OUTER_IDENTITY, strlen(EAP_OUTER_IDENTITY));
  esp_eap_client_set_username((const unsigned char *)EAP_USERNAME, strlen(EAP_USERNAME));
  esp_eap_client_set_password((const unsigned char *)EAP_PASSWORD, strlen(EAP_PASSWORD));
  esp_wifi_sta_enterprise_enable();
#else
  esp_wifi_sta_wpa2_ent_set_identity((const unsigned char *)EAP_OUTER_IDENTITY, strlen(EAP_OUTER_IDENTITY));
  esp_wifi_sta_wpa2_ent_set_username((const unsigned char *)EAP_USERNAME, strlen(EAP_USERNAME));
  esp_wifi_sta_wpa2_ent_set_password((const unsigned char *)EAP_PASSWORD, strlen(EAP_PASSWORD));
  esp_wifi_sta_wpa2_ent_enable();
#endif

  Serial.printf("SSID: %s\n", WIFI_SSID);
  Serial.printf("EAP outer identity: %s\n", EAP_OUTER_IDENTITY);
  Serial.printf("EAP username: %s\n", EAP_USERNAME);
}

static bool connectWifi() {
  WiFi.disconnect(true, true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  printMac();
  scanNetworks();

#if SCHOOL_WIFI_MODE == WIFI_MODE_ENTERPRISE
  configureEnterprise();
  WiFi.begin(WIFI_SSID);
#else
  Serial.printf("Connecting to personal Wi-Fi SSID: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif

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
      Serial.println("Connected.");
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
  Serial.println("ESP32-S3 school Wi-Fi test");
  Serial.printf("Arduino core supports esp_eap_client.h: %s\n", USE_ESP_EAP_CLIENT ? "yes" : "no");

  bool connected = connectWifi();
  if (!connected) {
    Serial.println();
    Serial.println("Things to verify:");
    Serial.println("  - SSID is visible in the scan.");
    Serial.println("  - EAP identity/username format matches the school requirement.");
    Serial.println("  - Campus password is current and not expired.");
    Serial.println("  - Device MAC is allowed if your school requires registration.");
    Serial.println("  - Some campus networks require certificate validation not included in this test.");
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
