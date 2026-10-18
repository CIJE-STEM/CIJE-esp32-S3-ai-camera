#include "dfr1154_camera.h"
#include "dfr1154_camera_pins.h"
#include "SD_MMC.h"
#include "FS.h"

static bool s_ok;

bool dfr1154_camera_init() {
  s_ok = false;

  // Match ESP32_AI_Camera_v3_2_5: JPEG SVGA, one framebuffer.
  // Two buffers + a long WiFi connect (or holding a frame during HTTP send)
  // is what produced cam_hal: FB-OVF and a dead /stream.
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    Serial.println("dfr1154_camera_init: PSRAM OK — SVGA, 1 framebuffer");
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.frame_size = FRAMESIZE_VGA;
    Serial.println("dfr1154_camera_init: no PSRAM — VGA, 1 framebuffer");
    Serial.println("  Enable OPI PSRAM in Arduino board settings for SVGA.");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("dfr1154_camera_init failed: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 0);
    s->set_hmirror(s, 1);
  }

  s_ok = true;
  Serial.println("dfr1154_camera_init: OK");
  return true;
}

bool dfr1154_camera_ok() {
  return s_ok;
}

bool dfr1154_save_jpeg_to_sd(const char* absolute_path, camera_fb_t* fb) {
  if (!fb || fb->format != PIXFORMAT_JPEG) return false;
  File file = SD_MMC.open(absolute_path, FILE_WRITE);
  if (!file) return false;
  size_t w = file.write(fb->buf, fb->len);
  file.close();
  return w == fb->len;
}
