#pragma once

#include <WebServer.h>
#include "cloud_ml_api.h"
#include "dfr1154_dashboard.h"

/** One pending (or most-recently-decided) cloud-AI review item. */
struct CloudReviewState {
  bool has_pending;
  char path[72];    // full SD path, e.g. "/dataset/inbox/IMG_123_motion.jpg"
  char source[16];  // "motion", "timer", "manual"
  uint32_t evaluated_at_ms;
  CloudMlResult last_result;
  char decision[16]; // "" while pending, else "squirrel"/"not_squirrel"/"uncertain"
  char confirmed_by[8]; // "human" or "auto"
};

/**
 * Wire the cloud review module to the shared dashboard state and the same
 * pan/spray callbacks used by the dashboard (so "center" and "spray" during
 * confirm reuse the exact same servo path as the manual dashboard controls).
 * Loads CloudMlConfig from NVS. Call once during setup(), before registering routes.
 */
void dfr1154_cloud_review_init(DashboardState* dash_state,
                               DashboardPanCallback pan_cb,
                               DashboardSprayCallback spray_cb);

/**
 * Capture a fresh frame, save it to /dataset/inbox, and — if cloud evaluation
 * is enabled in settings — classify it and update the pending-review state.
 * If auto-confirm is on and the result is confident, archives + centers +
 * (if armed) sprays immediately, same as a human clicking "Confirm squirrel".
 *
 * Safe to call when SD is not mounted or cloud eval is disabled (no-ops the
 * relevant parts). Blocking: a cloud call can take a few seconds.
 *
 * @param source short tag: "motion", "timer", or "manual".
 */
void dfr1154_cloud_review_capture_and_evaluate(const char* source);

/** Register the /api/cloud/* routes. Call once alongside dfr1154_dashboard_register. */
void dfr1154_cloud_review_register(WebServer& server);

/** Read-only snapshot of the current pending/last-decided review item. */
const CloudReviewState* dfr1154_cloud_review_get_state();

/** Read-only snapshot of current cloud AI settings. */
const CloudMlConfig* dfr1154_cloud_review_get_config();
