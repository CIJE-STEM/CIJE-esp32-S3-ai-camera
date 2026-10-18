/**
 * Squirrel deterrent + dataset collector — DFR1154 + edge_ml library.
 *
 * - Dashboard: live stream, pan slider, spray test, dataset labeling
 * - ACEBOTT UART (GPIO 43/44): MOTION triggers + remote servos
 * - SD: /dataset/inbox, squirrel, not_squirrel, uncertain
 *
 * See HOWTO.md for wiring, SD prep, and ACEBOTT companion sketch.
 */

#include <WiFi.h>
#include <WebServer.h>

#include <esp_camera.h>
#include <SD_MMC.h>
#include <FS.h>

#include <dfr1154_camera.h>
#include <dfr1154_camera_pins.h>
#include <dfr1154_dashboard.h>
#include <dfr1154_dataset.h>
#include <dfr1154_acebott_link.h>
#include <dfr1154_cloud_review.h>
#include <edge_ml_api.h>

#include "servo_backend_config.h"
#include "wifi_secrets.h"

// ---------------------------------------------------------------------------
#define ENABLE_HTTP_DEBUG 1
#define ENABLE_ACEBOTT_UART 1

// Minimum gap between MOTION-triggered captures. The ACEBOTT PIR can fire every
// 4 s while anything moves; 30 s keeps one animal visit from flooding the SD card.
#define MOTION_CAPTURE_COOLDOWN_MS 30000

// ---------------------------------------------------------------------------
#if ENABLE_HTTP_DEBUG
WebServer server(80);
#endif

DashboardState g_dash;

static uint32_t s_last_ml_ms;
static uint32_t s_last_timer_capture_ms;
static uint32_t s_last_motion_ms;
static uint8_t s_motion_burst_left;
static bool sd_ok;
static bool s_cam_inited;

static void mount_sd() {
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  sd_ok = SD_MMC.begin("/sdcard", true);
  if (!sd_ok) {
    Serial.println("SD_MMC mount failed — captures disabled");
    return;
  }
  if (!dfr1154_dataset_init()) {
    Serial.println("SD dataset folders failed");
  }
  Serial.println("SD_MMC OK — /dataset/* ready");
}

static void capture_to_inbox(const char* source) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;
  char path[72];
  if (dfr1154_dataset_capture_fb(fb, source, path, sizeof(path))) {
    Serial.printf("Dataset capture: %s\n", path);
  }
  esp_camera_fb_return(fb);
}

static void on_acebott_motion() {
  uint32_t now = millis();
  if (s_last_motion_ms != 0 && now - s_last_motion_ms < MOTION_CAPTURE_COOLDOWN_MS) return;
  s_last_motion_ms = now;
  s_motion_burst_left = g_dash.motion_burst_count;
  if (s_motion_burst_left < 1) s_motion_burst_left = 1;
  Serial.println("Motion event — starting burst capture");

  // One extra, separately-tracked capture that also runs the cloud AI check
  // (if enabled on the dashboard) and feeds the "AI squirrel review" tab.
  // The burst above keeps collecting raw training frames regardless.
  dfr1154_cloud_review_capture_and_evaluate("motion");
}

static void service_motion_burst() {
  if (s_motion_burst_left == 0) return;
  static uint32_t last_burst_ms;
  uint32_t now = millis();
  if (now - last_burst_ms < 450) return;
  last_burst_ms = now;
  capture_to_inbox("motion");
  s_motion_burst_left--;
}

static void service_timer_capture() {
  if (g_dash.capture_timer_ms == 0) return;
  uint32_t now = millis();
  if (s_last_timer_capture_ms == 0) {
    s_last_timer_capture_ms = now;
    return;
  }
  if (now - s_last_timer_capture_ms < g_dash.capture_timer_ms) return;
  s_last_timer_capture_ms = now;
  dfr1154_cloud_review_capture_and_evaluate("timer");
}

static void on_dashboard_pan(int deg) {
  sqdet_servo_set_pan(deg);
  g_dash.last_pan_deg = deg;
}

static void on_dashboard_spray_test() {
  Serial.println("Dashboard spray test");
  sqdet_servo_trigger_spray();
  g_dash.last_spray_at_ms = millis();
}

#if ENABLE_HTTP_DEBUG
static void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("----------------------------------------"));
    Serial.print(F("Dashboard: http://"));
    Serial.print(WiFi.localIP());
    Serial.println(F("/dash"));
    Serial.println(F("----------------------------------------"));
  } else {
    Serial.println("WiFi failed — HTTP dashboard unavailable");
  }
}
#endif

static bool can_spray() {
  if (g_dash.last_spray_at_ms == 0) return true;
  return (millis() - g_dash.last_spray_at_ms) >= g_dash.spray_cooldown_ms;
}

static void trigger_spray(camera_fb_t* fb) {
  if (!g_dash.armed) return;
  g_dash.last_spray_at_ms = millis();

  if (sd_ok && fb && fb->format == PIXFORMAT_JPEG) {
    char path[64];
    snprintf(path, sizeof(path), "/triggers/TRG_%lu.jpg", (unsigned long)g_dash.last_spray_at_ms);
    if (dfr1154_save_jpeg_to_sd(path, fb)) {
      Serial.printf("Saved %s\n", path);
    }
  }

  sqdet_servo_trigger_spray();
}

void setup() {
  Serial.begin(115200);
  // Never wait on USB serial. If the board is plugged into a computer with no
  // Serial Monitor reading, each print otherwise blocks until a timeout, which
  // made pan/spray/ping clicks take seconds.
  Serial.setTxTimeoutMs(0);
  delay(800);
  Serial.println("\n=== squirrel_deterrent + dataset collector ===");

  mount_sd();
  dfr1154_dashboard_nvs_load(&g_dash);
  g_dash.sd_ok = sd_ok;

  edge_ml_init();

#if ENABLE_ACEBOTT_UART
  dfr1154_acebott_link_init();
#endif

  SqdetServoConfig servo_cfg = sqdet_servo_config_from_defines();
  if (!sqdet_servo_init(&servo_cfg, g_dash.pan_center)) {
    Serial.println("Servo init failed — check servo_backend_config.h and ACEBOTT wiring");
  }
  g_dash.last_pan_deg = g_dash.pan_center;

#if ENABLE_HTTP_DEBUG
  connect_wifi();
  if (WiFi.status() == WL_CONNECTED) {
    dfr1154_dashboard_register(server, &g_dash, on_dashboard_pan, on_dashboard_spray_test);
    server.begin();
    Serial.println("HTTP up — /dash will answer now; camera starts in loop()");
  }
#endif
}

void loop() {
#if ENABLE_HTTP_DEBUG
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
#endif

  if (sd_ok) {
    dfr1154_dataset_service();  // background delete after "Delete all"
  }
  dfr1154_cloud_review_service();  // apply a finished background AI verdict

  if (!dfr1154_camera_ok()) {
    static uint32_t s_http_grace_ms;
    static uint32_t s_last_cam_try_ms;
    if (s_http_grace_ms == 0) {
      s_http_grace_ms = millis();
    }
    // Answer /dash before the first (blocking) camera init.
    if (millis() - s_http_grace_ms < 800) {
      return;
    }
    if (s_last_cam_try_ms != 0 && millis() - s_last_cam_try_ms < 4000) {
      return;
    }
    s_last_cam_try_ms = millis();
    dfr1154_camera_init();
    s_cam_inited = dfr1154_camera_ok();
  }

#if ENABLE_ACEBOTT_UART
  dfr1154_acebott_link_poll(on_acebott_motion);
#endif

  g_dash.sd_ok = sd_ok;

  if (sd_ok) {
    service_timer_capture();
    service_motion_burst();
  }

  if (!edge_ml_has_model()) {
    return;
  }

  uint32_t now = millis();
  if (now - s_last_ml_ms < g_dash.ml_interval_ms) {
    return;
  }
  s_last_ml_ms = now;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    return;
  }

  EdgeMlResult r;
  edge_ml_run_fb(fb, &r);
  g_dash.last_ml = r;
  g_dash.last_infer_at_ms = millis();

  const bool allow_track = g_dash.armed || g_dash.track_when_disarmed;

  if (r.target_present && r.confidence >= g_dash.confidence_threshold && allow_track) {
    float err = r.norm_cx - 0.5f;
    g_dash.in_deadband = fabsf(err) < g_dash.center_deadband;

    int pan = (int)(g_dash.pan_center + err * g_dash.aim_kp);
    pan = constrain(pan, 0, 180);
    sqdet_servo_set_pan(pan);
    g_dash.last_pan_deg = pan;

    if (g_dash.in_deadband && g_dash.armed && can_spray()) {
      trigger_spray(fb);
    }
  } else {
    g_dash.in_deadband = false;
  }

  esp_camera_fb_return(fb);
}
