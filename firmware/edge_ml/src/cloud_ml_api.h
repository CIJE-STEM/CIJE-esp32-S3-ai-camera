#pragma once

#include <Arduino.h>
#include "esp_camera.h"

/** Which cloud vision provider to call. */
#define CLOUD_ML_PROVIDER_OPENAI    0
#define CLOUD_ML_PROVIDER_ANTHROPIC 1

/** NVS-backed settings (namespace "cloudml"). */
struct CloudMlConfig {
  uint8_t provider;
  /** Master switch — no HTTPS calls are made at all when false (avoids surprise cost). */
  bool eval_enabled;
  /** true: auto-center+archive+spray when confident; false: always wait for dashboard confirm. */
  bool auto_confirm;
  float confidence_threshold;

  String openai_api_key;
  String anthropic_api_key;
  String openai_model;
  String anthropic_model;
};

/** Result of one classification call. */
struct CloudMlResult {
  /** true if the HTTPS call + JSON parse succeeded (independent of the verdict). */
  bool ok;
  bool is_squirrel;
  /** Model's self-reported confidence, 0..1. */
  float confidence;
  /** Best-effort normalized center of the squirrel in-frame (0..1); 0.5,0.5 if unknown. */
  float norm_cx;
  float norm_cy;
  String reason;
  /** Populated when ok == false. */
  String error;
  uint8_t provider_used;
};

void cloud_ml_apply_defaults(CloudMlConfig* c);

/** Load settings from NVS (namespace "cloudml"), or apply + save defaults on first boot. */
void cloud_ml_nvs_load(CloudMlConfig* c);

/** Persist settings to NVS. */
void cloud_ml_nvs_save(const CloudMlConfig* c);

/**
 * Classify one JPEG camera frame with the configured cloud provider.
 * Blocking — a round trip typically takes 1-6 seconds. Requires WiFi connected
 * and cfg->eval_enabled; returns false immediately (out->error set) otherwise.
 */
bool cloud_ml_classify_fb(const CloudMlConfig* cfg, camera_fb_t* fb, CloudMlResult* out);

/** Human-readable provider name ("openai" / "anthropic"). */
const char* cloud_ml_provider_name(uint8_t provider);
