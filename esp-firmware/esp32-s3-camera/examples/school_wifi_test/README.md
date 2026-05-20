# School Wi-Fi Test Sketch

Use this sketch to debug school Wi-Fi before changing the camera firmware.

Open:

```text
esp-firmware/esp32-s3-camera/examples/school_wifi_test/school_wifi_test.ino
```

## What It Does

The sketch:

- scans nearby Wi-Fi networks
- prints the ESP32-S3 station MAC address
- attempts to connect to the configured SSID
- prints Wi-Fi status changes
- prints IP, gateway, DNS, and RSSI if connected

## Configure

At the top of `school_wifi_test.ino`, set:

```cpp
#define SCHOOL_WIFI_MODE WIFI_MODE_ENTERPRISE
```

Use `WIFI_MODE_ENTERPRISE` for `eduroam` / WPA2 Enterprise.

Use `WIFI_MODE_PERSONAL` for a normal password Wi-Fi network or phone hotspot.

For eduroam, update:

```cpp
const char *WIFI_SSID = "eduroam";
const char *EAP_OUTER_IDENTITY = "anonymous@ucr.edu";
const char *EAP_USERNAME = "yourNetID@ucr.edu";
const char *EAP_PASSWORD = "yourCampusPassword";
```

Some schools want `EAP_USERNAME` as only the NetID, not the full email. Try both:

```cpp
const char *EAP_USERNAME = "yourNetID";
```

## Run

1. Flash the sketch.
2. Open Serial Monitor at `115200` baud.
3. Check whether the SSID appears in the scan.
4. Watch the connection status and final failure reason.

## Common Failure Causes

- Wrong username format, such as `netid` vs `netid@school.edu`.
- Expired campus password.
- Device MAC must be registered with campus IT.
- Network blocks embedded/IoT devices.
- Certificate validation is required by campus policy.
- You are on 5 GHz-only Wi-Fi. ESP32-S3 needs 2.4 GHz.

## After It Works

Copy the working Wi-Fi method and credential format back into:

```text
esp-firmware/esp32-s3-camera/firmware/esp32s3_webrtc_copy_20260513102720/esp32s3_webrtc_copy_20260513102720.ino
```
