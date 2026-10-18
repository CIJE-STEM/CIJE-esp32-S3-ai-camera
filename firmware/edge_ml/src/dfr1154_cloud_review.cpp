#include "dfr1154_cloud_review.h"
#include "dfr1154_dataset.h"
#include "sqdet_servo.h"

static CloudMlConfig g_cfg;
static CloudReviewState g_review;
static DashboardState* g_dash;
static DashboardPanCallback g_pan_cb;
static DashboardSprayCallback g_spray_cb;
static WebServer* g_srv;

// One cloud call at a time runs in a background task, because an HTTPS round
// trip (several seconds with model thinking) used to block loop() and with it
// the dashboard, pan and spray. The task only fills `result`; loop() applies it
// in dfr1154_cloud_review_service(), so review state, SD and servos are only
// ever touched from loop().
struct CloudJob {
  CloudMlConfig cfg;      // copied at start; the dashboard may edit g_cfg meanwhile
  uint8_t* jpeg;          // PSRAM copy, so the camera frame is released at once
  size_t jpeg_len;
  char path[72];
  char source[16];
  CloudMlResult result;
};
static CloudJob s_job;
static volatile bool s_job_running;  // set by loop(), cleared by loop() after apply
static volatile bool s_job_done;     // set by the task when result is ready

// A photo taken while a call is running waits here (newest wins) and is
// evaluated as soon as the current call finishes, so no capture skips review.
struct QueuedPhoto {
  uint8_t* jpeg;
  size_t jpeg_len;
  char path[72];
  char source[16];
};
static QueuedPhoto s_next;

static String json_escape_min(const String& in) {
  String out;
  out.reserve(in.length() + 4);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out += c;
    }
  }
  return out;
}

static const char* label_folder(const char* label) {
  if (!label) return nullptr;
  if (strcmp(label, "squirrel") == 0) return DFR1154_DS_SQUIRREL;
  if (strcmp(label, "not_squirrel") == 0) return DFR1154_DS_NOT_SQUIRREL;
  if (strcmp(label, "uncertain") == 0) return DFR1154_DS_UNCERTAIN;
  return nullptr;
}

static String build_meta_json(const char* source, const CloudMlResult& r,
                               const char* decision, const char* confirmed_by,
                               uint32_t evaluated_at_ms, uint32_t confirmed_at_ms) {
  String j;
  j.reserve(360);
  j += F("{\"source\":\"");
  j += source ? source : "";
  j += F("\",\"provider\":\"");
  j += cloud_ml_provider_name(r.provider_used);
  j += F("\",\"ai_ok\":");
  j += r.ok ? F("true") : F("false");
  j += F(",\"ai_is_squirrel\":");
  j += r.is_squirrel ? F("true") : F("false");
  j += F(",\"ai_confidence\":");
  j += String(r.confidence, 4);
  j += F(",\"ai_cx\":");
  j += String(r.norm_cx, 4);
  j += F(",\"ai_cy\":");
  j += String(r.norm_cy, 4);
  j += F(",\"ai_reason\":\"");
  j += json_escape_min(r.reason);
  j += F("\",\"ai_error\":\"");
  j += json_escape_min(r.error);
  j += F("\",\"evaluated_at_ms\":");
  j += String(evaluated_at_ms);
  j += F(",\"decision\":\"");
  j += decision ? decision : "";
  j += F("\",\"confirmed_by\":\"");
  j += confirmed_by ? confirmed_by : "";
  j += F("\",\"confirmed_at_ms\":");
  j += String(confirmed_at_ms);
  j += F("}");
  return j;
}

/** Archives the pending item, and (if requested) centers pan and/or sprays via
 *  the same callbacks the dashboard's manual controls use. */
static void do_confirm(const char* decision, bool want_center, bool want_spray,
                        const char* confirmed_by, bool* out_centered, bool* out_sprayed) {
  *out_centered = false;
  *out_sprayed = false;
  if (!g_review.has_pending) return;

  String full(g_review.path);
  int slash = full.lastIndexOf('/');
  String filename = (slash >= 0) ? full.substring(slash + 1) : full;

  if (want_center && g_pan_cb && g_dash) {
    int pan = sqdet_servo_compute_pan(g_dash->pan_center, g_dash->aim_kp,
                                       g_review.last_result.norm_cx);
    g_pan_cb(pan);
    g_dash->last_pan_deg = pan;
    *out_centered = true;
  }

  if (want_spray && g_spray_cb && g_dash && g_dash->armed) {
    uint32_t now = millis();
    bool cooldown_ok = (g_dash->last_spray_at_ms == 0) ||
                        ((now - g_dash->last_spray_at_ms) >= g_dash->spray_cooldown_ms);
    if (cooldown_ok) {
      g_spray_cb();
      g_dash->last_spray_at_ms = now;
      *out_sprayed = true;
    }
  }

  const char* to_dir = label_folder(decision);
  uint32_t confirmed_at_ms = millis();
  if (to_dir) {
    dfr1154_dataset_archive_from_inbox(filename.c_str(), decision);
    String new_path = String(to_dir) + "/" + filename;
    String meta = build_meta_json(g_review.source, g_review.last_result, decision, confirmed_by,
                                   g_review.evaluated_at_ms, confirmed_at_ms);
    dfr1154_dataset_write_meta(new_path.c_str(), meta);
    strncpy(g_review.path, new_path.c_str(), sizeof(g_review.path) - 1);
    g_review.path[sizeof(g_review.path) - 1] = '\0';
  }

  strncpy(g_review.decision, decision, sizeof(g_review.decision) - 1);
  g_review.decision[sizeof(g_review.decision) - 1] = '\0';
  strncpy(g_review.confirmed_by, confirmed_by, sizeof(g_review.confirmed_by) - 1);
  g_review.confirmed_by[sizeof(g_review.confirmed_by) - 1] = '\0';
  g_review.has_pending = false;
}

void dfr1154_cloud_review_init(DashboardState* dash_state, DashboardPanCallback pan_cb,
                               DashboardSprayCallback spray_cb) {
  g_dash = dash_state;
  g_pan_cb = pan_cb;
  g_spray_cb = spray_cb;
  cloud_ml_nvs_load(&g_cfg);
  memset(&g_review, 0, sizeof(g_review));
}

static void cloud_job_task(void*) {
  camera_fb_t fb = {};
  fb.buf = s_job.jpeg;
  fb.len = s_job.jpeg_len;
  fb.format = PIXFORMAT_JPEG;
  cloud_ml_classify_fb(&s_job.cfg, &fb, &s_job.result);
  __atomic_store_n(&s_job_done, true, __ATOMIC_RELEASE);
  vTaskDelete(nullptr);
}

/** Start the background call for a JPEG copy (takes ownership of `jpeg`). */
static void start_job(uint8_t* jpeg, size_t len, const char* path, const char* src) {
  s_job.jpeg = jpeg;
  s_job.jpeg_len = len;
  s_job.cfg = g_cfg;
  s_job.result = CloudMlResult();
  strncpy(s_job.path, path, sizeof(s_job.path) - 1);
  s_job.path[sizeof(s_job.path) - 1] = '\0';
  strncpy(s_job.source, src, sizeof(s_job.source) - 1);
  s_job.source[sizeof(s_job.source) - 1] = '\0';

  s_job_done = false;
  s_job_running = true;
  // TLS handshake needs a big stack. Core 0, priority 1: off loop()'s core.
  if (xTaskCreatePinnedToCore(cloud_job_task, "cloud_ai", 16384, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("[cloud] task create failed — skipping AI review");
    free(s_job.jpeg);
    s_job.jpeg = nullptr;
    s_job_running = false;
  }
}

void dfr1154_cloud_review_capture_and_evaluate(const char* source) {
  const char* src = (source && source[0]) ? source : "manual";

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;

  char path[72];
  bool saved = dfr1154_dataset_capture_fb(fb, src, path, sizeof(path));
  if (!saved || !g_cfg.eval_enabled) {
    // Cloud AI off (or SD failed) — behaves exactly like a plain dataset capture.
    esp_camera_fb_return(fb);
    return;
  }
  uint8_t* copy = (uint8_t*)ps_malloc(fb->len);
  if (!copy) {
    Serial.println("[cloud] no PSRAM for JPEG copy — skipping AI review");
    esp_camera_fb_return(fb);
    return;
  }
  memcpy(copy, fb->buf, fb->len);
  size_t len = fb->len;
  esp_camera_fb_return(fb);

  if (!s_job_running) {
    start_job(copy, len, path, src);
    return;
  }
  if (s_next.jpeg) {
    Serial.printf("[cloud] replacing queued %s with newer photo\n", s_next.path);
    free(s_next.jpeg);
  }
  s_next.jpeg = copy;
  s_next.jpeg_len = len;
  strncpy(s_next.path, path, sizeof(s_next.path) - 1);
  s_next.path[sizeof(s_next.path) - 1] = '\0';
  strncpy(s_next.source, src, sizeof(s_next.source) - 1);
  s_next.source[sizeof(s_next.source) - 1] = '\0';
  Serial.printf("[cloud] busy — queued %s for AI review\n", path);
}

void dfr1154_cloud_review_service() {
  if (!s_job_running || !__atomic_load_n(&s_job_done, __ATOMIC_ACQUIRE)) return;
  free(s_job.jpeg);
  s_job.jpeg = nullptr;
  const CloudMlResult& result = s_job.result;

  g_review.has_pending = true;
  strncpy(g_review.path, s_job.path, sizeof(g_review.path) - 1);
  g_review.path[sizeof(g_review.path) - 1] = '\0';
  strncpy(g_review.source, s_job.source, sizeof(g_review.source) - 1);
  g_review.source[sizeof(g_review.source) - 1] = '\0';
  g_review.evaluated_at_ms = millis();
  g_review.last_result = result;
  g_review.decision[0] = '\0';
  g_review.confirmed_by[0] = '\0';

  dfr1154_dataset_write_meta(
      s_job.path, build_meta_json(s_job.source, result, "", "", g_review.evaluated_at_ms, 0));

  if (result.ok && result.is_squirrel && g_cfg.auto_confirm &&
      result.confidence >= g_cfg.confidence_threshold) {
    bool centered = false, sprayed = false;
    do_confirm("squirrel", true, true, "auto", &centered, &sprayed);
  }
  s_job_running = false;

  if (s_next.jpeg) {
    QueuedPhoto q = s_next;
    s_next.jpeg = nullptr;
    start_job(q.jpeg, q.jpeg_len, q.path, q.source);
  }
}

static String build_status_json() {
  String j;
  j.reserve(900);
  j += F("{\"ok\":true,\"provider\":\"");
  j += cloud_ml_provider_name(g_cfg.provider);
  j += F("\",\"eval_enabled\":");
  j += g_cfg.eval_enabled ? F("true") : F("false");
  j += F(",\"auto_confirm\":");
  j += g_cfg.auto_confirm ? F("true") : F("false");
  j += F(",\"confidence_threshold\":");
  j += String(g_cfg.confidence_threshold, 3);
  j += F(",\"openai_model\":\"");
  j += g_cfg.openai_model;
  j += F("\",\"anthropic_model\":\"");
  j += g_cfg.anthropic_model;
  j += F("\",\"openai_key_set\":");
  j += (g_cfg.openai_api_key.length() >= 10) ? F("true") : F("false");
  j += F(",\"anthropic_key_set\":");
  j += (g_cfg.anthropic_api_key.length() >= 10) ? F("true") : F("false");
  j += F(",\"evaluating\":");
  j += s_job_running ? F("true") : F("false");
  j += F(",\"queued\":");
  j += s_next.jpeg ? F("true") : F("false");
  j += F(",\"pending\":");
  j += g_review.has_pending ? F("true") : F("false");
  j += F(",\"path\":\"");
  j += g_review.path;
  j += F("\",\"source\":\"");
  j += g_review.source;
  j += F("\",\"evaluated_at_ms\":");
  j += String(g_review.evaluated_at_ms);
  j += F(",\"decision\":\"");
  j += g_review.decision;
  j += F("\",\"confirmed_by\":\"");
  j += g_review.confirmed_by;
  j += F("\",\"ai_ok\":");
  j += g_review.last_result.ok ? F("true") : F("false");
  j += F(",\"ai_is_squirrel\":");
  j += g_review.last_result.is_squirrel ? F("true") : F("false");
  j += F(",\"ai_confidence\":");
  j += String(g_review.last_result.confidence, 4);
  j += F(",\"ai_cx\":");
  j += String(g_review.last_result.norm_cx, 4);
  j += F(",\"ai_cy\":");
  j += String(g_review.last_result.norm_cy, 4);
  j += F(",\"ai_reason\":\"");
  j += json_escape_min(g_review.last_result.reason);
  j += F("\",\"ai_error\":\"");
  j += json_escape_min(g_review.last_result.error);
  j += F("\",\"armed\":");
  j += (g_dash && g_dash->armed) ? F("true") : F("false");
  j += F("}");
  return j;
}

static void handleApiCloudStatus() {
  g_srv->send(200, "application/json", build_status_json());
}

static void handleApiCloudConfig() {
  if (g_srv->hasArg("provider")) {
    String v = g_srv->arg("provider");
    g_cfg.provider = (v == "anthropic") ? CLOUD_ML_PROVIDER_ANTHROPIC : CLOUD_ML_PROVIDER_OPENAI;
  }
  if (g_srv->hasArg("eval_enabled")) {
    String v = g_srv->arg("eval_enabled");
    g_cfg.eval_enabled = (v == "1" || v == "true" || v == "on");
  }
  if (g_srv->hasArg("auto_confirm")) {
    String v = g_srv->arg("auto_confirm");
    g_cfg.auto_confirm = (v == "1" || v == "true" || v == "on");
  }
  if (g_srv->hasArg("confidence_threshold")) {
    float f = g_srv->arg("confidence_threshold").toFloat();
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    g_cfg.confidence_threshold = f;
  }
  if (g_srv->hasArg("openai_key") && g_srv->arg("openai_key").length() > 0) {
    g_cfg.openai_api_key = g_srv->arg("openai_key");
  }
  if (g_srv->hasArg("anthropic_key") && g_srv->arg("anthropic_key").length() > 0) {
    g_cfg.anthropic_api_key = g_srv->arg("anthropic_key");
  }
  if (g_srv->hasArg("openai_model") && g_srv->arg("openai_model").length() > 0) {
    g_cfg.openai_model = g_srv->arg("openai_model");
  }
  if (g_srv->hasArg("anthropic_model") && g_srv->arg("anthropic_model").length() > 0) {
    g_cfg.anthropic_model = g_srv->arg("anthropic_model");
  }
  cloud_ml_nvs_save(&g_cfg);
  g_srv->send(200, "application/json", build_status_json());
}

static void handleApiCloudEvaluate() {
  dfr1154_cloud_review_capture_and_evaluate("manual");
  g_srv->send(200, "application/json", build_status_json());
}

static void handleApiCloudConfirm() {
  if (!g_srv->hasArg("decision")) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"decision\"}");
    return;
  }
  String decision = g_srv->arg("decision");
  if (decision != "squirrel" && decision != "not_squirrel" && decision != "uncertain") {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_decision\"}");
    return;
  }
  if (!g_review.has_pending) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"no_pending\"}");
    return;
  }

  bool want_center = (decision == "squirrel");
  bool want_spray = (decision == "squirrel");
  if (g_srv->hasArg("center")) {
    String v = g_srv->arg("center");
    want_center = (v == "1" || v == "true" || v == "on");
  }
  if (g_srv->hasArg("spray")) {
    String v = g_srv->arg("spray");
    want_spray = (v == "1" || v == "true" || v == "on");
  }

  bool centered = false, sprayed = false;
  do_confirm(decision.c_str(), want_center, want_spray, "human", &centered, &sprayed);

  String j = F("{\"ok\":true,\"centered\":");
  j += centered ? F("true") : F("false");
  j += F(",\"sprayed\":");
  j += sprayed ? F("true") : F("false");
  j += F(",\"path\":\"");
  j += g_review.path;
  j += F("\"}");
  g_srv->send(200, "application/json", j);
}

void dfr1154_cloud_review_register(WebServer& server) {
  g_srv = &server;
  server.on("/api/cloud/status", HTTP_GET, handleApiCloudStatus);
  server.on("/api/cloud/config", HTTP_POST, handleApiCloudConfig);
  server.on("/api/cloud/evaluate", HTTP_POST, handleApiCloudEvaluate);
  server.on("/api/cloud/confirm", HTTP_POST, handleApiCloudConfirm);
}

const CloudReviewState* dfr1154_cloud_review_get_state() { return &g_review; }
const CloudMlConfig* dfr1154_cloud_review_get_config() { return &g_cfg; }
