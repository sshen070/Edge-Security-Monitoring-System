// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// Enable LED FLASH setting
#define CONFIG_LED_ILLUMINATOR_ENABLED 1

// LED FLASH setup
#if CONFIG_LED_ILLUMINATOR_ENABLED

#define LED_LEDC_GPIO            22  //configure LED pin
#define CONFIG_LED_MAX_INTENSITY 255

int led_duty = 0;
bool isStreaming = false;

#endif

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;
static volatile uint8_t active_stream_clients = 0;

typedef struct {
  size_t size;   //number of values used for filtering
  size_t index;  //current value index
  size_t count;  //value count
  int sum;
  int *values;  //array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
  memset(filter, 0, sizeof(ra_filter_t));

  filter->values = (int *)malloc(sample_size * sizeof(int));
  if (!filter->values) {
    return NULL;
  }
  memset(filter->values, 0, sample_size * sizeof(int));

  filter->size = sample_size;
  return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value) {
  if (!filter->values) {
    return value;
  }
  filter->sum -= filter->values[filter->index];
  filter->values[filter->index] = value;
  filter->sum += filter->values[filter->index];
  filter->index++;
  filter->index = filter->index % filter->size;
  if (filter->count < filter->size) {
    filter->count++;
  }
  return filter->sum / filter->count;
}

#if CONFIG_LED_ILLUMINATOR_ENABLED
void enable_led(bool en) {  // Turn LED On or Off
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) {
    duty = CONFIG_LED_MAX_INTENSITY;
  }
  ledcWrite(LED_LEDC_GPIO, duty);
  //ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL, duty);
  //ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL);
  log_i("Set LED intensity to %d", duty);
}
#endif

static esp_err_t bmp_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  uint64_t fr_start = esp_timer_get_time();
  fb = esp_camera_fb_get();
  if (!fb) {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/x-windows-bmp");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

  uint8_t *buf = NULL;
  size_t buf_len = 0;
  bool converted = frame2bmp(fb, &buf, &buf_len);
  esp_camera_fb_return(fb);
  if (!converted) {
    log_e("BMP Conversion failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  res = httpd_resp_send(req, (const char *)buf, buf_len);
  free(buf);
  uint64_t fr_end = esp_timer_get_time();
  log_i("BMP: %llums, %uB", (uint64_t)((fr_end - fr_start) / 1000), buf_len);
  return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

static int parse_framesize_value(const char *value) {
  if (!strcmp(value, "qqvga")) {
    return FRAMESIZE_QQVGA;
  } else if (!strcmp(value, "qcif")) {
    return FRAMESIZE_QCIF;
  } else if (!strcmp(value, "hqvga")) {
    return FRAMESIZE_HQVGA;
  } else if (!strcmp(value, "240x240")) {
    return FRAMESIZE_240X240;
  } else if (!strcmp(value, "qvga")) {
    return FRAMESIZE_QVGA;
  } else if (!strcmp(value, "cif")) {
    return FRAMESIZE_CIF;
  } else if (!strcmp(value, "hvga")) {
    return FRAMESIZE_HVGA;
  } else if (!strcmp(value, "vga")) {
    return FRAMESIZE_VGA;
  } else if (!strcmp(value, "svga")) {
    return FRAMESIZE_SVGA;
  } else if (!strcmp(value, "xga")) {
    return FRAMESIZE_XGA;
  } else if (!strcmp(value, "hd")) {
    return FRAMESIZE_HD;
  } else if (!strcmp(value, "sxga")) {
    return FRAMESIZE_SXGA;
  } else if (!strcmp(value, "uxga")) {
    return FRAMESIZE_UXGA;
  }
  return atoi(value);
}

static int apply_camera_setting(sensor_t *s, const char *variable, const char *value) {
  int val = atoi(value);

  if (!strcmp(variable, "framesize")) {
    if (s->pixformat == PIXFORMAT_JPEG) {
      return s->set_framesize(s, (framesize_t)parse_framesize_value(value));
    }
    return 0;
  } else if (!strcmp(variable, "quality")) {
    return s->set_quality(s, val);
  } else if (!strcmp(variable, "contrast")) {
    return s->set_contrast(s, val);
  } else if (!strcmp(variable, "brightness")) {
    return s->set_brightness(s, val);
  } else if (!strcmp(variable, "saturation")) {
    return s->set_saturation(s, val);
  } else if (!strcmp(variable, "gainceiling")) {
    return s->set_gainceiling(s, (gainceiling_t)val);
  } else if (!strcmp(variable, "colorbar")) {
    return s->set_colorbar(s, val);
  } else if (!strcmp(variable, "awb")) {
    return s->set_whitebal(s, val);
  } else if (!strcmp(variable, "agc")) {
    return s->set_gain_ctrl(s, val);
  } else if (!strcmp(variable, "aec")) {
    return s->set_exposure_ctrl(s, val);
  } else if (!strcmp(variable, "hmirror")) {
    return s->set_hmirror(s, val);
  } else if (!strcmp(variable, "vflip")) {
    return s->set_vflip(s, val);
  } else if (!strcmp(variable, "awb_gain")) {
    return s->set_awb_gain(s, val);
  } else if (!strcmp(variable, "agc_gain")) {
    return s->set_agc_gain(s, val);
  } else if (!strcmp(variable, "aec_value")) {
    return s->set_aec_value(s, val);
  } else if (!strcmp(variable, "aec2")) {
    return s->set_aec2(s, val);
  } else if (!strcmp(variable, "dcw")) {
    return s->set_dcw(s, val);
  } else if (!strcmp(variable, "bpc")) {
    return s->set_bpc(s, val);
  } else if (!strcmp(variable, "wpc")) {
    return s->set_wpc(s, val);
  } else if (!strcmp(variable, "raw_gma")) {
    return s->set_raw_gma(s, val);
  } else if (!strcmp(variable, "lenc")) {
    return s->set_lenc(s, val);
  } else if (!strcmp(variable, "special_effect")) {
    return s->set_special_effect(s, val);
  } else if (!strcmp(variable, "wb_mode")) {
    return s->set_wb_mode(s, val);
  } else if (!strcmp(variable, "ae_level")) {
    return s->set_ae_level(s, val);
  }
#if CONFIG_LED_ILLUMINATOR_ENABLED
  else if (!strcmp(variable, "led_intensity")) {
    led_duty = val;
    if (isStreaming) {
      enable_led(true);
    }
    return 0;
  }
#endif

  return -1;
}

typedef struct {
  const char *variable;
  const char *query_key;
  const char *query_alias;
  const char *header_key;
} camera_request_setting_t;

static const camera_request_setting_t request_settings[] = {
  {"framesize", "framesize", "fs", "X-Camera-Framesize"},
  {"quality", "quality", "q", "X-Camera-Quality"},
  {"brightness", "brightness", "bri", "X-Camera-Brightness"},
  {"contrast", "contrast", "con", "X-Camera-Contrast"},
  {"saturation", "saturation", "sat", "X-Camera-Saturation"},
  {"special_effect", "special_effect", "effect", "X-Camera-Effect"},
  {"wb_mode", "wb_mode", "wb", "X-Camera-WB-Mode"},
  {"awb", "awb", NULL, "X-Camera-AWB"},
  {"awb_gain", "awb_gain", NULL, "X-Camera-AWB-Gain"},
  {"aec", "aec", NULL, "X-Camera-AEC"},
  {"aec2", "aec2", NULL, "X-Camera-AEC2"},
  {"ae_level", "ae_level", NULL, "X-Camera-AE-Level"},
  {"aec_value", "aec_value", NULL, "X-Camera-AEC-Value"},
  {"agc", "agc", NULL, "X-Camera-AGC"},
  {"agc_gain", "agc_gain", NULL, "X-Camera-AGC-Gain"},
  {"gainceiling", "gainceiling", "gainceil", "X-Camera-Gainceiling"},
  {"bpc", "bpc", NULL, "X-Camera-BPC"},
  {"wpc", "wpc", NULL, "X-Camera-WPC"},
  {"raw_gma", "raw_gma", NULL, "X-Camera-Raw-GMA"},
  {"lenc", "lenc", NULL, "X-Camera-Lenc"},
  {"dcw", "dcw", NULL, "X-Camera-DCW"},
  {"hmirror", "hmirror", "mirror", "X-Camera-HMirror"},
  {"vflip", "vflip", "flip", "X-Camera-VFlip"},
  {"colorbar", "colorbar", NULL, "X-Camera-Colorbar"},
  {"led_intensity", "led_intensity", "led", "X-Camera-LED"}
};

static bool query_value(char *query, const camera_request_setting_t *setting, char *value, size_t value_len) {
  if (!query) {
    return false;
  }
  if (httpd_query_key_value(query, setting->query_key, value, value_len) == ESP_OK) {
    return true;
  }
  return setting->query_alias && httpd_query_key_value(query, setting->query_alias, value, value_len) == ESP_OK;
}

static bool header_value(httpd_req_t *req, const char *header_key, char *value, size_t value_len) {
  size_t header_len = httpd_req_get_hdr_value_len(req, header_key);
  return header_len > 0 && header_len < value_len && httpd_req_get_hdr_value_str(req, header_key, value, value_len) == ESP_OK;
}

static esp_err_t apply_request_camera_settings(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    return ESP_FAIL;
  }

  char *query = NULL;
  size_t query_len = httpd_req_get_url_query_len(req) + 1;
  if (query_len > 1) {
    query = (char *)malloc(query_len);
    if (!query) {
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, query, query_len) != ESP_OK) {
      free(query);
      query = NULL;
    }
  }

  char value[32];
  for (size_t i = 0; i < sizeof(request_settings) / sizeof(request_settings[0]); i++) {
    const camera_request_setting_t *setting = &request_settings[i];
    bool found = query_value(query, setting, value, sizeof(value));
    if (!found) {
      found = header_value(req, setting->header_key, value, sizeof(value));
    }
    if (found) {
      log_i("Request setting %s = %s", setting->variable, value);
      if (apply_camera_setting(s, setting->variable, value) < 0) {
        if (query) {
          free(query);
        }
        return ESP_FAIL;
      }
    }
  }

  if (query) {
    free(query);
  }
  return ESP_OK;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  int64_t fr_start = esp_timer_get_time();

  if (apply_request_camera_settings(req) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

#if CONFIG_LED_ILLUMINATOR_ENABLED
  enable_led(true);
  vTaskDelay(150 / portTICK_PERIOD_MS);  // The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
  fb = esp_camera_fb_get();              // or it won't be visible in the frame. A better way to do this is needed.
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif

  if (!fb) {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

  size_t fb_len = 0;
  if (fb->format == PIXFORMAT_JPEG) {
    fb_len = fb->len;
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  } else {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
    fb_len = jchunk.len;
  }
  esp_camera_fb_return(fb);
  int64_t fr_end = esp_timer_get_time();
  log_i("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[128];

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  if (apply_request_camera_settings(req) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  active_stream_clients++;
  log_i("Stream client connected, active clients: %u", active_stream_clients);

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

#if CONFIG_LED_ILLUMINATOR_ENABLED
  isStreaming = true;
  enable_led(true);
#endif

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      log_e("Camera capture failed");
      res = ESP_FAIL;
    } else {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          log_e("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      log_e("Send frame failed");
      break;
    }
    int64_t fr_end = esp_timer_get_time();

    int64_t frame_time = fr_end - last_frame;
    frame_time /= 1000;
    if (frame_time == 0) {
      frame_time = 1;
    }
    uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
    log_i(
      "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)",
      (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time, 1000.0 / avg_frame_time
    );
    last_frame = fr_end;
  }

  if (active_stream_clients > 0) {
    active_stream_clients--;
  }

#if CONFIG_LED_ILLUMINATOR_ENABLED
  if (active_stream_clients == 0) {
    isStreaming = false;
    enable_led(false);
  }
#endif

  log_i("Stream client disconnected, active clients: %u", active_stream_clients);

  return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
  char *buf = NULL;
  size_t buf_len = 0;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      *obuf = buf;
      return ESP_OK;
    }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf = NULL;
  char variable[32];
  char value[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  log_i("%s = %s", variable, value);
  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    return httpd_resp_send_500(req);
  }

  int res = apply_camera_setting(s, variable, value);
  if (res < 0) {
    log_i("Unknown command: %s", variable);
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static int print_reg(char *p, sensor_t *s, uint16_t reg, uint32_t mask) {
  return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';

  if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
    for (int reg = 0x3400; reg < 0x3406; reg += 2) {
      p += print_reg(p, s, reg, 0xFFF);  //12 bit
    }
    p += print_reg(p, s, 0x3406, 0xFF);

    p += print_reg(p, s, 0x3500, 0xFFFF0);  //16 bit
    p += print_reg(p, s, 0x3503, 0xFF);
    p += print_reg(p, s, 0x350a, 0x3FF);   //10 bit
    p += print_reg(p, s, 0x350c, 0xFFFF);  //16 bit

    for (int reg = 0x5480; reg <= 0x5490; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5380; reg <= 0x538b; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5580; reg < 0x558a; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }
    p += print_reg(p, s, 0x558a, 0x1FF);  //9 bit
  } else if (s->id.PID == OV2640_PID) {
    p += print_reg(p, s, 0xd3, 0xFF);
    p += print_reg(p, s, 0x111, 0xFF);
    p += print_reg(p, s, 0x132, 0xFF);
  }

  p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
  p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
  p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
  p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p += sprintf(p, "\"awb\":%u,", s->status.awb);
  p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p += sprintf(p, "\"aec\":%u,", s->status.aec);
  p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p += sprintf(p, "\"agc\":%u,", s->status.agc);
  p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
#if CONFIG_LED_ILLUMINATOR_ENABLED
  p += sprintf(p, ",\"led_intensity\":%u", led_duty);
#else
  p += sprintf(p, ",\"led_intensity\":%d", -1);
#endif
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t xclk_handler(httpd_req_t *req) {
  char *buf = NULL;
  char _xclk[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int xclk = atoi(_xclk);
  log_i("Set XCLK: %d MHz", xclk);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t reg_handler(httpd_req_t *req) {
  char *buf = NULL;
  char _reg[32];
  char _mask[32];
  char _val[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK
      || httpd_query_key_value(buf, "val", _val, sizeof(_val)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  int val = atoi(_val);
  log_i("Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_reg(s, reg, mask, val);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t greg_handler(httpd_req_t *req) {
  char *buf = NULL;
  char _reg[32];
  char _mask[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->get_reg(s, reg, mask);
  if (res < 0) {
    return httpd_resp_send_500(req);
  }
  log_i("Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);

  char buffer[20];
  const char *val = itoa(res, buffer, 10);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, val, strlen(val));
}

static int parse_get_var(char *buf, const char *key, int def) {
  char _int[16];
  if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK) {
    return def;
  }
  return atoi(_int);
}

static esp_err_t pll_handler(httpd_req_t *req) {
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }

  int bypass = parse_get_var(buf, "bypass", 0);
  int mul = parse_get_var(buf, "mul", 0);
  int sys = parse_get_var(buf, "sys", 0);
  int root = parse_get_var(buf, "root", 0);
  int pre = parse_get_var(buf, "pre", 0);
  int seld5 = parse_get_var(buf, "seld5", 0);
  int pclken = parse_get_var(buf, "pclken", 0);
  int pclk = parse_get_var(buf, "pclk", 0);
  free(buf);

  log_i("Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t win_handler(httpd_req_t *req) {
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }

  int startX = parse_get_var(buf, "sx", 0);
  int startY = parse_get_var(buf, "sy", 0);
  int endX = parse_get_var(buf, "ex", 0);
  int endY = parse_get_var(buf, "ey", 0);
  int offsetX = parse_get_var(buf, "offx", 0);
  int offsetY = parse_get_var(buf, "offy", 0);
  int totalX = parse_get_var(buf, "tx", 0);
  int totalY = parse_get_var(buf, "ty", 0);
  int outputX = parse_get_var(buf, "ox", 0);
  int outputY = parse_get_var(buf, "oy", 0);
  bool scale = parse_get_var(buf, "scale", 0) == 1;
  bool binning = parse_get_var(buf, "binning", 0) == 1;
  free(buf);

  log_i(
    "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY,
    totalX, totalY, outputX, outputY, scale, binning
  );
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static const char portal_index_html[] = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Camera Portal</title>
<style>
html,body{margin:0;background:#151515;color:#eee;font-family:Arial,Helvetica,sans-serif;font-size:15px;}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 12px;background:#1d1d1d;border-bottom:1px solid #303030;}
main{display:grid;grid-template-columns:minmax(0,1fr) 340px;gap:12px;padding:12px;}
section{min-width:0;}
img{display:block;max-width:100%;max-height:calc(100vh - 94px);background:#050505;border:1px solid #303030;}
.panel{background:#242424;border:1px solid #333;padding:10px;}
.row{display:grid;grid-template-columns:132px minmax(0,1fr);align-items:center;gap:8px;margin:8px 0;}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px;}
button,a{color:#fff;background:#ff3034;border:0;border-radius:5px;padding:7px 12px;text-decoration:none;cursor:pointer;font-size:15px;}
button:hover,a:hover{background:#ff494d;}
input,select{min-width:0;width:100%;}
input[type=checkbox]{width:auto;justify-self:start;}
#status{min-height:18px;color:#bbb;font-size:13px;}
@media(max-width:760px){main{grid-template-columns:1fr;}img{max-height:none;}.row{grid-template-columns:120px minmax(0,1fr);}}
</style>
</head>
<body>
<header><strong>ESP32 Camera Portal</strong><a id="raw-stream" href="#">Stream</a></header>
<main>
<section>
<div class="actions"><button id="capture">Refresh Still</button><a id="save" href="/capture" download="capture.jpg">Save Still</a></div>
<img id="still" alt="camera still">
</section>
<aside class="panel">
<div class="row"><label for="framesize">Frame size</label><select id="framesize" data-var="framesize">
<option value="3">QVGA 320x240</option><option value="4">CIF 400x296</option><option value="5">HVGA 480x320</option><option value="6">VGA 640x480</option><option value="7">SVGA 800x600</option><option value="8">XGA 1024x768</option><option value="9">HD 1280x720</option><option value="10">UXGA 1600x1200</option>
</select></div>
<div class="row"><label for="quality">JPEG quality</label><input id="quality" data-var="quality" type="range" min="4" max="63"></div>
<div class="row"><label for="brightness">Brightness</label><input id="brightness" data-var="brightness" type="range" min="-2" max="2"></div>
<div class="row"><label for="contrast">Contrast</label><input id="contrast" data-var="contrast" type="range" min="-2" max="2"></div>
<div class="row"><label for="saturation">Saturation</label><input id="saturation" data-var="saturation" type="range" min="-2" max="2"></div>
<div class="row"><label for="special_effect">Effect</label><select id="special_effect" data-var="special_effect">
<option value="0">None</option><option value="1">Negative</option><option value="2">Grayscale</option><option value="3">Red tint</option><option value="4">Green tint</option><option value="5">Blue tint</option><option value="6">Sepia</option>
</select></div>
<div class="row"><label for="wb_mode">White balance</label><select id="wb_mode" data-var="wb_mode">
<option value="0">Auto</option><option value="1">Sunny</option><option value="2">Cloudy</option><option value="3">Office</option><option value="4">Home</option>
</select></div>
<div class="row"><label for="awb">AWB</label><input id="awb" data-var="awb" type="checkbox"></div>
<div class="row"><label for="awb_gain">AWB gain</label><input id="awb_gain" data-var="awb_gain" type="checkbox"></div>
<div class="row"><label for="aec">Exposure</label><input id="aec" data-var="aec" type="checkbox"></div>
<div class="row"><label for="aec2">DSP exposure</label><input id="aec2" data-var="aec2" type="checkbox"></div>
<div class="row"><label for="ae_level">AE level</label><input id="ae_level" data-var="ae_level" type="range" min="-2" max="2"></div>
<div class="row"><label for="agc">Gain control</label><input id="agc" data-var="agc" type="checkbox"></div>
<div class="row"><label for="gainceiling">Gain ceiling</label><input id="gainceiling" data-var="gainceiling" type="range" min="0" max="6"></div>
<div class="row"><label for="hmirror">Mirror</label><input id="hmirror" data-var="hmirror" type="checkbox"></div>
<div class="row"><label for="vflip">Flip</label><input id="vflip" data-var="vflip" type="checkbox"></div>
<div class="row"><label for="dcw">Downsize</label><input id="dcw" data-var="dcw" type="checkbox"></div>
<div class="row"><label for="colorbar">Color bar</label><input id="colorbar" data-var="colorbar" type="checkbox"></div>
<div class="row"><label for="led_intensity">LED</label><input id="led_intensity" data-var="led_intensity" type="range" min="0" max="255"></div>
<div id="status"></div>
</aside>
</main>
<script>
const base=location.origin;
const streamBase=location.protocol+'//'+location.hostname+':81';
const still=document.getElementById('still');
const save=document.getElementById('save');
const statusEl=document.getElementById('status');
document.getElementById('raw-stream').href=streamBase+'/stream';
function setStatus(text){statusEl.textContent=text;}
function refreshStill(){const url=base+'/capture?_cb='+Date.now();still.src=url;save.href=url;}
function setControl(el){
  const value=el.type==='checkbox'?(el.checked?1:0):el.value;
  fetch(base+'/control?var='+encodeURIComponent(el.dataset.var)+'&val='+encodeURIComponent(value))
    .then(r=>{if(!r.ok)throw new Error(r.status);setStatus('Updated '+el.dataset.var);})
    .catch(()=>setStatus('Update failed for '+el.dataset.var));
}
fetch(base+'/status').then(r=>r.json()).then(data=>{
  document.querySelectorAll('[data-var]').forEach(el=>{
    if(data[el.dataset.var]===undefined)return;
    if(el.type==='checkbox')el.checked=!!data[el.dataset.var];else el.value=data[el.dataset.var];
  });
  setStatus('Ready');
}).catch(()=>setStatus('Status unavailable'));
document.querySelectorAll('[data-var]').forEach(el=>el.addEventListener('change',()=>setControl(el)));
document.getElementById('capture').addEventListener('click',refreshStill);
refreshStill();
</script>
</body>
</html>
)HTML";

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, portal_index_html, HTTPD_RESP_USE_STRLEN);
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 16;

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t portal_uri = {
    .uri = "/portal",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t cmd_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t bmp_uri = {
    .uri = "/bmp",
    .method = HTTP_GET,
    .handler = bmp_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t xclk_uri = {
    .uri = "/xclk",
    .method = HTTP_GET,
    .handler = xclk_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t reg_uri = {
    .uri = "/reg",
    .method = HTTP_GET,
    .handler = reg_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t greg_uri = {
    .uri = "/greg",
    .method = HTTP_GET,
    .handler = greg_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t pll_uri = {
    .uri = "/pll",
    .method = HTTP_GET,
    .handler = pll_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t win_uri = {
    .uri = "/resolution",
    .method = HTTP_GET,
    .handler = win_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  ra_filter_init(&ra_filter, 20);

  log_i("Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &portal_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &bmp_uri);

    httpd_register_uri_handler(camera_httpd, &xclk_uri);
    httpd_register_uri_handler(camera_httpd, &reg_uri);
    httpd_register_uri_handler(camera_httpd, &greg_uri);
    httpd_register_uri_handler(camera_httpd, &pll_uri);
    httpd_register_uri_handler(camera_httpd, &win_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  log_i("Starting stream server on port: '%d'", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setupLedFlash(int pin) {
#if CONFIG_LED_ILLUMINATOR_ENABLED
  ledcAttach(pin, 5000, 8);
#else
  log_i("LED flash is disabled -> CONFIG_LED_ILLUMINATOR_ENABLED = 0");
#endif
}
