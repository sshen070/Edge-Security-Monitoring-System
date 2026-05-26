#pragma once

#include "esp_camera.h"

/*
  Camera boot preset

  Edit the values in this file to change what the camera uses immediately after
  boot. The /portal controls can still override these until the board restarts.

  Resolution choices:
    CAMERA_RES_QQVGA   160x120
    CAMERA_RES_QCIF    176x144
    CAMERA_RES_HQVGA   240x176
    CAMERA_RES_240X240 240x240
    CAMERA_RES_QVGA    320x240
    CAMERA_RES_CIF     400x296
    CAMERA_RES_HVGA    480x320
    CAMERA_RES_VGA     640x480
    CAMERA_RES_SVGA    800x600
    CAMERA_RES_XGA     1024x768
    CAMERA_RES_HD      1280x720
    CAMERA_RES_SXGA    1280x1024
    CAMERA_RES_UXGA    1600x1200

  Practical defaults:
    - QVGA/VGA: smoother stream, lower latency.
    - SVGA/XGA: sharper, more bandwidth and memory.
    - HD/UXGA: best detail, slower and may be unstable on weak Wi-Fi.

  Runtime URL overrides:
    These change the current camera state before capture/stream starts.
    They stay active until another request changes them or the board restarts.

    Still image:
      http://<ip>/capture?framesize=vga&quality=12&brightness=1

    Live stream:
      http://<ip>:81/stream?fs=qvga&q=10&vflip=1

    Common query names:
      framesize or fs: qqvga, qcif, hqvga, 240x240, qvga, cif, hvga,
                       vga, svga, xga, hd, sxga, uxga
      quality or q:    4..63
      brightness/bri:  -2..2
      contrast/con:    -2..2
      saturation/sat:  -2..2
      led_intensity or led: 0..255

    Header overrides are also accepted with names like:
      X-Camera-Framesize: vga
      X-Camera-Quality: 12
      X-Camera-Brightness: 1
*/

#define CAMERA_RES_QQVGA   FRAMESIZE_QQVGA
#define CAMERA_RES_QCIF    FRAMESIZE_QCIF
#define CAMERA_RES_HQVGA   FRAMESIZE_HQVGA
#define CAMERA_RES_240X240 FRAMESIZE_240X240
#define CAMERA_RES_QVGA    FRAMESIZE_QVGA
#define CAMERA_RES_CIF     FRAMESIZE_CIF
#define CAMERA_RES_HVGA    FRAMESIZE_HVGA
#define CAMERA_RES_VGA     FRAMESIZE_VGA
#define CAMERA_RES_SVGA    FRAMESIZE_SVGA
#define CAMERA_RES_XGA     FRAMESIZE_XGA
#define CAMERA_RES_HD      FRAMESIZE_HD
#define CAMERA_RES_SXGA    FRAMESIZE_SXGA
#define CAMERA_RES_UXGA    FRAMESIZE_UXGA

// Main image settings.
#define CAMERA_PRESET_FRAMESIZE    CAMERA_RES_QVGA  // Use one CAMERA_RES_* value above.
#define CAMERA_PRESET_JPEG_QUALITY 10               // 4..63. Lower number = better quality/larger frames.
#define CAMERA_PRESET_BRIGHTNESS   0                // -2..2. Higher = brighter image.
#define CAMERA_PRESET_CONTRAST     0                // -2..2. Higher = stronger light/dark separation.
#define CAMERA_PRESET_SATURATION   0                // -2..2. Higher = stronger color.

// Special effects: 0 none, 1 negative, 2 grayscale, 3 red tint, 4 green tint, 5 blue tint, 6 sepia.
#define CAMERA_PRESET_SPECIAL_EFFECT 0

// White balance.
#define CAMERA_PRESET_AWB      1  // 0 off, 1 on.
#define CAMERA_PRESET_AWB_GAIN 1  // 0 off, 1 on. Usually keep on with AWB.
#define CAMERA_PRESET_WB_MODE  0  // 0 auto, 1 sunny, 2 cloudy, 3 office, 4 home.

// Exposure.
#define CAMERA_PRESET_AEC       1    // 0 manual exposure, 1 auto exposure.
#define CAMERA_PRESET_AEC2      1    // 0 off, 1 on. DSP auto exposure assist.
#define CAMERA_PRESET_AE_LEVEL  0    // -2..2. Bias auto exposure darker/brighter.
#define CAMERA_PRESET_AEC_VALUE 204  // 0..1200. Used most when CAMERA_PRESET_AEC is 0.

// Gain.
#define CAMERA_PRESET_AGC         1              // 0 manual gain, 1 auto gain.
#define CAMERA_PRESET_AGC_GAIN    5              // 0..30. Used most when CAMERA_PRESET_AGC is 0.
#define CAMERA_PRESET_GAINCEILING GAINCEILING_2X // GAINCEILING_2X, 4X, 8X, 16X, 32X, 64X, or 128X.

// Cleanup / correction.
#define CAMERA_PRESET_BPC     0  // Bad pixel correction: 0 off, 1 on.
#define CAMERA_PRESET_WPC     1  // White pixel correction: 0 off, 1 on.
#define CAMERA_PRESET_RAW_GMA 1  // Gamma correction: 0 off, 1 on.
#define CAMERA_PRESET_LENC    1  // Lens correction: 0 off, 1 on.
#define CAMERA_PRESET_DCW     1  // Downsize crop/window helper: 0 off, 1 on.

// Orientation and test output.
#define CAMERA_PRESET_HMIRROR  0  // 0 normal, 1 mirror horizontally.
#define CAMERA_PRESET_VFLIP    0  // 0 normal, 1 flip vertically.
#define CAMERA_PRESET_COLORBAR 0  // 0 normal image, 1 sensor color-bar test pattern.

// Power behavior. Camera driver shuts down after this much idle time with no active stream.
#define CAMERA_IDLE_SLEEP_MS 60000

// Activity LED behavior. Single-color LED/flash is off when idle and on while the camera is in use.
#define CAMERA_ACTIVITY_LED_INTENSITY 24  // 0..255. Use 0 to keep it always off.

static inline void applyCameraFeedPreset(sensor_t *sensor) {
  if (!sensor) {
    return;
  }

  if (sensor->pixformat == PIXFORMAT_JPEG) {
    sensor->set_framesize(sensor, CAMERA_PRESET_FRAMESIZE);
  }

  sensor->set_quality(sensor, CAMERA_PRESET_JPEG_QUALITY);
  sensor->set_brightness(sensor, CAMERA_PRESET_BRIGHTNESS);
  sensor->set_contrast(sensor, CAMERA_PRESET_CONTRAST);
  sensor->set_saturation(sensor, CAMERA_PRESET_SATURATION);
  sensor->set_special_effect(sensor, CAMERA_PRESET_SPECIAL_EFFECT);

  sensor->set_whitebal(sensor, CAMERA_PRESET_AWB);
  sensor->set_awb_gain(sensor, CAMERA_PRESET_AWB_GAIN);
  sensor->set_wb_mode(sensor, CAMERA_PRESET_WB_MODE);

  sensor->set_exposure_ctrl(sensor, CAMERA_PRESET_AEC);
  sensor->set_aec2(sensor, CAMERA_PRESET_AEC2);
  sensor->set_ae_level(sensor, CAMERA_PRESET_AE_LEVEL);
  sensor->set_aec_value(sensor, CAMERA_PRESET_AEC_VALUE);

  sensor->set_gain_ctrl(sensor, CAMERA_PRESET_AGC);
  sensor->set_agc_gain(sensor, CAMERA_PRESET_AGC_GAIN);
  sensor->set_gainceiling(sensor, CAMERA_PRESET_GAINCEILING);

  sensor->set_bpc(sensor, CAMERA_PRESET_BPC);
  sensor->set_wpc(sensor, CAMERA_PRESET_WPC);
  sensor->set_raw_gma(sensor, CAMERA_PRESET_RAW_GMA);
  sensor->set_lenc(sensor, CAMERA_PRESET_LENC);
  sensor->set_dcw(sensor, CAMERA_PRESET_DCW);

  sensor->set_hmirror(sensor, CAMERA_PRESET_HMIRROR);
  sensor->set_vflip(sensor, CAMERA_PRESET_VFLIP);
  sensor->set_colorbar(sensor, CAMERA_PRESET_COLORBAR);
}
