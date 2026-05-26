# Team Responsibilities

## 1. Cloud Dashboard – Marc / Steven

Responsible for:
- Frontend UI
- Sensor charts
- Camera/device status
- Alert display

Folder:
- `cloud-dashboard/`

Consumes:
- Cloud backend APIs
- Shared schemas from `shared/schemas/`

---

## 2. Cloud Backend – Steven / Marc

Responsible for:
- APIs
- Database
- Device registration
- Sensor/video event ingestion

Folder:
- `cloud-backend/`

Consumes:
- Sensor data from `jetson-device-gateway/`
- Camera events/status from `jetson-device-gateway/`

Provides:
- API endpoints for dashboard
- API contract documented in `docs/api-contract.md`

---

## 3. ESP Setup, Firmware, and Receiver Interface – Hikaru

Responsible for:
- ESP32-C3 sensor setup
- ESP32-S3/S6 camera setup
- Sensor reading code
- Camera/WebRTC streaming code
- Define sensor data format sent to Jetson
- Define camera stream format sent to Jetson

Folders:
- `esp-firmware/esp32-c3-sensors/`
- `esp-firmware/esp32-s3-camera/`

Important boundary:
- ESP firmware owns direct device behavior.
- Jetson device gateway owns stable API access for other services.

---

## 4. Jetson Orin Nano Device Gateway + API for ESP Paths

Responsible for:
- Host the ESP-only WiFi network on the Jetson side
- Register ESP32-C3 sensor nodes and ESP32-S3 camera nodes
- Store current device IPs and sensor readings
- Proxy ESP32-S3 camera capture/stream endpoints by device ID
- Provide one local API surface for dashboard/backend services

Folder:
- `jetson-device-gateway/`

Consumes:
- `esp-firmware/esp32-c3-sensors/packet-format.md`
- `esp-firmware/esp32-s3-camera/stream-format.md`
- `shared/schemas/sensor-reading.schema.json`
- `shared/schemas/camera-event.schema.json`
