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

## Flash From Jetson Orin Nano

Connect the ESP board to the Jetson over USB, then run the flash helper from
the repo root:

```bash
BOARD=c3-register-test ./esp-firmware/scripts/flash_esp.sh
```

Equivalent positional form:

```bash
./esp-firmware/scripts/flash_esp.sh c3-register-test
```

The script installs `arduino-cli` and the Espressif ESP32 board core if needed,
auto-detects `/dev/ttyACM*` or `/dev/ttyUSB*`, compiles the sketch, and uploads
it. Use `PORT` when more than one board is connected:

```bash
BOARD=c3-register-test PORT=/dev/ttyACM0 ./esp-firmware/scripts/flash_esp.sh
```

Common targets:

```bash
BOARD=c3-register-test   ./esp-firmware/scripts/flash_esp.sh
BOARD=c3-sensor-node     ./esp-firmware/scripts/flash_esp.sh
BOARD=c3-output-node     ./esp-firmware/scripts/flash_esp.sh
BOARD=c3-dashboard-node  ./esp-firmware/scripts/flash_esp.sh
BOARD=s3-camera          ./esp-firmware/scripts/flash_esp.sh
```

Open Serial Monitor after upload:

```bash
MONITOR=1 BOARD=c3-register-test ./esp-firmware/scripts/flash_esp.sh
```

If Arduino CLI uses a different board ID on the Jetson, override it:

```bash
FQBN=esp32:esp32:XIAO_ESP32C3 BOARD=c3-register-test ./esp-firmware/scripts/flash_esp.sh
```
