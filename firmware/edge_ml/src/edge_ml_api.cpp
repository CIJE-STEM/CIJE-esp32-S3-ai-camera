#include "edge_ml_api.h"

static uint32_t s_last_stub_log_ms;

bool edge_ml_init() {
  Serial.println("[edge_ml] init (stub — integrate Edge Impulse per firmware/edge_ml/README.md)");
  return true;
}

bool edge_ml_has_model() {
  return false;
}

bool edge_ml_run_fb(camera_fb_t* fb, EdgeMlResult* out) {
  if (!fb || !out) return false;

  out->target_present = false;
  out->norm_cx = 0.5f;
  out->norm_cy = 0.5f;
  out->confidence = 0.0f;

  if (fb->format != PIXFORMAT_JPEG) {
    Serial.println("[edge_ml] stub: expected JPEG from camera");
    return true;
  }

  uint32_t now = millis();
  if (now - s_last_stub_log_ms > 10000) {
    s_last_stub_log_ms = now;
    Serial.println("[edge_ml] stub: no detections — add Edge Impulse Arduino export");
  }

  return true;
}
