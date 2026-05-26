# ESP Firmware

Deployable Arduino sketches and reference notes for the ESP nodes.

## Deploy Targets

- [ESP32-C3 Grove sensor/output nodes](./esp32-c3-sensors/README.md)
- [ESP32-S3 camera stream portal](./esp32-s3-camera/README.md)

## Architecture

- [Jetson Orin Nano ESP WiFi topology](./jetson-esp-wifi-topology.md)

## Expected Network

Production ESP firmware should connect to the Jetson Orin Nano private AP:

```text
SSID: ESP-NET
Jetson API: http://10.42.0.1:8080
```

School/campus WiFi sketches are kept only as diagnostics.
