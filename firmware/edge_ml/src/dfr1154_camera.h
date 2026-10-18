#pragma once

#include <Arduino.h>
#include "esp_camera.h"

/** Initialize OV2640 with the same defaults as ESP32_AI_Camera v3.2.5 (JPEG, SVGA). */
bool dfr1154_camera_init();

/** True after a successful init. */
bool dfr1154_camera_ok();

/** Save one JPEG frame to SD_MMC (path like "/triggers/TRG_123.jpg"). Returns false if SD or write fails. */
bool dfr1154_save_jpeg_to_sd(const char* absolute_path, camera_fb_t* fb);
