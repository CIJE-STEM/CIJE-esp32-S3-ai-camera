#include "cloud_ml_api.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"

static const char kNs[] = "cloudml";

static const char kOpenAiUrl[] = "https://api.openai.com/v1/chat/completions";
static const char kAnthropicUrl[] = "https://api.anthropic.com/v1/messages";
static const char kAnthropicVersion[] = "2023-06-01";
// Server-side fallback: if the model declines (stop_reason "refusal"), the API
// retries on a model chosen for that refusal category.
static const char kAnthropicFallbackBeta[] = "server-side-fallback-2026-07-01";
static const char kAnthropicDefaultModel[] = "claude-opus-5-5";

// Kept as ordinary text (real quotes/newlines) and JSON-escaped at call time —
// see json_escape() below — rather than hand-escaped inline, for readability.
static const char kPrompt[] =
  "You are a vision classifier for a backyard squirrel-deterrent camera. "
  "Look at the photo and decide whether a squirrel is visible. "
  "Respond with ONLY a compact JSON object (no markdown, no code fences) matching this "
  "schema: {\"squirrel\":true|false,\"confidence\":0.0-1.0,\"cx\":0.0-1.0,\"cy\":0.0-1.0,"
  "\"reason\":\"<=15 words\"}. cx/cy are the normalized position of the squirrel's center "
  "in the image (0,0 top-left, 1,1 bottom-right); use 0.5,0.5 if there is no squirrel or "
  "the position is unclear.";

const char* cloud_ml_provider_name(uint8_t provider) {
  return (provider == CLOUD_ML_PROVIDER_ANTHROPIC) ? "anthropic" : "openai";
}

void cloud_ml_apply_defaults(CloudMlConfig* c) {
  c->provider = CLOUD_ML_PROVIDER_OPENAI;
  c->eval_enabled = false;
  c->auto_confirm = false;
  c->confidence_threshold = 0.70f;
  c->openai_api_key = "";
  c->anthropic_api_key = "";
  c->openai_model = "gpt-4o-mini";
  c->anthropic_model = kAnthropicDefaultModel;
}

void cloud_ml_nvs_load(CloudMlConfig* c) {
  Preferences p;
  if (!p.begin(kNs, true)) {
    cloud_ml_apply_defaults(c);
    return;
  }
  if (!p.getBool("initv1", false)) {
    p.end();
    cloud_ml_apply_defaults(c);
    cloud_ml_nvs_save(c);
    return;
  }
  c->provider = (uint8_t)p.getUInt("provider", CLOUD_ML_PROVIDER_OPENAI);
  c->eval_enabled = p.getBool("eval_on", false);
  c->auto_confirm = p.getBool("auto_conf", false);
  c->confidence_threshold = p.getFloat("conf_th", 0.70f);
  c->openai_api_key = p.getString("oa_key", "");
  c->anthropic_api_key = p.getString("an_key", "");
  c->openai_model = p.getString("oa_model", "gpt-4o-mini");
  c->anthropic_model = p.getString("an_model", kAnthropicDefaultModel);
  p.end();
  // Claude 3.x models are retired (404). Boards that saved one keep failing
  // until the ID changes, so move them to the current default.
  if (c->anthropic_model.startsWith("claude-3")) {
    c->anthropic_model = kAnthropicDefaultModel;
    cloud_ml_nvs_save(c);
  }
}

void cloud_ml_nvs_save(const CloudMlConfig* c) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putBool("initv1", true);
  p.putUInt("provider", c->provider);
  p.putBool("eval_on", c->eval_enabled);
  p.putBool("auto_conf", c->auto_confirm);
  p.putFloat("conf_th", c->confidence_threshold);
  p.putString("oa_key", c->openai_api_key);
  p.putString("an_key", c->anthropic_api_key);
  p.putString("oa_model", c->openai_model);
  p.putString("an_model", c->anthropic_model);
  p.end();
}

static String json_escape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      default: out += c;
    }
  }
  return out;
}

static String strip_code_fence(String s) {
  s.trim();
  if (s.startsWith("```")) {
    int nl = s.indexOf('\n');
    if (nl >= 0) s = s.substring(nl + 1);
    if (s.endsWith("```")) s = s.substring(0, s.length() - 3);
    s.trim();
  }
  return s;
}

static String base64_encode_buffer(const uint8_t* data, size_t len) {
  String result;
  size_t out_len_estimate = 4 * ((len + 2) / 3) + 4;
  result.reserve(out_len_estimate);

  const size_t kChunk = 3000; // multiple of 3 for clean base64 chunk boundaries
  uint8_t* out_buf = (uint8_t*)malloc(kChunk * 2);
  if (!out_buf) return String();

  size_t offset = 0;
  while (offset < len) {
    size_t n = min(kChunk, len - offset);
    size_t written = 0;
    int ret = mbedtls_base64_encode(out_buf, kChunk * 2, &written, data + offset, n);
    if (ret != 0) {
      free(out_buf);
      return String();
    }
    result.concat((const char*)out_buf, written);
    offset += n;
  }
  free(out_buf);
  return result;
}

/** Fills out->is_squirrel/confidence/norm_cx/norm_cy/reason from the model's inner JSON text. */
static bool parse_model_json(const String& text, CloudMlResult* out) {
  String cleaned = strip_code_fence(text);
  DynamicJsonDocument doc(768);
  DeserializationError err = deserializeJson(doc, cleaned);
  if (err) {
    out->error = "model_json_parse: " + String(err.c_str());
    return false;
  }
  out->is_squirrel = doc["squirrel"] | false;
  out->confidence = doc["confidence"] | 0.0f;
  if (out->confidence < 0.0f) out->confidence = 0.0f;
  if (out->confidence > 1.0f) out->confidence = 1.0f;
  out->norm_cx = doc["cx"] | 0.5f;
  out->norm_cy = doc["cy"] | 0.5f;
  out->reason = doc["reason"].as<String>();
  return true;
}

static bool classify_openai(const CloudMlConfig* cfg, const uint8_t* jpeg, size_t jpeg_len,
                             CloudMlResult* out) {
  if (cfg->openai_api_key.length() < 10) {
    out->error = "openai_api_key not set";
    return false;
  }

  String image_b64 = base64_encode_buffer(jpeg, jpeg_len);
  if (image_b64.length() == 0) {
    out->error = "base64_encode_failed";
    return false;
  }

  String body;
  body.reserve(image_b64.length() + 1200);
  body += F("{\"model\":\"");
  body += cfg->openai_model;
  body += F("\",\"max_tokens\":300,\"response_format\":{\"type\":\"json_object\"},\"messages\":[");
  body += F("{\"role\":\"system\",\"content\":\"");
  body += json_escape(String(kPrompt));
  body += F("\"},{\"role\":\"user\",\"content\":[");
  body += F("{\"type\":\"text\",\"text\":\"Classify this photo per the schema.\"},");
  body += F("{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,");
  body += image_b64;
  body += F("\"}}]}]}");
  image_b64 = "";

  WiFiClientSecure client;
  client.setInsecure(); // see firmware/standalone/.../ssl_validation.h for rationale
  client.setTimeout(60);
  HTTPClient https;
  if (!https.begin(client, kOpenAiUrl)) {
    out->error = "https_begin_failed";
    return false;
  }
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + cfg->openai_api_key);
  https.setTimeout(60000);

  int code = https.POST(body);
  body = "";

  bool ok = false;
  if (code == 200) {
    String response = https.getString();
    DynamicJsonDocument doc(response.length() + 1024);
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      out->error = "openai_response_parse: " + String(err.c_str());
    } else {
      String content = doc["choices"][0]["message"]["content"].as<String>();
      ok = parse_model_json(content, out);
    }
  } else if (code > 0) {
    String response = https.getString();
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, response)) {
      out->error = "openai_http_" + String(code) + ": " + doc["error"]["message"].as<String>();
    } else {
      out->error = "openai_http_" + String(code);
    }
  } else {
    out->error = "openai_connect_failed";
  }

  https.end();
  return ok;
}

static bool classify_anthropic(const CloudMlConfig* cfg, const uint8_t* jpeg, size_t jpeg_len,
                                CloudMlResult* out) {
  if (cfg->anthropic_api_key.length() < 10) {
    out->error = "anthropic_api_key not set";
    return false;
  }

  String image_b64 = base64_encode_buffer(jpeg, jpeg_len);
  if (image_b64.length() == 0) {
    out->error = "base64_encode_failed";
    return false;
  }

  String body;
  body.reserve(image_b64.length() + 1200);
  body += F("{\"model\":\"");
  body += cfg->anthropic_model;
  // Thinking is always on for current Claude models and counts toward max_tokens,
  // so leave headroom; low effort is plenty for a yes/no photo check.
  body += F("\",\"max_tokens\":2048,\"output_config\":{\"effort\":\"low\"},");
  body += F("\"fallbacks\":\"default\",\"system\":\"");
  body += json_escape(String(kPrompt));
  body += F("\",\"messages\":[{\"role\":\"user\",\"content\":[");
  body += F("{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/jpeg\",\"data\":\"");
  body += image_b64;
  body += F("\"}},");
  body += F("{\"type\":\"text\",\"text\":\"Classify this photo per the schema.\"}");
  body += F("]}]}");
  image_b64 = "";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);
  HTTPClient https;
  if (!https.begin(client, kAnthropicUrl)) {
    out->error = "https_begin_failed";
    return false;
  }
  https.addHeader("Content-Type", "application/json");
  https.addHeader("x-api-key", cfg->anthropic_api_key);
  https.addHeader("anthropic-version", kAnthropicVersion);
  https.addHeader("anthropic-beta", kAnthropicFallbackBeta);
  https.setTimeout(60000);

  int code = https.POST(body);
  body = "";

  bool ok = false;
  if (code == 200) {
    String response = https.getString();
    DynamicJsonDocument doc(response.length() + 1024);
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      out->error = "anthropic_response_parse: " + String(err.c_str());
    } else {
      // A thinking block comes before the answer, so find the text block.
      String content;
      for (JsonVariant block : doc["content"].as<JsonArray>()) {
        if (block["type"] == "text") {
          content = block["text"].as<String>();
          break;
        }
      }
      String stop = doc["stop_reason"].as<String>();
      if (stop == "refusal") {
        out->error = "anthropic_refusal";
      } else if (content.length() == 0) {
        out->error = "anthropic_no_text (stop_reason " + stop + ")";
      } else {
        ok = parse_model_json(content, out);
      }
    }
  } else if (code > 0) {
    String response = https.getString();
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, response)) {
      out->error = "anthropic_http_" + String(code) + ": " + doc["error"]["message"].as<String>();
    } else {
      out->error = "anthropic_http_" + String(code);
    }
  } else {
    out->error = "anthropic_connect_failed";
  }

  https.end();
  return ok;
}

bool cloud_ml_classify_fb(const CloudMlConfig* cfg, camera_fb_t* fb, CloudMlResult* out) {
  out->ok = false;
  out->is_squirrel = false;
  out->confidence = 0.0f;
  out->norm_cx = 0.5f;
  out->norm_cy = 0.5f;
  out->reason = "";
  out->error = "";
  out->provider_used = cfg ? cfg->provider : CLOUD_ML_PROVIDER_OPENAI;

  if (!cfg || !fb || fb->format != PIXFORMAT_JPEG) {
    out->error = "bad_args";
    return false;
  }
  if (!cfg->eval_enabled) {
    out->error = "eval_disabled";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    out->error = "wifi_down";
    return false;
  }
  if (ESP.getFreeHeap() < 60000) {
    out->error = "low_memory";
    return false;
  }

  bool ok = (cfg->provider == CLOUD_ML_PROVIDER_ANTHROPIC)
              ? classify_anthropic(cfg, fb->buf, fb->len, out)
              : classify_openai(cfg, fb->buf, fb->len, out);
  out->ok = ok;
  return ok;
}
