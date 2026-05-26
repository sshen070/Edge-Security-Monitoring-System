# ESP32-C3 Sensors

Firmware and examples for Seeed XIAO ESP32-C3 Grove sensor/output nodes.

## Deployable Demos

- [Demo overview](./examples/README.md)
- [Register test](./examples/jetson_register_test/jetson_register_test.ino)
- [Sensor node](./examples/jetson_sensor_node_demo/jetson_sensor_node_demo.ino)
- [Output node](./examples/jetson_output_node_demo/jetson_output_node_demo.ino)
- [Gateway/dashboard node](./examples/jetson_gateway_dashboard_demo/jetson_gateway_dashboard_demo.ino)

## Diagnostics

- [Hotspot WiFi test](./examples/hotspot_wifi_test/README.md)

## Flash From Jetson

From the repo root on the Jetson:

```bash
BOARD=c3-register-test ./esp-firmware/scripts/flash_esp.sh
```

Other C3 targets:

```bash
BOARD=c3-sensor-node    ./esp-firmware/scripts/flash_esp.sh
BOARD=c3-output-node    ./esp-firmware/scripts/flash_esp.sh
BOARD=c3-dashboard-node ./esp-firmware/scripts/flash_esp.sh
BOARD=c3-hotspot-test   ./esp-firmware/scripts/flash_esp.sh
```

Set `PORT=/dev/ttyACM0` if multiple ESP boards are connected.
