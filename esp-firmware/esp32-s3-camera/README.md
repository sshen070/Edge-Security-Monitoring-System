# ESP32-S3 Camera

Deployable camera firmware for the XIAO ESP32S3 camera setup.

## Main Sketch

Open this folder in Arduino IDE:

```text
esp-firmware/esp32-s3-camera/firmware/camera_stream_portal
```

Flash:

```text
camera_stream_portal.ino
```

## Endpoints

After the board connects, Serial Monitor prints its IP.

```text
Portal/settings: http://<ip>/portal
Raw still JPEG:  http://<ip>/capture
Raw MJPEG feed:  http://<ip>:81/stream
```

From a Mac or any client that can reach the Jetson but not the ESP private
subnet, use the gateway proxy:

```text
Portal/settings: http://<jetson-ip>:8080/api/cameras/camera-front-01/portal
Raw still JPEG:  http://<jetson-ip>:8080/api/cameras/camera-front-01/capture
Raw MJPEG feed:  http://<jetson-ip>:8080/api/cameras/camera-front-01/stream
```

Detailed camera settings and URL/header controls are documented in:

```text
firmware/camera_stream_portal/README.md
```

## Diagnostics

- [School WiFi test](./examples/school_wifi_test/README.md)
- [Stream format](./stream-format.md)

## Flash From Jetson

From the repo root on the Jetson:

```bash
BOARD=s3-camera ./esp-firmware/scripts/flash_esp.sh
```

Diagnostic school WiFi sketch:

```bash
BOARD=s3-school-wifi-test ./esp-firmware/scripts/flash_esp.sh
```

Set `PORT=/dev/ttyACM0` if multiple ESP boards are connected.
