# ESP32-S3 Camera Stream Format

The deployable camera firmware exposes raw camera outputs.

## Still Capture

```text
GET http://<ip>/capture
Content-Type: image/jpeg
```

This returns one JPEG frame.

## Live Stream

```text
GET http://<ip>:81/stream
Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321
```

Each MJPEG part contains:

```text
Content-Type: image/jpeg
Content-Length: <bytes>
```

The stream remains open while the client stays connected. The camera only streams when `/stream` is actively requested.

## Runtime Settings

Both `/capture` and `/stream` accept query parameters before output begins:

```text
http://<ip>/capture?framesize=vga&quality=12
http://<ip>:81/stream?fs=qvga&q=10
```

See `firmware/camera_stream_portal/README.md` for the full setting list.
