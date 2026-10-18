#pragma once

#include <Arduino.h>
#include "esp_camera.h"

/** Normalized target position in image (0..1). (0.5, 0.5) is center. */
struct EdgeMlResult {
  bool target_present;
  float norm_cx;
  float norm_cy;
  float confidence;
};

/**
 * Initialize inference runtime (stub: no-op; replace with Edge Impulse init when integrated).
 * @return true if ready
 */
bool edge_ml_init();

/** False while `edge_ml_run_fb` is still the stub (do not steal frames from /stream). */
bool edge_ml_has_model();

/**
 * Run inference on one camera frame (JPEG from esp_camera).
 * Default stub: always target_present=false. Replace body with Edge Impulse run_classifier/run_fomo etc.
 *
 * @param fb  Framebuffer from esp_camera_fb_get(); must not be null.
 * @param out Filled on success; undefined if returns false.
 * @return false only on hard failure (stub always returns true).
 */
bool edge_ml_run_fb(camera_fb_t* fb, EdgeMlResult* out);
