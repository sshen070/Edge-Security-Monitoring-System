# ESP32-S3 Camera Firmware

## Deployable Sketch

```text
camera_stream_portal/
```

Open that folder in Arduino IDE and flash `camera_stream_portal.ino`.

## Notes

- The camera portal exposes settings on port `80`.
- The raw MJPEG stream runs on port `81`.
- Edit `camera_stream_portal/secrets.h` for the WiFi network.
- Edit `camera_stream_portal/camera_presets.h` for boot camera settings.
