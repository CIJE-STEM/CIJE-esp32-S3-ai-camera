#include "dfr1154_http_debug.h"
#include "dfr1154_camera.h"
#include "esp_camera.h"
#include "SD_MMC.h"
#include "FS.h"

#include <string.h>

static WebServer* g_server;
static uint32_t g_capture_seq = 0;
static uint8_t* s_jpg_copy;
static size_t s_jpg_copy_cap;

static uint8_t* jpg_scratch(size_t need) {
  if (s_jpg_copy && s_jpg_copy_cap >= need) {
    return s_jpg_copy;
  }
  if (s_jpg_copy) {
    free(s_jpg_copy);
    s_jpg_copy = nullptr;
    s_jpg_copy_cap = 0;
  }
  size_t cap = need + 8192;
  s_jpg_copy = (uint8_t*)ps_malloc(cap);
  if (!s_jpg_copy) {
    s_jpg_copy = (uint8_t*)malloc(cap);
  }
  if (s_jpg_copy) {
    s_jpg_copy_cap = cap;
  }
  return s_jpg_copy;
}

void dfr1154_http_serve_stream(WebServer& server) {
  if (!dfr1154_camera_ok()) {
    server.send(503, "text/plain", "camera starting");
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || !fb->buf || fb->len < 100) {
    if (fb) {
      esp_camera_fb_return(fb);
    }
    static uint32_t last_log_ms;
    if (millis() - last_log_ms > 2000) {
      last_log_ms = millis();
      Serial.println("[stream] fb_get failed (camera busy or FB-OVF)");
    }
    server.send(503, "text/plain", "camera busy");
    return;
  }

  // Copy out and return the framebuffer BEFORE the HTTP write. Holding the
  // only FB during a WiFi send is what triggers cam_hal: FB-OVF.
  size_t len = fb->len;
  uint8_t* copy = jpg_scratch(len);
  if (copy) {
    memcpy(copy, fb->buf, len);
    esp_camera_fb_return(fb);
    fb = nullptr;
  }

  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  // ESP32 WebServer 3.x has no send(code, type, buf, len). send_P does.
  if (copy) {
    server.send_P(200, "image/jpeg", (const char*)copy, len);
  } else {
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  }
}

void dfr1154_http_serve_capture(WebServer& server) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "application/json", "{\"success\":false}");
    return;
  }
  g_capture_seq++;
  String filename = "CAP_" + String(g_capture_seq) + ".jpg";
  String path = "/images/" + filename;
  File file = SD_MMC.open(path, FILE_WRITE);
  bool ok = false;
  if (file && fb->format == PIXFORMAT_JPEG) {
    ok = file.write(fb->buf, fb->len) == fb->len;
    file.close();
  } else if (file) {
    file.close();
  }
  esp_camera_fb_return(fb);
  if (ok) {
    server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + filename + "\"}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
}

static void handleRoot() {
  static const char page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Edge ML debug</title></head>
<body style="font-family:sans-serif;margin:12px;">
<h2>DFR1154 — FOV debug</h2>
<p><img id="v" style="max-width:100%;border:1px solid #ccc;" alt="stream"></p>
<button type="button" id="btn">Start stream</button>
<button type="button" onclick="fetch('/capture').then(r=>r.json()).then(j=>alert(JSON.stringify(j)))">Capture</button>
<script>
let on=false,timer=null;
function tick(){document.getElementById('v').src='/stream?t='+Date.now();}
document.getElementById('btn').onclick=function(){
  on=!on;
  if(!on){clearInterval(timer);document.getElementById('v').src='';this.textContent='Start stream';return;}
  tick();timer=setInterval(tick,150);this.textContent='Stop stream';
};
</script>
<p style="color:#666;font-size:14px;">Stream polls <code>/stream</code> every 150 ms (matches main camera firmware pattern).</p>
</body></html>
)rawliteral";
  g_server->send_P(200, "text/html", page);
}

static void handleStream() {
  dfr1154_http_serve_stream(*g_server);
}

static void handleCapture() {
  dfr1154_http_serve_capture(*g_server);
}

void dfr1154_http_debug_register_stream_capture_only(WebServer& server) {
  g_server = &server;
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/capture", HTTP_GET, handleCapture);
}

void dfr1154_http_debug_register(WebServer& server) {
  g_server = &server;
  server.on("/", HTTP_GET, handleRoot);
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/capture", HTTP_GET, handleCapture);
}

void dfr1154_http_debug_reset_capture_counter() {
  g_capture_seq = 0;
}
