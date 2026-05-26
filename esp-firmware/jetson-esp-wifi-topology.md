# Jetson ESP WiFi Topology

## Goal

Avoid depending on campus WiFi for ESP32-C3 and ESP32-S3 devices.

The Jetson Orin Nano acts as the stable bridge:

- native Jetson WiFi connects to the outside network
- USB WiFi dongle hosts a private ESP-only access point
- ESP devices join the private AP automatically
- each ESP registers itself with a small service on the Jetson
- the Jetson stores device identity and current IP in a local database
- other software queries the Jetson API instead of guessing ESP IP addresses

## Topology

```text
Internet / campus / hotspot
        |
        | Jetson native WiFi
        v
Jetson Orin Nano
  - Docker services
  - device gateway on port 8080
  - gateway container uses host networking
  - SQLite device/sensor DB
  - registration, sensor, and camera proxy API
  - USB WiFi dongle as ESP AP
        |
        | private ESP WiFi network
        v
ESP32-C3 sensors + ESP32-S3 cameras
```

## Jetson Responsibilities

The Jetson should own the private ESP network.

Planned services:

- WiFi AP service using the USB dongle
- DHCP for ESP clients
- local database for device registry and sensor readings
- single HTTP device gateway for registration, lookup, camera proxying, and sensor readings
- optional dashboard/debug endpoint

The Jetson Docker compose uses host networking for the device gateway. This is
intentional: the gateway must reach ESP camera streams on `10.42.0.0/24` and
serve clients through the Jetson's upstream network IP at the same time.

Suggested private network:

```text
SSID: ESP-NET
Subnet: 10.42.0.0/24
Jetson AP IP: 10.42.0.1
API base URL: http://10.42.0.1:8080
Developer index: http://10.42.0.1:8080/
Camera API: http://10.42.0.1:8080/api/cameras
Sensor API: http://10.42.0.1:8080/api/sensors
```

## Setup Method

Use this order on the Jetson Orin Nano.

### 1. Pick Interfaces

Check interface names:

```sh
nmcli dev status
ip link
```

Expected shape:

```text
wlan0  upstream WiFi or internet path
wlan1  USB WiFi dongle for ESP-NET
```

The exact names may differ. Use the USB dongle interface for the AP commands below.

### 2. Start ESP-NET

Recommended scripted setup from the repo root:

```sh
AP_IFACE=wlx503eaace2bc7 AP_PASSWORD='change-this-password' ./jetson-device-gateway/scripts/setup_jetson_orin.sh
```

The script installs the Realtek USB WiFi driver, creates the NetworkManager hotspot, sets `10.42.0.1/24`, and starts the device gateway.

Manual setup:

First try NetworkManager hotspot mode:

```sh
sudo nmcli dev wifi hotspot ifname wlan1 ssid ESP-NET password 'change-this-password'
```

Then set the AP address to the expected gateway IP if NetworkManager did not assign it:

```sh
sudo nmcli con modify Hotspot ipv4.addresses 10.42.0.1/24 ipv4.method shared
sudo nmcli con up Hotspot
```

Verify:

```sh
ip addr show wlan1
nmcli dev wifi list ifname wlan1
```

If NetworkManager hotspot mode is unreliable with the dongle, use host-side `hostapd` + `dnsmasq` instead. Keep AP/DHCP on the host, not inside Docker.

### 3. Start Device Gateway

From the repo root:

```sh
docker compose -f docker-compose.jetson.yml up --build
```

Verify:

```sh
curl http://10.42.0.1:8080/
curl http://10.42.0.1:8080/health
curl http://10.42.0.1:8080/api/devices
```

### 4. Flash ESP Devices

Configure each ESP sketch to use:

```cpp
const char *WIFI_SSID = "ESP-NET";
const char *WIFI_PASSWORD = "change-this-password";
```

Flash the S3 camera sketch:

```text
esp-firmware/esp32-s3-camera/firmware/camera_stream_portal/camera_stream_portal.ino
```

Flash C3 demos from:

```text
esp-firmware/esp32-c3-sensors/examples/
```

### 5. Check Devices

After ESPs boot:

```sh
curl http://10.42.0.1:8080/api/devices
curl http://10.42.0.1:8080/api/cameras
curl http://10.42.0.1:8080/api/sensors
```

Use stable URLs from other services:

```text
http://10.42.0.1:8080/api/cameras/camera-front-01/stream
http://10.42.0.1:8080/api/cameras/camera-front-01/capture
http://10.42.0.1:8080/api/sensors/sensor-node-01/latest
```

## ESP Responsibilities

Each ESP connects to the Jetson-hosted AP, then registers itself.

Registration payload should include:

```json
{
  "device_id": "camera-front-01",
  "device_type": "esp32-s3-camera",
  "ip": "10.42.0.23",
  "mac": "AA:BB:CC:DD:EE:FF",
  "firmware": "esp32s3-camera",
  "capabilities": ["camera", "stream", "capture"]
}
```

For ESP32-C3 sensor nodes, capabilities may look like:

```json
{
  "device_id": "sensor-left-01",
  "device_type": "esp32-c3-sensor",
  "ip": "10.42.0.34",
  "mac": "AA:BB:CC:DD:EE:11",
  "firmware": "esp32c3-sensors",
  "capabilities": ["imu", "distance", "telemetry"]
}
```

## API Draft

Minimum endpoints:

```text
POST /api/devices/register
GET  /api/devices
GET  /api/devices/:device_id
POST /api/devices/:device_id/heartbeat
```

Camera endpoints:

```text
GET /api/cameras
GET /api/cameras/:device_id
GET /api/cameras/:device_id/portal
GET /api/cameras/:device_id/viewer
GET /api/cameras/:device_id/status
GET /api/cameras/:device_id/control
GET /api/cameras/:device_id/capture
GET /api/cameras/:device_id/stream
```

Sensor endpoints:

```text
POST /api/sensors/:device_id/readings
GET  /api/sensors
GET  /api/sensors/:device_id/latest
GET  /api/sensors/:device_id/readings
```

The registration endpoint should upsert by `device_id` or `mac`, then store:

- device ID
- device type
- IP address
- MAC address
- firmware name/version
- capabilities
- last seen timestamp

## ESP Boot Flow

```text
1. Boot ESP.
2. Connect to ESP-NET.
3. Read local IP from WiFi.localIP().
4. POST registration payload to Jetson device gateway.
5. Keep normal firmware running.
6. Camera nodes expose `/capture` and `:81/stream` for the device gateway camera API.
7. Sensor nodes push readings to the device gateway sensor API.
8. Send periodic heartbeat or re-register after reconnect.
```

## Jetson Boot Flow

```text
1. Bring up native WiFi for upstream network.
2. Bring up USB WiFi dongle as AP.
3. Start DHCP/DNS for ESP network.
4. Start Docker services.
5. API waits for ESP registrations.
```

## Docker Plan

First pass can be a small Compose stack:

```text
jetson-device-gateway
```

The WiFi router/AP layer should stay on the host instead of inside Docker because host networking, DHCP, and WiFi driver control are cleaner outside containers.

## Notes

- Keep ESP WiFi credentials in local `secrets.h` files, not committed source.
- Prefer static `device_id` values in firmware presets so devices are easy to identify.
- The Jetson should treat ESP IPs as dynamic and rely on heartbeats/registration refreshes.
- The ESP private AP avoids school WPA2 Enterprise issues and makes local discovery deterministic.
