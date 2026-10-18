#pragma once

#include <WebServer.h>
#include "edge_ml_api.h"

/** NVS namespace "sqdet" — calibration + arm state (persisted). */
struct DashboardState {
  bool armed;
  int pan_center;
  float aim_kp;
  float center_deadband;
  float confidence_threshold;
  uint32_t spray_cooldown_ms;
  uint32_t ml_interval_ms;
  /** If true, pan follows target even when disarmed (spray still blocked). */
  bool track_when_disarmed;

  /** Auto-capture interval (ms); 0 = timer off. */
  uint32_t capture_timer_ms;
  /** Frames to save per ACEBOTT MOTION event. */
  uint8_t motion_burst_count;

  // --- Filled by sketch each frame (read by GET /api/status) ---
  bool sd_ok;
  uint32_t last_infer_at_ms;
  uint32_t last_spray_at_ms;
  int last_pan_deg;
  EdgeMlResult last_ml;
  bool in_deadband;
};

typedef void (*DashboardPanCallback)(int degrees);
typedef void (*DashboardSprayCallback)(void);

/** Apply defaults (does not write NVS). */
void dfr1154_dashboard_apply_defaults(DashboardState* s);

/** Load from NVS or apply defaults and save. */
void dfr1154_dashboard_nvs_load(DashboardState* s);

/** Persist current calibration fields (armed + tuning). */
void dfr1154_dashboard_nvs_save(const DashboardState* s);

/** Clear NVS namespace and reload defaults. */
void dfr1154_dashboard_nvs_factory_reset(DashboardState* s);

/**
 * Register dashboard, dataset, stream, and control API routes. Call after WiFi is up.
 *
 * @param pan_cb   optional — pan/yaw servo (slider or pan test).
 * @param spray_cb optional — manual spray test from dashboard.
 */
void dfr1154_dashboard_register(WebServer& server, DashboardState* state,
                                DashboardPanCallback pan_cb,
                                DashboardSprayCallback spray_cb);
