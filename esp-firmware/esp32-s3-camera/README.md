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

Detailed camera settings and URL/header controls are documented in:

```text
firmware/camera_stream_portal/README.md
```

## Diagnostics

- [School WiFi test](./examples/school_wifi_test/README.md)
- [Stream format](./stream-format.md)
