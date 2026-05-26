/*
  ESP32-S3 school Wi-Fi connection test

  Use this sketch before debugging the camera firmware. It only scans Wi-Fi,
  tries to join the configured network, and prints detailed status to Serial.

  1. Open this sketch in Arduino IDE.
  2. Set SCHOOL_WIFI_MODE below:
       WIFI_MODE_ENTERPRISE for UCR-SECURE / eduroam / WPA2 Enterprise
       WIFI_MODE_PERSONAL   for normal password Wi-Fi / hotspot
  3. For enterprise Wi-Fi, try ENTERPRISE_METHOD_PEAP_BEGIN first.
  4. Fill the credentials below.
  5. Open Serial Monitor at 115200 baud.
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

#define ENTERPRISE_METHOD_LOW_LEVEL  0
#define ENTERPRISE_METHOD_PEAP_BEGIN 1

#define SCHOOL_WIFI_MODE WIFI_MODE_ENTERPRISE

// Most campus WPA2 Enterprise networks use PEAP/MSCHAPv2. If this does not compile or connect,
// switch to ENTERPRISE_METHOD_LOW_LEVEL to test the older ESP-IDF calls.
#define SCHOOL_ENTERPRISE_METHOD ENTERPRISE_METHOD_LOW_LEVEL

// Locks the connection attempt to the strongest scanned AP for WIFI_SSID.
// Turn this off if the strongest AP is unstable or the campus controller rejects BSSID pinning.
#define LOCK_TO_STRONGEST_AP 1

// UCR's main secure network is "UCR-SECURE". You can also test "eduroam".
const char *WIFI_SSID = "UCR-SECURE";

// For WPA2 Enterprise:
// - UCR-SECURE Android guidance uses NetID for both identity fields.
// - eduroam commonly uses NetID@ucr.edu for EAP_USERNAME.
// - EAP_PASSWORD is your campus password.
const char *EAP_OUTER_IDENTITY = "yourNetID";
const char *EAP_USERNAME = "yourNetID";
const char *EAP_PASSWORD = "yourCampusPassword";

// For normal WPA/WPA2 Personal networks.
const char *WIFI_PASSWORD = "yourWifiPassword";

const uint32_t CONNECT_TIMEOUT_MS = 90000;
const int WEAK_RSSI_DBM = -75;

static int configuredSsidBestRssi = -127;
static int32_t configuredSsidBestChannel = 0;
static uint8_t configuredSsidBestBssid[6] = {0};
static bool configuredSsidHasBssid = false;
static uint8_t lastDisconnectReason = 0;

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

static const char *disconnectReasonName(uint8_t reason) {
  return WiFi.disconnectReasonName((wifi_err_reason_t)reason);
}

static void onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  lastDisconnectReason = info.wifi_sta_disconnected.reason;
  Serial.printf(
    "\nWi-Fi event: disconnected, reason %u (%s)\n",
    lastDisconnectReason,
    disconnectReasonName(lastDisconnectReason)
  );
}

static void onWifiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("\nWi-Fi event: connected to AP, waiting for IP...");
}

static void onWifiGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.print("\nWi-Fi event: got IP ");
  Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
}

static void printMac() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.printf("STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void scanNetworks() {
  Serial.println();
  Serial.println("Scanning for Wi-Fi networks...");
  configuredSsidBestRssi = -127;
  configuredSsidBestChannel = 0;
  configuredSsidHasBssid = false;
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
    if (WiFi.SSID(i) == WIFI_SSID && WiFi.RSSI(i) > configuredSsidBestRssi) {
      configuredSsidBestRssi = WiFi.RSSI(i);
      configuredSsidBestChannel = WiFi.channel(i);
      uint8_t *bssid = WiFi.BSSID(i);
      if (bssid != NULL) {
        memcpy(configuredSsidBestBssid, bssid, sizeof(configuredSsidBestBssid));
        configuredSsidHasBssid = true;
      }
    }
  }

  if (configuredSsidBestRssi == -127) {
    Serial.printf("Configured SSID \"%s\" was not found in the scan.\n", WIFI_SSID);
  } else {
    Serial.printf("Best RSSI for \"%s\": %d dBm\n", WIFI_SSID, configuredSsidBestRssi);
    if (configuredSsidHasBssid) {
      Serial.printf(
        "Best AP: %02X:%02X:%02X:%02X:%02X:%02X on channel %d\n",
        configuredSsidBestBssid[0],
        configuredSsidBestBssid[1],
        configuredSsidBestBssid[2],
        configuredSsidBestBssid[3],
        configuredSsidBestBssid[4],
        configuredSsidBestBssid[5],
        configuredSsidBestChannel
      );
    }
    if (configuredSsidBestRssi < WEAK_RSSI_DBM) {
      Serial.println("Signal is weak. Move closer to an AP before trusting auth failures.");
    }
  }
}

static void printEnterpriseConfig() {
  Serial.printf("SSID: %s\n", WIFI_SSID);
  Serial.printf("EAP outer identity: %s\n", EAP_OUTER_IDENTITY);
  Serial.printf("EAP username: %s\n", EAP_USERNAME);
}

static const uint8_t *selectedBssid() {
#if LOCK_TO_STRONGEST_AP
  return configuredSsidHasBssid ? configuredSsidBestBssid : NULL;
#else
  return NULL;
#endif
}

static int32_t selectedChannel() {
#if LOCK_TO_STRONGEST_AP
  return configuredSsidHasBssid ? configuredSsidBestChannel : 0;
#else
  return 0;
#endif
}

static void printApLockMode() {
#if LOCK_TO_STRONGEST_AP
  if (configuredSsidHasBssid) {
    Serial.println("AP lock: enabled, using strongest scanned BSSID/channel");
  } else {
    Serial.println("AP lock: enabled, but no matching BSSID was found");
  }
#else
  Serial.println("AP lock: disabled");
#endif
}

static void configureEnterpriseLowLevel() {
  Serial.println("Configuring WPA2 Enterprise credentials with low-level API...");
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
  printEnterpriseConfig();
}

static bool connectWifi() {
  WiFi.disconnect(true, true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  printMac();
  scanNetworks();

#if SCHOOL_WIFI_MODE == WIFI_MODE_ENTERPRISE
  printEnterpriseConfig();
  printApLockMode();
#if SCHOOL_ENTERPRISE_METHOD == ENTERPRISE_METHOD_PEAP_BEGIN
  Serial.println("Connecting with Arduino PEAP helper: WiFi.begin(ssid, WPA2_AUTH_PEAP, identity, username, password)");
  WiFi.begin(
    WIFI_SSID,
    WPA2_AUTH_PEAP,
    EAP_OUTER_IDENTITY,
    EAP_USERNAME,
    EAP_PASSWORD,
    NULL,
    NULL,
    NULL,
    -1,
    selectedChannel(),
    selectedBssid()
  );
#else
  configureEnterpriseLowLevel();
  WiFi.begin(WIFI_SSID, NULL, selectedChannel(), selectedBssid());
#endif
#else
  Serial.printf("Connecting to personal Wi-Fi SSID: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, selectedChannel(), selectedBssid());
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
  if (lastDisconnectReason != 0) {
    Serial.printf("Last disconnect reason: %u (%s)\n", lastDisconnectReason, disconnectReasonName(lastDisconnectReason));
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("ESP32-S3 school Wi-Fi test");
  WiFi.onEvent(onWifiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(onWifiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(onWifiGotIp, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  Serial.printf("Arduino core supports esp_eap_client.h: %s\n", USE_ESP_EAP_CLIENT ? "yes" : "no");
#if SCHOOL_WIFI_MODE == WIFI_MODE_ENTERPRISE
  Serial.printf(
    "Enterprise method: %s\n",
    SCHOOL_ENTERPRISE_METHOD == ENTERPRISE_METHOD_PEAP_BEGIN ? "Arduino PEAP helper" : "low-level ESP-IDF calls"
  );
#endif

  bool connected = connectWifi();
  if (!connected) {
    Serial.println();
    Serial.println("Things to verify:");
    Serial.println("  - SSID is visible in the scan.");
    Serial.println("  - EAP identity/username format matches the school requirement.");
    Serial.println("  - Campus password is current and not expired.");
    Serial.println("  - Device MAC is allowed if your school requires registration.");
    Serial.println("  - Some campus networks require certificate validation not included in this test.");
    Serial.println("  - If PEAP helper fails, try ENTERPRISE_METHOD_LOW_LEVEL, or the reverse.");
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
