# ESP32-C3 Hotspot Wi-Fi Test

Open `hotspot_wifi_test.ino`, set:

```cpp
const char *HOTSPOT_SSID = "yourHotspotName";
const char *HOTSPOT_PASSWORD = "yourHotspotPassword";
```

Then flash the ESP32-C3 and open Serial Monitor at `115200` baud.

The sketch prints:

- ESP32-C3 station MAC address
- nearby Wi-Fi scan results
- connection status changes
- IP, gateway, DNS, and RSSI when connected

If it fails, check that the hotspot has 2.4 GHz or compatibility mode enabled. ESP32-C3 cannot connect to 5 GHz-only Wi-Fi.
