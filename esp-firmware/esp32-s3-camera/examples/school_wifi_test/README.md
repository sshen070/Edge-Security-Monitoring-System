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

Use `WIFI_MODE_ENTERPRISE` for `UCR-SECURE`, `eduroam`, or another WPA2 Enterprise network.

Use `WIFI_MODE_PERSONAL` for a normal password Wi-Fi network or phone hotspot.

For UCR-SECURE or eduroam, you can test the Arduino PEAP helper:

```cpp
#define SCHOOL_ENTERPRISE_METHOD ENTERPRISE_METHOD_PEAP_BEGIN
```

If that returns `HANDSHAKE_TIMEOUT`, try the lower-level ESP-IDF calls:

```cpp
#define SCHOOL_ENTERPRISE_METHOD ENTERPRISE_METHOD_LOW_LEVEL
```

The sketch also locks the connection attempt to the strongest scanned AP by default:

```cpp
#define LOCK_TO_STRONGEST_AP 1
```

If the campus Wi-Fi controller does not like BSSID pinning, set it to `0` and retry.

For UCR-SECURE, update:

```cpp
const char *WIFI_SSID = "UCR-SECURE";
const char *EAP_OUTER_IDENTITY = "yourNetID";
const char *EAP_USERNAME = "yourNetID";
const char *EAP_PASSWORD = "yourCampusPassword";
```

For eduroam, use the eduroam SSID and try the full campus identity:

```cpp
const char *WIFI_SSID = "eduroam";
const char *EAP_OUTER_IDENTITY = "anonymous@ucr.edu";
const char *EAP_USERNAME = "yourNetID@ucr.edu";
```

## Reading The Failure

If the scan finds the SSID but the connection stays at `WL_DISCONNECTED`, the ESP32 is reaching the network but enterprise authentication is not finishing.

The sketch prints the best RSSI for the configured SSID. Treat anything weaker than about `-75 dBm` as suspicious. Move closer to an access point and retry before assuming the username/password method is wrong.

If the best scan RSSI is strong but the status RSSI later drops a lot, the ESP may be trying a weaker AP. Keep `LOCK_TO_STRONGEST_AP` enabled for that test.

The sketch also prints the last disconnect reason code and name. That line matters more than the generic `WL_DISCONNECTED` status for WPA2 Enterprise debugging.

`HANDSHAKE_TIMEOUT` means the ESP found the AP but did not complete WPA/enterprise authentication. Try `ENTERPRISE_METHOD_LOW_LEVEL`, then try `LOCK_TO_STRONGEST_AP 0` if the same reason repeats.

## Run

1. Flash the sketch.
2. Open Serial Monitor at `115200` baud.
3. Check whether the SSID appears in the scan.
4. Watch the connection status and final failure reason.

## Common Failure Causes

- Weak signal. The ESP32-S3 is 2.4 GHz only, and eduroam roaming can be flaky at low RSSI.
- Wrong username format, such as `netid` vs `netid@school.edu`.
- Expired campus password.
- Device MAC must be registered with campus IT.
- Network blocks embedded/IoT devices.
- Certificate validation is required by campus policy.
- You are on 5 GHz-only Wi-Fi. ESP32-S3 needs 2.4 GHz.

## After It Works

Copy the working Wi-Fi method and credential format back into:

```text
esp-firmware/esp32-s3-camera/firmware/camera_stream_portal/camera_stream_portal.ino
```
