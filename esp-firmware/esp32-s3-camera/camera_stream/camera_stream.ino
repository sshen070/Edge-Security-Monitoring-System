#include "esp_camera.h"
#include <WiFi.h>

const char* ssid = "NJSLAPTOP 5186";
const char* password = "aaaaaaaa";

WiFiServer server(80);

// ===== CAMERA PINS =====
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    10
#define SIOD_GPIO_NUM    40
#define SIOC_GPIO_NUM    39

#define Y9_GPIO_NUM      48
#define Y8_GPIO_NUM      11
#define Y7_GPIO_NUM      12
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      16
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM      17
#define Y2_GPIO_NUM      15
#define VSYNC_GPIO_NUM   38
#define HREF_GPIO_NUM    47
#define PCLK_GPIO_NUM    13

void startCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;

  esp_camera_init(&config);
}

void connectWiFi() {
  Serial.println("\nConnecting to WiFi...");
  Serial.printf("SSID: %s\n", ssid);

  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 15000; // 15 seconds

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");

    delay(500);

    if (millis() - startAttemptTime > timeout) {
      Serial.println("\n❌ WiFi connection timed out!");
      Serial.println("Check SSID/password or signal strength.");
      return;
    }
  }

  Serial.println("\n✅ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  connectWiFi();

  startCamera();
  server.begin();

  Serial.println("📷 Camera server started");
  Serial.println("Stream endpoint: /stream");
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  String req = client.readStringUntil('\r');
  client.flush();

  if (req.indexOf("/stream") >= 0) {

    Serial.println("Client connected to stream");

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    client.println();

    while (client.connected()) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) continue;

      client.println("--frame");
      client.println("Content-Type: image/jpeg");
      client.println("Content-Length: " + String(fb->len));
      client.println();
      client.write(fb->buf, fb->len);
      client.println();

      esp_camera_fb_return(fb);
    }

    Serial.println("Client disconnected");
    client.stop();
  }
}
