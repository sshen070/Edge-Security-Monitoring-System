# Jetson ESP32-C3 Demo Sketches

These examples target Seeed XIAO ESP32-C3 boards. Start with the register test, then move to Grove sensor/output demos.

All three sketches connect to the Jetson Orin Nano private ESP WiFi network:

```text
SSID: ESP-NET
Registry API: http://10.42.0.1:8080/api/devices/register
Sensor API:   http://10.42.0.1:8080/api/sensors
```

Before flashing, update this in each `.ino`:

```cpp
const char *WIFI_PASSWORD = "change-this-password";
```

## Examples

### Register Test

Path:

```text
esp-firmware/esp32-c3-sensors/examples/jetson_register_test/jetson_register_test.ino
```

Use this first. It needs no Grove sensors. It connects to `ESP-NET`, registers with the Jetson device gateway, and posts a small test reading every 5 seconds.

Registers as:

```text
c3-register-test-01
```

Jetson checks:

```sh
curl http://10.42.0.1:8080/api/devices
curl http://10.42.0.1:8080/api/sensors/c3-register-test-01/latest
```

### Sensor Node

Path:

```text
esp-firmware/esp32-c3-sensors/examples/jetson_sensor_node_demo/jetson_sensor_node_demo.ino
```

Grove wiring:

```text
Light sensor: A0
PIR motion:   D0
I2C SDA:      D4
I2C SCL:      D5
```

Registers as:

```text
sensor-node-01
```

Capabilities:

```text
pir, light, i2c-scan
```

The sensor node also pushes live readings to:

```text
POST http://10.42.0.1:8080/api/sensors/sensor-node-01/readings
```

To reduce network and power usage, it samples locally every second but only
posts when one of these is true:

```text
initial reading after boot
PIR motion state changed
light changed by at least 50 raw ADC counts
60 second heartbeat interval elapsed
```

### Output Node

Path:

```text
esp-firmware/esp32-c3-sensors/examples/jetson_output_node_demo/jetson_output_node_demo.ino
```

Grove wiring:

```text
LED:    D10 or built-in LED
Buzzer: D1
Servo:  D2 signal
```

Registers as:

```text
output-node-01
```

Capabilities:

```text
led, buzzer, servo
```

The servo demo generates pulses directly and does not require the `ESP32Servo` library. Use external 5V power for larger servos.

### Gateway/Dashboard Node

Path:

```text
esp-firmware/esp32-c3-sensors/examples/jetson_gateway_dashboard_demo/jetson_gateway_dashboard_demo.ino
```

Registers as:

```text
gateway-dashboard-01
```

Capabilities:

```text
wifi-status, local-dashboard
```

After it connects, open the IP printed in Serial Monitor. The node serves:

```text
GET /
GET /status
```

## Arduino IDE

Use:

```text
Board: XIAO_ESP32C3
Serial Monitor: 115200 baud
```

These examples intentionally do not use the XIAO ESP32S3 Sense camera/mic. The C3 boards are for Grove sensors, simple outputs, WiFi registration, and small local HTTP tools.

## Jetson Flash Helper

From the repo root on the Jetson:

```bash
./esp-firmware/scripts/flash_esp.sh c3-register-test
MONITOR=1 ./esp-firmware/scripts/flash_esp.sh c3-register-test
```

Use `PORT=/dev/ttyACM0` if multiple ESP boards are plugged in.
