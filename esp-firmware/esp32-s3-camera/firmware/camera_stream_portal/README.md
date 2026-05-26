# ESP32-S3 Camera Firmware

This sketch runs an ESP32-S3 camera with a small settings portal, a raw JPEG capture endpoint, and a raw MJPEG stream endpoint.

## Access

After flashing, open the Serial Monitor at `115200` baud and copy the printed IP address.

Replace `<ip>` below with that address:

```text
Portal/settings: http://<ip>/portal
Raw still JPEG:  http://<ip>/capture
Raw MJPEG feed:  http://<ip>:81/stream
```

The portal runs on port `80`. The live stream runs on port `81` so the long-running MJPEG response does not block settings, status, or capture requests.

## Idle Power Behavior

The web server stays online after boot, but the camera sensor/driver does not
stay initialized forever. The firmware powers the camera on when one of these
routes needs it:

```text
/capture
:81/stream
/status
/control
/bmp
low-level register/resolution routes
```

When there are no active stream clients and no camera request has used the
sensor for `CAMERA_IDLE_SLEEP_MS`, the firmware calls `esp_camera_deinit()` and
leaves only WiFi plus the HTTP servers running. The next capture, stream, or
portal control request initializes the camera again and reapplies
`camera_presets.h`.

This reduces idle heat and power draw. Runtime settings changed from the portal
may reset after an idle sleep because the camera presets are reapplied on wake.

The camera flash LED defined by `LED_GPIO_NUM` is also tied to camera activity.
It stays off while idle and turns on while capture, stream, or camera-control
routes are using the camera. Set `CAMERA_ACTIVITY_LED_INTENSITY` in
`camera_presets.h` to adjust the brightness, or set it to `0` to keep the flash
LED always off.

The red USB/battery charge/status LED on the XIAO ESP32S3 Sense is
hardware-controlled and is not a firmware-controlled RGB/user LED. Color changes
require an external RGB LED or NeoPixel.

## Raw Endpoints

`/capture` returns a single raw `image/jpeg`.

```text
http://<ip>/capture
```

`:81/stream` returns a raw multipart MJPEG stream.

```text
http://<ip>:81/stream
```

If you type `http://<ip>/stream` without `:81`, you are not using the raw stream server.

## Runtime URL Settings

The capture and stream endpoints can apply camera settings from URL query parameters before they start.

Examples:

```text
http://<ip>/capture?framesize=vga&quality=12&brightness=1
http://<ip>:81/stream?fs=qvga&q=10&vflip=1
```

These changes update the current camera state. They remain active until another request changes them or the board restarts.

Common query parameters:

```text
framesize or fs: qqvga, qcif, hqvga, 240x240, qvga, cif, hvga, vga, svga, xga, hd, sxga, uxga
quality or q:    4..63, lower number means better quality and larger frames
brightness/bri:  -2..2
contrast/con:    -2..2
saturation/sat:  -2..2
effect:          0 none, 1 negative, 2 grayscale, 3 red tint, 4 green tint, 5 blue tint, 6 sepia
wb:              0 auto, 1 sunny, 2 cloudy, 3 office, 4 home
mirror:          0 off, 1 on
flip:            0 off, 1 on
led:             0..255
```

Full supported setting names also include:

```text
awb, awb_gain, aec, aec2, ae_level, aec_value, agc, agc_gain,
gainceiling, bpc, wpc, raw_gma, lenc, dcw, hmirror, vflip,
colorbar, led_intensity
```

The portal still uses `/control?var=<setting>&val=<value>` internally, so these are also valid:

```text
http://<ip>/control?var=framesize&val=8
http://<ip>/control?var=quality&val=10
```

## Header Settings

Clients that can send HTTP headers may use `X-Camera-*` headers instead of query parameters.

Example:

```bash
curl \
  -H "X-Camera-Framesize: vga" \
  -H "X-Camera-Quality: 12" \
  -H "X-Camera-Brightness: 1" \
  "http://<ip>/capture" \
  --output capture.jpg
```

Supported headers include:

```text
X-Camera-Framesize
X-Camera-Quality
X-Camera-Brightness
X-Camera-Contrast
X-Camera-Saturation
X-Camera-Effect
X-Camera-WB-Mode
X-Camera-AWB
X-Camera-AWB-Gain
X-Camera-AEC
X-Camera-AEC2
X-Camera-AE-Level
X-Camera-AEC-Value
X-Camera-AGC
X-Camera-AGC-Gain
X-Camera-Gainceiling
X-Camera-BPC
X-Camera-WPC
X-Camera-Raw-GMA
X-Camera-Lenc
X-Camera-DCW
X-Camera-HMirror
X-Camera-VFlip
X-Camera-Colorbar
X-Camera-LED
```

## Camera Presets

Edit `camera_presets.h` to change the settings that apply when the camera
powers on. Because the camera can sleep while idle, these presets are reapplied
after each idle wake.

Useful defaults:

```cpp
#define CAMERA_PRESET_FRAMESIZE    CAMERA_RES_QVGA
#define CAMERA_PRESET_JPEG_QUALITY 10
#define CAMERA_PRESET_BRIGHTNESS   0
#define CAMERA_PRESET_CONTRAST     0
#define CAMERA_PRESET_SATURATION   0
```

Resolution options are documented in `camera_presets.h`, including `CAMERA_RES_QVGA`, `CAMERA_RES_VGA`, `CAMERA_RES_SVGA`, `CAMERA_RES_HD`, and `CAMERA_RES_UXGA`.

Lower resolutions are smoother and lower latency. Higher resolutions are sharper but require more bandwidth and memory.

## Jetson Registration

Edit `secrets.h` before flashing:

```cpp
const char *WIFI_SSID = "ESP-NET";
const char *WIFI_PASSWORD = "change-this-password";
const char *DEVICE_ID = "camera-front-01";
const char *DEVICE_REGISTRY_URL = "http://10.42.0.1:8080/api/devices/register";
```

After WiFi connects, the camera registers its IP and capabilities with the Jetson Orin Nano device gateway. The same gateway can then proxy:

```text
http://10.42.0.1:8080/api/cameras/camera-front-01/portal
http://10.42.0.1:8080/api/cameras/camera-front-01/viewer
http://10.42.0.1:8080/api/cameras/camera-front-01/status
http://10.42.0.1:8080/api/cameras/camera-front-01/control?var=quality&val=10
http://10.42.0.1:8080/api/cameras/camera-front-01/capture
http://10.42.0.1:8080/api/cameras/camera-front-01/stream
```

## Files

```text
camera_stream_portal.ino                 Main boot/setup sketch
app_httpd.cpp                           HTTP endpoints and portal
camera_presets.h                        Boot defaults and setting reference
camera_pins.h                           Board pin map
secrets.h                               Local Wi-Fi credentials
```
