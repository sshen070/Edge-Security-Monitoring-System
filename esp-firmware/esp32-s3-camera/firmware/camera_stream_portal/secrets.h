#pragma once

// Local Wi-Fi credentials for the camera firmware.
// Replace these placeholders before flashing. Do not commit real credentials.
const char *WIFI_SSID = "yourWifiName";
const char *WIFI_PASSWORD = "yourWifiPassword";

// Jetson Orin Nano registry settings.
const char *DEVICE_ID = "camera-front-01";
const char *DEVICE_TYPE = "esp32-s3-camera";
const char *FIRMWARE_NAME = "camera_stream_portal";
const char *DEVICE_REGISTRY_URL = "http://10.42.0.1:8080/api/devices/register";
