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
- Sensor data from `jetson-sensor-gateway/`
- Camera events/status from `jetson-camera-gateway/`

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
- Provide minimal Jetson-side receiver examples/test scripts

Folders:
- `esp-firmware/esp32-c3-sensors/`
- `esp-firmware/esp32-s3-camera/`
- `esp-firmware/receiver-reference/`

Important boundary:
- Hikaru provides ESP outputs and minimal receiver examples.
- Jetson gateway owners turn those examples into full gateway services.

---

## 4. Jetson Nano Sensor Gateway + API for ESP32-C3 Path

Responsible for:
- Use Hikaru’s Zigbee packet format and receiver example
- Set up Zigbee coordinator / receiver on Jetson
- Receive data from 3 ESP32-C3 sensor nodes
- Parse and validate incoming sensor packets
- Perform local preprocessing if needed
- Send processed sensor data to cloud backend API

Folder:
- `jetson-sensor-gateway/`

Consumes:
- `esp-firmware/receiver-reference/zigbee-receiver-example/`
- `shared/schemas/sensor-reading.schema.json`

---

## 5. Jetson Nano Camera Gateway + API for ESP32-S3/S6 Path

Responsible for:
- Use Hikaru’s WebRTC/IP-camera stream format and receiver example
- Set up WebRTC/IP-camera receiver on Jetson
- Receive camera stream from ESP32-S3/S6 camera device
- Perform optional edge video processing
- Send camera status, metadata, or detected events to cloud backend API

Folder:
- `jetson-camera-gateway/`

Consumes:
- `esp-firmware/receiver-reference/webrtc-receiver-example/`
- `shared/schemas/camera-event.schema.json`
