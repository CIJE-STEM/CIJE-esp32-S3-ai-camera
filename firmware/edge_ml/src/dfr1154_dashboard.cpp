#include "dfr1154_dashboard.h"
#include "dfr1154_http_debug.h"
#include "dfr1154_dataset.h"
#include "dfr1154_cloud_review.h"
#include "dfr1154_acebott_link.h"
#include "dfr1154_camera.h"

#include <WiFi.h>
#include <Preferences.h>

static DashboardState* g_st;
static DashboardPanCallback g_pan_cb;
static DashboardSprayCallback g_spray_cb;
static WebServer* g_srv;

static const char kNs[] = "sqdet";

void dfr1154_dashboard_apply_defaults(DashboardState* s) {
  s->armed = false;
  s->pan_center = 90;
  s->aim_kp = 72.0f;
  s->center_deadband = 0.10f;
  s->confidence_threshold = 0.60f;
  s->spray_cooldown_ms = 15000;
  s->ml_interval_ms = 200;
  s->track_when_disarmed = false;
  s->capture_timer_ms = 300000;
  s->motion_burst_count = 3;

  s->sd_ok = false;
  s->last_infer_at_ms = 0;
  s->last_spray_at_ms = 0;
  s->last_pan_deg = s->pan_center;
  s->last_ml.target_present = false;
  s->last_ml.norm_cx = 0.5f;
  s->last_ml.norm_cy = 0.5f;
  s->last_ml.confidence = 0.0f;
  s->in_deadband = false;
}

void dfr1154_dashboard_nvs_load(DashboardState* s) {
  Preferences p;
  if (!p.begin(kNs, true)) {
    dfr1154_dashboard_apply_defaults(s);
    return;
  }

  if (!p.getBool("initv1", false)) {
    p.end();
    dfr1154_dashboard_apply_defaults(s);
    dfr1154_dashboard_nvs_save(s);
    return;
  }

  s->armed = p.getBool("armed", false);
  s->pan_center = p.getInt("p_ctr", 90);
  s->aim_kp = p.getFloat("aim_kp", 72.0f);
  s->center_deadband = p.getFloat("deadbd", 0.10f);
  s->confidence_threshold = p.getFloat("conf_th", 0.60f);
  s->spray_cooldown_ms = p.getUInt("cd_ms", 15000);
  s->ml_interval_ms = p.getUInt("ml_int", 200);
  s->track_when_disarmed = p.getBool("track_d", false);
  s->capture_timer_ms = p.getUInt("cap_tmr", 300000);
  s->motion_burst_count = (uint8_t)p.getUInt("mot_burst", 3);
  if (s->motion_burst_count < 1) s->motion_burst_count = 1;
  if (s->motion_burst_count > 10) s->motion_burst_count = 10;
  p.end();

  s->last_pan_deg = s->pan_center;
}

void dfr1154_dashboard_nvs_save(const DashboardState* s) {
  Preferences p;
  if (!p.begin(kNs, false)) return;

  p.putBool("initv1", true);
  p.putBool("armed", s->armed);
  p.putInt("p_ctr", s->pan_center);
  p.putFloat("aim_kp", s->aim_kp);
  p.putFloat("deadbd", s->center_deadband);
  p.putFloat("conf_th", s->confidence_threshold);
  p.putUInt("cd_ms", s->spray_cooldown_ms);
  p.putUInt("ml_int", s->ml_interval_ms);
  p.putBool("track_d", s->track_when_disarmed);
  p.putUInt("cap_tmr", s->capture_timer_ms);
  p.putUInt("mot_burst", s->motion_burst_count);
  p.end();
}

void dfr1154_dashboard_nvs_factory_reset(DashboardState* s) {
  Preferences p;
  if (p.begin(kNs, false)) {
    p.clear();
    p.end();
  }
  dfr1154_dashboard_apply_defaults(s);
  dfr1154_dashboard_nvs_save(s);
}

static void handleRootRedirect() {
  g_srv->sendHeader("Location", "/dash", true);
  g_srv->send(302, "text/plain", "");
}

static void handleApiStatus() {
  DashboardState* st = g_st;
  if (!st) {
    g_srv->send(500, "application/json", "{\"ok\":false}");
    return;
  }

  uint32_t now = millis();
  uint32_t infer_ago = (st->last_infer_at_ms > 0) ? (now - st->last_infer_at_ms) : 999999;
  uint32_t cd_rem = 0;
  if (st->last_spray_at_ms > 0) {
    uint32_t elapsed = now - st->last_spray_at_ms;
    if (elapsed < st->spray_cooldown_ms) {
      cd_rem = st->spray_cooldown_ms - elapsed;
    }
  }

  AcebottLinkStatus ace;
  dfr1154_acebott_link_get_status(&ace);

  String j;
  j.reserve(1100);
  j = F("{\"ok\":true,");
  j += F("\"armed\":");
  j += st->armed ? F("true") : F("false");
  j += F(",\"wifi_ok\":");
  j += (WiFi.status() == WL_CONNECTED) ? F("true") : F("false");
  j += F(",\"ip\":\"");
  if (WiFi.status() == WL_CONNECTED) {
    j += WiFi.localIP().toString();
  }
  j += F("\",\"sd_ok\":");
  j += st->sd_ok ? F("true") : F("false");
  j += F(",\"camera_ok\":");
  j += dfr1154_camera_ok() ? F("true") : F("false");
  j += F(",\"infer_ago_ms\":");
  j += String(infer_ago);
  j += F(",\"target_present\":");
  j += st->last_ml.target_present ? F("true") : F("false");
  j += F(",\"confidence\":");
  j += String(st->last_ml.confidence, 4);
  j += F(",\"norm_cx\":");
  j += String(st->last_ml.norm_cx, 4);
  j += F(",\"norm_cy\":");
  j += String(st->last_ml.norm_cy, 4);
  j += F(",\"pan_deg\":");
  j += String(st->last_pan_deg);
  j += F(",\"spray_cooldown_remaining_ms\":");
  j += String(cd_rem);
  j += F(",\"in_deadband\":");
  j += st->in_deadband ? F("true") : F("false");
  j += F(",\"cal_pan_center\":");
  j += String(st->pan_center);
  j += F(",\"cal_aim_kp\":");
  j += String(st->aim_kp, 2);
  j += F(",\"cal_deadband\":");
  j += String(st->center_deadband, 3);
  j += F(",\"cal_confidence\":");
  j += String(st->confidence_threshold, 3);
  j += F(",\"cal_cooldown_ms\":");
  j += String(st->spray_cooldown_ms);
  j += F(",\"cal_ml_interval_ms\":");
  j += String(st->ml_interval_ms);
  j += F(",\"cal_track_disarmed\":");
  j += st->track_when_disarmed ? F("true") : F("false");
  j += F(",\"cal_capture_timer_ms\":");
  j += String(st->capture_timer_ms);
  j += F(",\"cal_motion_burst_count\":");
  j += String(st->motion_burst_count);
  j += F(",\"acebott_ok\":");
  j += ace.heard_recent ? F("true") : F("false");
  j += F(",\"acebott_rx_ago_ms\":");
  j += String(ace.rx_ago_ms);
  j += F(",\"acebott_ack_ago_ms\":");
  j += String(ace.ack_ago_ms);
  j += F(",\"acebott_rx_count\":");
  j += String(ace.rx_count);
  j += F(",\"acebott_tx_count\":");
  j += String(ace.tx_count);
  j += F(",\"acebott_ack_count\":");
  j += String(ace.ack_count);
  j += F(",\"acebott_last_rx\":\"");
  j += ace.last_rx;
  j += F("\",\"acebott_last_tx\":\"");
  j += ace.last_tx;
  j += F("\"}");

  g_srv->send(200, "application/json", j);
}

static void handleApiCal() {
  if (!g_st) {
    g_srv->send(500, "application/json", "{\"ok\":false}");
    return;
  }
  DashboardState* st = g_st;

  if (g_srv->hasArg("pan_center")) {
    st->pan_center = constrain(g_srv->arg("pan_center").toInt(), 0, 180);
  }
  if (g_srv->hasArg("aim_kp")) {
    st->aim_kp = g_srv->arg("aim_kp").toFloat();
    if (st->aim_kp < 0.0f) st->aim_kp = 0.0f;
    if (st->aim_kp > 200.0f) st->aim_kp = 200.0f;
  }
  if (g_srv->hasArg("center_deadband")) {
    st->center_deadband = g_srv->arg("center_deadband").toFloat();
    if (st->center_deadband < 0.01f) st->center_deadband = 0.01f;
    if (st->center_deadband > 0.45f) st->center_deadband = 0.45f;
  }
  if (g_srv->hasArg("confidence_threshold")) {
    st->confidence_threshold = g_srv->arg("confidence_threshold").toFloat();
    if (st->confidence_threshold < 0.0f) st->confidence_threshold = 0.0f;
    if (st->confidence_threshold > 1.0f) st->confidence_threshold = 1.0f;
  }
  if (g_srv->hasArg("spray_cooldown_ms")) {
    st->spray_cooldown_ms = (uint32_t)g_srv->arg("spray_cooldown_ms").toInt();
    if (st->spray_cooldown_ms < 1000) st->spray_cooldown_ms = 1000;
    if (st->spray_cooldown_ms > 600000) st->spray_cooldown_ms = 600000;
  }
  if (g_srv->hasArg("ml_interval_ms")) {
    st->ml_interval_ms = (uint32_t)g_srv->arg("ml_interval_ms").toInt();
    if (st->ml_interval_ms < 50) st->ml_interval_ms = 50;
    if (st->ml_interval_ms > 5000) st->ml_interval_ms = 5000;
  }
  if (g_srv->hasArg("track_when_disarmed")) {
    String v = g_srv->arg("track_when_disarmed");
    st->track_when_disarmed = (v == "1" || v == "true" || v == "on");
  }
  if (g_srv->hasArg("capture_timer_ms")) {
    st->capture_timer_ms = (uint32_t)g_srv->arg("capture_timer_ms").toInt();
    if (st->capture_timer_ms > 0 && st->capture_timer_ms < 10000) {
      st->capture_timer_ms = 10000;
    }
  }
  if (g_srv->hasArg("motion_burst_count")) {
    int n = g_srv->arg("motion_burst_count").toInt();
    st->motion_burst_count = (uint8_t)constrain(n, 1, 10);
  }

  dfr1154_dashboard_nvs_save(st);
  g_srv->send(200, "application/json", "{\"ok\":true}");
}

static void handleApiArm() {
  if (!g_st) {
    g_srv->send(500, "application/json", "{\"ok\":false}");
    return;
  }
  if (g_srv->hasArg("armed")) {
    String v = g_srv->arg("armed");
    g_st->armed = (v == "1" || v == "true" || v == "on");
  }
  dfr1154_dashboard_nvs_save(g_st);
  g_srv->send(200, "application/json", "{\"ok\":true}");
}

static void handleApiPan() {
  if (!g_st || !g_pan_cb) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"no_pan_cb\"}");
    return;
  }
  if (g_srv->hasArg("pan_deg")) {
    int d = constrain(g_srv->arg("pan_deg").toInt(), 0, 180);
    g_pan_cb(d);
    g_st->last_pan_deg = d;
  }
  g_srv->send(200, "application/json", "{\"ok\":true}");
}

static void handleApiSpray() {
  if (!g_spray_cb) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"no_spray_cb\"}");
    return;
  }
  AcebottLinkStatus before;
  dfr1154_acebott_link_get_status(&before);
  g_spray_cb();
  dfr1154_acebott_link_pump(400);
  AcebottLinkStatus after;
  dfr1154_acebott_link_get_status(&after);
  bool ack = after.ack_count > before.ack_count;
  String j = F("{\"ok\":true,\"acebott_ack\":");
  j += ack ? F("true") : F("false");
  j += F(",\"acebott_last_rx\":\"");
  j += after.last_rx;
  j += F("\"}");
  g_srv->send(200, "application/json", j);
}

static void handleApiAcebottPing() {
  AcebottLinkStatus before;
  dfr1154_acebott_link_get_status(&before);
  dfr1154_acebott_link_send_ping();
  dfr1154_acebott_link_pump(400);
  AcebottLinkStatus after;
  dfr1154_acebott_link_get_status(&after);
  bool ack = after.ack_count > before.ack_count;
  String j = F("{\"ok\":true,\"acebott_ack\":");
  j += ack ? F("true") : F("false");
  j += F(",\"acebott_ok\":");
  j += after.heard_recent ? F("true") : F("false");
  j += F(",\"acebott_last_rx\":\"");
  j += after.last_rx;
  j += F("\",\"acebott_last_tx\":\"");
  j += after.last_tx;
  j += F("\"}");
  g_srv->send(200, "application/json", j);
}

static void handleApiNvsReset() {
  if (!g_st) {
    g_srv->send(500, "application/json", "{\"ok\":false}");
    return;
  }
  dfr1154_dashboard_nvs_factory_reset(g_st);
  g_srv->send(200, "application/json", "{\"ok\":true}");
}

static void handleDashPage() {
  static const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Squirrel deterrent</title>
<style>
body{font-family:system-ui,sans-serif;margin:12px;max-width:960px;}
h1{font-size:1.25rem;} h2{font-size:1rem;margin:0 0 8px;}
.badge{font-weight:700;padding:4px 10px;border-radius:6px;display:inline-block;}
.arm-yes{background:#c62828;color:#fff;} .arm-no{background:#2e7d32;color:#fff;}
section{border:1px solid #ccc;border-radius:8px;padding:12px;margin:12px 0;}
pre#stat{background:#f5f5f5;padding:8px;overflow:auto;font-size:11px;max-height:160px;}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:6px 0;}
label{display:inline-block;min-width:160px;}
input[type=number]{width:90px;}
button{padding:6px 12px;cursor:pointer;}
.tag-sq{background:#2e7d32;color:#fff;} .tag-no{background:#1565c0;color:#fff;}
.tag-un{background:#6a1b9a;color:#fff;}
#pan_slider{width:min(100%,320px);vertical-align:middle;}
#review_img,#cloud_img{max-width:100%;max-height:320px;border:1px solid #888;background:#222;}
.counts{font-size:14px;color:#444;}
.keystat{font-size:12px;color:#666;}
#cloud_verdict{background:#f5f5f5;padding:8px;font-size:13px;white-space:pre-wrap;min-height:1.4em;}
</style>
</head>
<body>
<h1>Squirrel deterrent + dataset</h1>
<p>Arm spray: <span id="armbadge" class="badge arm-no">DISARMED</span>
<button type="button" onclick="arm(1)">Arm</button>
<button type="button" onclick="arm(0)">Disarm</button>
&nbsp; ACEBOTT UART: <span id="acebadge" class="badge arm-no">NO REPLY</span>
<button type="button" onclick="pingAcebott()">Ping ACEBOTT</button>
<span id="acehint" class="keystat"></span></p>

<section>
<h2>Live view — pan (yaw) &amp; spray</h2>
<p><img id="cam" style="max-width:100%;border:1px solid #888;min-height:120px;background:#222;" alt="camera">
<span id="cam_err" style="color:#c62828;font-size:13px;"></span></p>
<div class="row">
<button type="button" id="btnstream">Start stream</button>
<button type="button" onclick="captureManual()">Take photo</button>
<button type="button" onclick="evaluateNow()">Take photo + ask AI</button>
<button type="button" onclick="testSpray()" style="background:#c62828;color:#fff;">Test spray</button>
</div>
<p id="live_msg" style="font-size:13px;min-height:1em;"></p>
<div class="row">
<label>Pan yaw <span id="pan_val">90</span>&deg;</label>
<input type="range" id="pan_slider" min="0" max="180" value="90">
<button type="button" onclick="sendPan()">Set pan</button>
</div>
</section>

<section>
<h2>AI squirrel review</h2>
<p class="keystat">A photo is captured on motion, on the capture timer, or via the button below, then sent to the
selected cloud AI for a squirrel / not-squirrel verdict. Confirming "Squirrel" centers the pan servo on the AI's
best-guess position and sprays (if armed); every decision archives the photo (+ AI notes) into the matching
dataset folder for later edge-model training.</p>
<div class="row">
<label>Provider</label>
<select id="cloud_provider"><option value="openai">OpenAI (GPT-4o)</option><option value="anthropic">Anthropic (Claude)</option></select>
<label><input type="checkbox" id="cloud_eval_enabled"> Enable cloud AI calls</label>
<label><input type="checkbox" id="cloud_auto_confirm"> Auto-confirm above threshold</label>
</div>
<div class="row">
<label>Confidence threshold</label>
<input type="number" id="cloud_confidence_threshold" step="0.01" min="0" max="1">
</div>
<div class="row">
<label>OpenAI API key</label>
<input type="password" id="cloud_openai_key" placeholder="sk-... (blank = keep saved key)">
<span id="cloud_openai_key_status" class="keystat"></span>
</div>
<div class="row">
<label>OpenAI model</label>
<input type="text" id="cloud_openai_model">
</div>
<div class="row">
<label>Anthropic API key</label>
<input type="password" id="cloud_anthropic_key" placeholder="sk-ant-... (blank = keep saved key)">
<span id="cloud_anthropic_key_status" class="keystat"></span>
</div>
<div class="row">
<label>Anthropic model</label>
<input type="text" id="cloud_anthropic_model">
</div>
<div class="row">
<button type="button" onclick="saveCloudCfg()">Save AI settings</button>
<button type="button" onclick="evaluateNow()">Capture &amp; evaluate now</button>
</div>
<div class="row" id="cloud_pending_wrap" style="display:none;">
<img id="cloud_img" alt="pending review">
</div>
<pre id="cloud_verdict">no photo evaluated yet</pre>
<div class="row">
<button type="button" class="tag-sq" onclick="cloudConfirm('squirrel')">Confirm squirrel — center &amp; spray</button>
<button type="button" class="tag-no" onclick="cloudConfirm('not_squirrel')">Not a squirrel</button>
<button type="button" class="tag-un" onclick="cloudConfirm('uncertain')">Uncertain</button>
</div>
</section>

<section>
<h2>Label dataset (review on camera)</h2>
<p class="counts" id="counts">loading counts…</p>
<div class="row">
<label>Folder</label>
<select id="ds_folder" onchange="loadFileList()">
<option value="inbox">inbox (untagged)</option>
<option value="squirrel">squirrel</option>
<option value="not_squirrel">not squirrel</option>
<option value="uncertain">uncertain</option>
</select>
<label>File</label>
<select id="ds_file" onchange="showReview()"></select>
</div>
<p><img id="review_img" alt="review"></p>
<div class="row">
<button type="button" class="tag-sq" onclick="tagCurrent('squirrel')">Squirrel</button>
<button type="button" class="tag-no" onclick="tagCurrent('not_squirrel')">Not squirrel</button>
<button type="button" class="tag-un" onclick="tagCurrent('uncertain')">Uncertain</button>
<button type="button" onclick="loadFileList()">Refresh list</button>
<button type="button" onclick="clearFolder()">Delete all in this folder</button>
</div>
</section>

<section>
<h2>Status</h2>
<pre id="stat">loading…</pre>
</section>

<section>
<h2>Calibration (NVS)</h2>
<div class="row"><label>Pan center trim</label><input type="number" id="pan_center" min="0" max="180"></div>
<div class="row"><label>Aim Kp</label><input type="number" id="aim_kp" step="0.1"></div>
<div class="row"><label>Center deadband</label><input type="number" id="center_deadband" step="0.01"></div>
<div class="row"><label>Confidence threshold</label><input type="number" id="confidence_threshold" step="0.01" max="1"></div>
<div class="row"><label>Spray cooldown (ms)</label><input type="number" id="spray_cooldown_ms" min="1000"></div>
<div class="row"><label>ML interval (ms)</label><input type="number" id="ml_interval_ms" min="50"></div>
<div class="row"><label>Timer capture (ms, 0=off)</label><input type="number" id="capture_timer_ms" min="0" step="1000"></div>
<div class="row"><label>Motion burst count</label><input type="number" id="motion_burst_count" min="1" max="10"></div>
<div class="row"><label>Track when disarmed</label><input type="checkbox" id="track_when_disarmed"></div>
<button type="button" onclick="saveCal()">Save calibration</button>
</section>

<section>
<h2>Danger zone</h2>
<button type="button" onclick="if(confirm('Reset NVS to defaults?')) fetch('/api/nvs_reset',{method:'POST'}).then(r=>r.json()).then(j=>{alert(JSON.stringify(j)); location.reload();})">Factory reset NVS</button>
</section>

<script>
let streamOn=true, streamIv=null, panDebounce=null, formFilled=false, cameraOk=false;
function setCamErr(msg){ document.getElementById('cam_err').textContent=msg||''; }
function stopStreamTicks(){
  if(streamIv){ clearInterval(streamIv); streamIv=null; }
  document.getElementById('cam').src='';
  document.getElementById('btnstream').textContent='Start stream';
}
function startStreamTicks(){
  if(streamIv) return;
  tickStream();
  streamIv=setInterval(tickStream,300);
  document.getElementById('btnstream').textContent='Stop stream';
}
function tickStream(){
  if(!cameraOk) return;
  document.getElementById('cam').src='/stream?t='+Date.now();
}
document.getElementById('cam').onerror=function(){
  if(!cameraOk){ setCamErr('Camera starting…'); return; }
  setCamErr('No JPEG from /stream — check Serial for camera init / PSRAM.');
};
document.getElementById('cam').onload=function(){ setCamErr(''); };
document.getElementById('btnstream').onclick=function(){
  streamOn=!streamOn;
  if(!streamOn){ stopStreamTicks(); return; }
  if(!cameraOk){ setCamErr('Camera starting…'); return; }
  startStreamTicks();
};
setCamErr('Camera starting…');
const panSlider=document.getElementById('pan_slider');
panSlider.oninput=function(){
  document.getElementById('pan_val').textContent=this.value;
  clearTimeout(panDebounce);
  panDebounce=setTimeout(()=>{
    fetch('/api/pan',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:'pan_deg='+encodeURIComponent(this.value)});
  },120);
};
function sendPan(){
  fetch('/api/pan',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'pan_deg='+encodeURIComponent(panSlider.value)});
}
function testSpray(){
  if(!confirm('Pulse spray servo now?')) return;
  fetch('/api/spray',{method:'POST'}).then(r=>r.json()).then(j=>{
    if(!j.ok){ alert('Fail'); return; }
    alert(j.acebott_ack
      ? ('ACEBOTT ACK: '+(j.acebott_last_rx||'ok')+' — servo command was received')
      : 'Camera sent SPRAY but ACEBOTT did not ACK. Reflash acebott_sensor_node, check TX/RX and common GND.');
  });
}
function pingAcebott(){
  fetch('/api/acebott/ping',{method:'POST'}).then(r=>r.json()).then(j=>{
    alert(j.acebott_ack
      ? ('ACEBOTT replied: '+(j.acebott_last_rx||'PONG'))
      : 'No PONG. ACEBOTT is not receiving (or not running the updated sketch).');
  });
}
function liveMsg(t){ document.getElementById('live_msg').textContent=t; }
function captureManual(){
  liveMsg('Taking photo\u2026');
  fetch('/api/dataset/capture?src=manual').then(r=>r.json()).then(j=>{
    liveMsg(j.ok?('Saved '+j.path):'Capture failed');
    refreshCounts(); loadFileList();
  }).catch(e=>liveMsg('Capture failed: '+e));
}
function arm(v){
  fetch('/api/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'armed='+(v?1:0)}).then(()=>poll());
}
function saveCal(){
  const p=new URLSearchParams();
  p.set('pan_center', document.getElementById('pan_center').value);
  p.set('aim_kp', document.getElementById('aim_kp').value);
  p.set('center_deadband', document.getElementById('center_deadband').value);
  p.set('confidence_threshold', document.getElementById('confidence_threshold').value);
  p.set('spray_cooldown_ms', document.getElementById('spray_cooldown_ms').value);
  p.set('ml_interval_ms', document.getElementById('ml_interval_ms').value);
  p.set('capture_timer_ms', document.getElementById('capture_timer_ms').value);
  p.set('motion_burst_count', document.getElementById('motion_burst_count').value);
  p.set('track_when_disarmed', document.getElementById('track_when_disarmed').checked ? '1':'0');
  fetch('/api/cal',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})
    .then(r=>r.json()).then(j=>alert(j.ok?'Saved':'Fail'));
}
function fillForm(j){
  if(typeof j.cal_pan_center==='undefined') return;
  document.getElementById('pan_center').value=j.cal_pan_center;
  document.getElementById('aim_kp').value=j.cal_aim_kp;
  document.getElementById('center_deadband').value=j.cal_deadband;
  document.getElementById('confidence_threshold').value=j.cal_confidence;
  document.getElementById('spray_cooldown_ms').value=j.cal_cooldown_ms;
  document.getElementById('ml_interval_ms').value=j.cal_ml_interval_ms;
  document.getElementById('capture_timer_ms').value=j.cal_capture_timer_ms;
  document.getElementById('motion_burst_count').value=j.cal_motion_burst_count;
  document.getElementById('track_when_disarmed').checked=!!j.cal_track_disarmed;
  panSlider.value=j.pan_deg||j.cal_pan_center;
  document.getElementById('pan_val').textContent=panSlider.value;
}
function refreshCounts(){
  fetch('/api/dataset/counts').then(r=>r.json()).then(j=>{
    document.getElementById('counts').textContent=
      'inbox: '+j.inbox+' | squirrel: '+j.squirrel+' | not: '+j.not_squirrel+' | uncertain: '+j.uncertain+
      (j.purging?' | deleting old photos in background ('+j.purged+' done)':'');
  });
}
function clearFolder(){
  const folder=document.getElementById('ds_folder').value;
  if(!confirm('Permanently delete ALL photos in "'+folder+'"? This cannot be undone.')) return;
  const p=new URLSearchParams(); p.set('folder', folder);
  fetch('/api/dataset/clear',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})
    .then(r=>r.json()).then(j=>{
      if(j.ok){ alert('Cleared '+j.cleared+' photos from '+folder+'. Old files are deleted in the background.'); refreshCounts(); loadFileList(); }
      else alert('Delete failed: '+(j.error||'unknown'));
    }).catch(e=>alert('Delete failed: '+e));
}
function loadFileList(){
  const folder=document.getElementById('ds_folder').value;
  fetch('/api/dataset/list?folder='+encodeURIComponent(folder)).then(r=>r.json()).then(j=>{
    const sel=document.getElementById('ds_file');
    sel.innerHTML='';
    (j.files||[]).forEach(f=>{
      const o=document.createElement('option');
      o.value=f.name; o.textContent=f.name;
      sel.appendChild(o);
    });
    if(j.truncated){
      const o=document.createElement('option');
      o.disabled=true; o.textContent='… first '+j.files.length+' of '+j.total+' shown — tag these to see more';
      sel.appendChild(o);
    }
    showReview();
  });
}
function showReview(){
  const folder=document.getElementById('ds_folder').value;
  const name=document.getElementById('ds_file').value;
  const img=document.getElementById('review_img');
  if(!name){ img.removeAttribute('src'); return; }
  img.src='/api/dataset/image?folder='+encodeURIComponent(folder)+'&name='+encodeURIComponent(name)+'&t='+Date.now();
}
function tagCurrent(label){
  const folder=document.getElementById('ds_folder').value;
  const name=document.getElementById('ds_file').value;
  if(!name){ alert('No file selected'); return; }
  const p=new URLSearchParams();
  p.set('folder', folder); p.set('name', name); p.set('label', label);
  fetch('/api/dataset/tag',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})
    .then(r=>r.json()).then(j=>{
      if(j.ok){ refreshCounts(); loadFileList(); }
      else alert('Tag failed');
    });
}
let cloudFormFilled=false;
function fillCloudForm(j){
  document.getElementById('cloud_provider').value=j.provider;
  document.getElementById('cloud_eval_enabled').checked=!!j.eval_enabled;
  document.getElementById('cloud_auto_confirm').checked=!!j.auto_confirm;
  document.getElementById('cloud_confidence_threshold').value=j.confidence_threshold;
  document.getElementById('cloud_openai_model').value=j.openai_model;
  document.getElementById('cloud_anthropic_model').value=j.anthropic_model;
  document.getElementById('cloud_openai_key_status').textContent=j.openai_key_set?'key saved':'not set';
  document.getElementById('cloud_anthropic_key_status').textContent=j.anthropic_key_set?'key saved':'not set';
}
function saveCloudCfg(){
  const p=new URLSearchParams();
  p.set('provider', document.getElementById('cloud_provider').value);
  p.set('eval_enabled', document.getElementById('cloud_eval_enabled').checked?'1':'0');
  p.set('auto_confirm', document.getElementById('cloud_auto_confirm').checked?'1':'0');
  p.set('confidence_threshold', document.getElementById('cloud_confidence_threshold').value);
  p.set('openai_model', document.getElementById('cloud_openai_model').value);
  p.set('anthropic_model', document.getElementById('cloud_anthropic_model').value);
  const oaKey=document.getElementById('cloud_openai_key').value;
  if(oaKey) p.set('openai_key', oaKey);
  const anKey=document.getElementById('cloud_anthropic_key').value;
  if(anKey) p.set('anthropic_key', anKey);
  fetch('/api/cloud/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})
    .then(r=>r.json()).then(j=>{
      document.getElementById('cloud_openai_key').value='';
      document.getElementById('cloud_anthropic_key').value='';
      cloudFormFilled=false;
      renderCloudStatus(j);
      alert('AI settings saved');
    });
}
function evaluateNow(){
  const v=document.getElementById('cloud_verdict');
  v.textContent='Capturing + evaluating\u2026 (a few seconds)';
  liveMsg('Taking photo + asking AI\u2026');
  fetch('/api/cloud/evaluate',{method:'POST'}).then(r=>r.json()).then(j=>{
    renderCloudStatus(j);
    liveMsg('AI: '+v.textContent+' (confirm in AI squirrel review below)');
  }).catch(e=>{ v.textContent='Evaluate failed: '+e; liveMsg(v.textContent); });
}
function cloudConfirm(decision){
  const p=new URLSearchParams();
  p.set('decision', decision);
  fetch('/api/cloud/confirm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p})
    .then(r=>r.json()).then(j=>{
      if(!j.ok){ alert('Confirm failed: '+(j.error||'?')); return; }
      let msg='Archived as '+decision;
      if(j.centered) msg+=' \u2014 centered pan';
      if(j.sprayed) msg+=' \u2014 sprayed!';
      alert(msg);
      pollCloudStatus(); refreshCounts(); loadFileList();
    });
}
function renderCloudStatus(j){
  if(!cloudFormFilled){ fillCloudForm(j); cloudFormFilled=true; }
  const wrap=document.getElementById('cloud_pending_wrap');
  const img=document.getElementById('cloud_img');
  if(j.path){
    wrap.style.display='block';
    const parts=j.path.split('/'); // "", "dataset", folder, name
    img.src='/api/dataset/image?folder='+encodeURIComponent(parts[2])+'&name='+encodeURIComponent(parts[3])+'&t='+Date.now();
  } else {
    wrap.style.display='none';
  }
  let verdict;
  if(!j.path){
    verdict='No photo evaluated yet.';
  } else if(j.pending){
    if(!j.ai_ok){
      verdict='AI call failed: '+j.ai_error;
    } else {
      verdict=(j.ai_is_squirrel?'SQUIRREL':'no squirrel')+' \u2014 confidence '+Math.round(j.ai_confidence*100)+'%\n'+
              j.ai_reason+'\nsource: '+j.source+(j.armed?'':'\n(disarmed \u2014 confirming will center but not spray)');
    }
  } else {
    verdict='Archived as '+j.decision+' ('+j.confirmed_by+')';
  }
  document.getElementById('cloud_verdict').textContent=verdict;
}
function pollCloudStatus(){
  fetch('/api/cloud/status').then(r=>r.json()).then(j=>renderCloudStatus(j)).catch(()=>{});
}
setInterval(pollCloudStatus,2000);
function poll(){
  fetch('/api/status').then(r=>r.json()).then(j=>{
    document.getElementById('stat').textContent=JSON.stringify(j,null,2);
    const b=document.getElementById('armbadge');
    if(j.armed){ b.textContent='ARMED'; b.className='badge arm-yes'; }
    else { b.textContent='DISARMED'; b.className='badge arm-no'; }
    const a=document.getElementById('acebadge');
    const h=document.getElementById('acehint');
    if(j.acebott_ok){ a.textContent='LINK UP'; a.className='badge arm-no'; }
    else { a.textContent='NO REPLY'; a.className='badge arm-yes'; }
    h.textContent=' last RX: '+(j.acebott_last_rx||'—')+'  last TX: '+(j.acebott_last_tx||'—');
    cameraOk=!!j.camera_ok;
    if(cameraOk && streamOn) startStreamTicks();
    if(!cameraOk){
      stopStreamTicks();
      if(streamOn) setCamErr('Camera starting…');
    }
    if(!formFilled){ fillForm(j); formFilled=true; }
  }).catch(e=>{ document.getElementById('stat').textContent='status error '+e; });
}
setInterval(poll,400);
poll(); refreshCounts(); loadFileList(); pollCloudStatus();
</script>
</body>
</html>
)rawliteral";
  g_srv->send_P(200, "text/html", page, sizeof(page) - 1);
}

static void handleOk() {
  g_srv->send(200, "text/plain", "ok");
}

static void routePostCal() {
  handleApiCal();
}
static void routePostArm() {
  handleApiArm();
}
static void routePostPan() {
  handleApiPan();
}
static void routePostSpray() {
  handleApiSpray();
}
static void routePostNvsReset() {
  handleApiNvsReset();
}

void dfr1154_dashboard_register(WebServer& server, DashboardState* state,
                                DashboardPanCallback pan_cb,
                                DashboardSprayCallback spray_cb) {
  g_srv = &server;
  g_st = state;
  g_pan_cb = pan_cb;
  g_spray_cb = spray_cb;

  dfr1154_http_debug_register_stream_capture_only(server);
  dfr1154_dataset_register(server);
  dfr1154_cloud_review_init(state, pan_cb, spray_cb);
  dfr1154_cloud_review_register(server);

  server.on("/", HTTP_GET, handleRootRedirect);
  server.on("/ok", HTTP_GET, handleOk);
  server.on("/dash", HTTP_GET, handleDashPage);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/cal", HTTP_POST, routePostCal);
  server.on("/api/arm", HTTP_POST, routePostArm);
  server.on("/api/pan", HTTP_POST, routePostPan);
  server.on("/api/spray", HTTP_POST, routePostSpray);
  server.on("/api/acebott/ping", HTTP_POST, handleApiAcebottPing);
  server.on("/api/nvs_reset", HTTP_POST, routePostNvsReset);
}
