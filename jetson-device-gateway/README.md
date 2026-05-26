# Jetson Device Gateway

Single local API for ESP32-C3 sensor nodes and ESP32-S3 camera nodes.

The Jetson Orin Nano hosts the ESP-only WiFi network on its USB WiFi dongle, then runs this service so ESP devices can register current IPs, publish sensor readings, and access camera streams through stable API URLs.

## Setup Order

1. Bring up `ESP-NET` on the Jetson USB WiFi dongle.
2. Start this gateway on port `8080`.
3. Flash ESP devices with the `ESP-NET` SSID/password.
4. Verify devices through `/api/devices`.
5. Use `/api/cameras/...` and `/api/sensors/...` from dashboards or backend services.

## One-Command Jetson Setup

From the repo root on the Jetson Orin Nano:

```sh
AP_PASSWORD='change-this-password' ./jetson-device-gateway/scripts/setup_jetson_orin.sh
```

If you know the USB WiFi interface name:

```sh
AP_IFACE=wlx503eaace2bc7 AP_PASSWORD='change-this-password' ./jetson-device-gateway/scripts/setup_jetson_orin.sh
```

The script:

- installs Jetson build/header packages
- installs the Realtek USB WiFi driver using `aircrack-ng/rtl8812au`
- creates or updates NetworkManager `Hotspot`
- assigns `10.42.0.1/24`
- starts `docker-compose.jetson.yml`
- prints API checks

Configurable environment variables:

```text
AP_SSID=ESP-NET
AP_PASSWORD=change-this-password
AP_IFACE=<auto-detected disconnected WiFi interface>
AP_IP=10.42.0.1
AP_CIDR=10.42.0.1/24
INSTALL_REALTEK_DRIVER=1
REALTEK_DRIVER_SOURCE=aircrack
```

Set `INSTALL_REALTEK_DRIVER=0` if the dongle driver is already working.

## Run Locally

```sh
cd jetson-device-gateway
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8080
```

## Run With Docker

```sh
cd jetson-device-gateway
docker compose up --build
```

From the repo root, run the Jetson service:

```sh
docker compose -f docker-compose.jetson.yml up --build
```

The SQLite database is stored at:

```text
jetson-device-gateway/data/devices.db
```

## Device Registry API

Health:

```sh
curl http://10.42.0.1:8080/health
```

Register or update a camera:

```sh
curl -X POST http://10.42.0.1:8080/api/devices/register \
  -H 'Content-Type: application/json' \
  -d '{
    "device_id": "camera-front-01",
    "device_type": "esp32-s3-camera",
    "ip": "10.42.0.23",
    "mac": "AA:BB:CC:DD:EE:FF",
    "firmware": "esp32s3-camera",
    "capabilities": ["camera", "stream", "capture"]
  }'
```

Register or update a sensor:

```sh
curl -X POST http://10.42.0.1:8080/api/devices/register \
  -H 'Content-Type: application/json' \
  -d '{
    "device_id": "sensor-left-01",
    "device_type": "esp32-c3-sensor",
    "ip": "10.42.0.34",
    "mac": "AA:BB:CC:DD:EE:11",
    "firmware": "esp32c3-sensors",
    "capabilities": ["imu", "distance", "telemetry"]
  }'
```

List devices:

```sh
curl http://10.42.0.1:8080/api/devices
```

Get one device:

```sh
curl http://10.42.0.1:8080/api/devices/camera-front-01
```

Heartbeat:

```sh
curl -X POST http://10.42.0.1:8080/api/devices/camera-front-01/heartbeat \
  -H 'Content-Type: application/json' \
  -d '{"ip": "10.42.0.23"}'
```

If `ip` is omitted, the API uses the request source IP.

## Camera API

```text
GET /api/cameras
GET /api/cameras/{device_id}
GET /api/cameras/{device_id}/portal
GET /api/cameras/{device_id}/status
GET /api/cameras/{device_id}/control
GET /api/cameras/{device_id}/capture
GET /api/cameras/{device_id}/stream
```

Examples:

```sh
curl http://10.42.0.1:8080/api/cameras
curl http://10.42.0.1:8080/api/cameras/camera-front-01/capture --output capture.jpg
```

Open the camera settings portal through the gateway:

```text
http://10.42.0.1:8080/api/cameras/camera-front-01/portal
```

Open the stream in a browser or video client:

```text
http://10.42.0.1:8080/api/cameras/camera-front-01/stream
```

Camera runtime settings pass through to the ESP:

```text
http://10.42.0.1:8080/api/cameras/camera-front-01/stream?fs=qvga&q=10
http://10.42.0.1:8080/api/cameras/camera-front-01/capture?framesize=vga&quality=12
```

## Sensor API

```text
POST   /api/sensors/{device_id}/readings
GET    /api/sensors
GET    /api/sensors/{device_id}/latest
GET    /api/sensors/{device_id}/readings
DELETE /api/sensors/{device_id}/readings
```

Post a reading:

```sh
curl -X POST http://10.42.0.1:8080/api/sensors/sensor-node-01/readings \
  -H 'Content-Type: application/json' \
  -d '{
    "reading": {
      "light_raw": 2400,
      "motion": true,
      "rssi": -48
    }
  }'
```

Read latest:

```sh
curl http://10.42.0.1:8080/api/sensors/sensor-node-01/latest
```

## Jetson WiFi AP

Keep the AP/router setup on the Jetson host, not inside Docker.

Target network:

```text
SSID: ESP-NET
Jetson AP IP: 10.42.0.1
ESP subnet: 10.42.0.0/24
Registry API: http://10.42.0.1:8080
Camera API: http://10.42.0.1:8080/api/cameras
Sensor API: http://10.42.0.1:8080/api/sensors
```

First pass on Jetson Linux / Ubuntu can use NetworkManager if the USB dongle supports AP mode:

```sh
nmcli dev status
sudo nmcli dev wifi hotspot ifname wlan1 ssid ESP-NET password 'change-this-password'
sudo nmcli con modify Hotspot ipv4.addresses 10.42.0.1/24 ipv4.method shared
sudo nmcli con up Hotspot
```

Then verify the Jetson AP address:

```sh
ip addr show wlan1
```

If NetworkManager is unreliable on the Jetson Orin Nano image, use `hostapd` + `dnsmasq` on the host instead.

## Service Checks

```sh
curl http://10.42.0.1:8080/health
curl http://10.42.0.1:8080/api/devices
curl http://10.42.0.1:8080/api/cameras
curl http://10.42.0.1:8080/api/sensors
```

## ESP Boot Flow

1. ESP connects to `ESP-NET`.
2. ESP reads its IP using `WiFi.localIP()`.
3. ESP posts to `http://10.42.0.1:8080/api/devices/register`.
4. Sensor ESPs post readings to `/api/sensors/{device_id}/readings`.
5. Camera ESPs expose `/capture` and `:81/stream`; this gateway proxies them through `/api/cameras/{device_id}`.
6. ESP sends periodic heartbeat after reconnect or every fixed interval.

Keep ESP WiFi credentials in local firmware secrets files, not committed source.
