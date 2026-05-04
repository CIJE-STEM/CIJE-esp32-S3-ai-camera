#include "time.h"
/*
 * ESP32-S3 AI CAM - OpenAI Automation Agent v1.0.0
 * 
 * EPS32S3 DEV Module
 * Tools Tab in IDE
 * USB CDC On Boot- Enabled
 * Flash Size - 16MB(128Mb)
 * Partition Scheme - 16M Flash (3MB APP/9.9MB FATFS)
 * PSRAM - OPI PSRAM
 *
 * AUTOMATION AGENT - PHASE 1: SERIAL API INTERFACE
 * =================================================
 * Adds serial command protocol for external device control (Arduino UNO, etc.)
 * 
 * Serial Protocol:
 * - Baud Rate: 115200
 * - Format: CMD:<command>|PARAM:<value>\n
 * - Response: OK:<data>\n or ERROR:<message>\n
 * 
 * Phase 1 Commands:
 * - PING                    → Test connection
 * - STATUS                  → Get system status
 * - CAPTURE                 → Take photo
 * - RECORD:<duration>       → Record audio (seconds)
 * - LIST_IMAGES             → List image files
 * - LIST_AUDIO              → List audio files
 * - GET_LAST_IMAGE          → Get last captured image filename
 * - GET_LAST_AUDIO          → Get last recorded audio filename
 * - VERSION                 → Get firmware version
 * 
 * Example Usage from Arduino:
 *   Serial.println("CMD:PING");
 *   → "OK:PONG"
 *   
 *   Serial.println("CMD:CAPTURE");
 *   → "OK:IMG_5.jpg"
 *   
 *   Serial.println("CMD:STATUS");
 *   → "OK:READY|HEAP:215000|IMAGES:5|AUDIO:3"
 *
 * All existing web interface features remain fully functional.
 */

#include "esp_camera.h"
#include "SD_MMC.h"
#include "FS.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "ESP_I2S.h"
#include "mbedtls/base64.h"

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// IMPORTANT: Replace with your actual OpenAI API key
const char* OPENAI_API_KEY = "sk-";

const char* OPENAI_WHISPER_URL = "https://api.openai.com/v1/audio/transcriptions";
const char* OPENAI_CHAT_URL = "https://api.openai.com/v1/chat/completions";
const char* OPENAI_TTS_URL = "https://api.openai.com/v1/audio/speech";

const char* OPENAI_ROOT_CA = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n" \
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n" \
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n" \
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n" \
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n" \
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n" \
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n" \
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n" \
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n" \
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n" \
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n" \
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n" \
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n" \
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n" \
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n" \
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n" \
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n" \
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n" \
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n" \
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n" \
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n" \
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n" \
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n" \
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n" \
"-----END CERTIFICATE-----\n";

WebServer server(80);

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 5
#define SIOD_GPIO_NUM 8
#define SIOC_GPIO_NUM 9
#define Y9_GPIO_NUM 4
#define Y8_GPIO_NUM 6
#define Y7_GPIO_NUM 7
#define Y6_GPIO_NUM 14
#define Y5_GPIO_NUM 17
#define Y4_GPIO_NUM 21
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 16
#define VSYNC_GPIO_NUM 1
#define HREF_GPIO_NUM 2
#define PCLK_GPIO_NUM 15
#define SD_MMC_CMD 11
#define SD_MMC_CLK 12
#define SD_MMC_D0 13
#define PDM_DATA_PIN GPIO_NUM_39
#define PDM_CLOCK_PIN GPIO_NUM_38
#define I2S_BCLK 45
#define I2S_LRC 46
#define I2S_DOUT 42
#define I2S_SD_PIN 41
#define SAMPLE_RATE 16000
#define AMPLIFICATION 15

int imageCount = 0;
int audioCount = 0;
int ttsCount = 0;
bool streamingEnabled = true;
volatile bool recordingInProgress = false;
volatile bool ttsInProgress = false;

// Progress tracking for OpenAI analysis
volatile bool analysisInProgress = false;
String analysisProgressStage = "";
String analysisProgressDetail = "";
int analysisProgressPercent = 0;

// Store completed analysis results
bool analysisResultReady = false;
String analysisResultText = "";
String analysisResultTTSFile = "";
bool analysisResultSuccess = false;

// Current analysis request
String currentImagePath = "";
String currentAudioPath = "";
String currentAnalysisID = "";

// Serial command interface variables
String serialCommandBuffer = "";
bool serialCommandReady = false;
String lastCapturedImage = "";
String lastRecordedAudio = "";
const char* FIRMWARE_VERSION = "1.0.0";

I2SClass i2s_mic;
I2SClass i2s_spk;

String base64EncodeFileOptimized(String filePath) {
  Serial.println("=== Base64 Encoding ===");
  File file = SD_MMC.open(filePath, FILE_READ);
  if (!file) {
    Serial.println("ERROR: Failed to open file");
    return "";
  }
  
  size_t fileSize = file.size();
  Serial.printf("File size: %d bytes\n", fileSize);
  Serial.printf("Free heap before encoding: %d bytes\n", ESP.getFreeHeap());
  
  // Calculate output size (base64 is ~4/3 the input size)
  size_t base64Size = ((fileSize + 2) / 3) * 4;
  
  String result = "";
  result.reserve(base64Size + 100);
  
  // Process in chunks to avoid memory issues
  const size_t chunkSize = 3000; // Must be multiple of 3 for clean base64 encoding
  uint8_t* inputBuffer = (uint8_t*)malloc(chunkSize);
  uint8_t* outputBuffer = (uint8_t*)malloc(chunkSize * 2); // Base64 needs ~33% more space
  
  if (!inputBuffer || !outputBuffer) {
    Serial.println("ERROR: Buffer allocation failed");
    if (inputBuffer) free(inputBuffer);
    if (outputBuffer) free(outputBuffer);
    file.close();
    return "";
  }
  
  size_t totalRead = 0;
  while (file.available()) {
    size_t bytesRead = file.read(inputBuffer, chunkSize);
    if (bytesRead > 0) {
      size_t outLen = 0;
      int ret = mbedtls_base64_encode(outputBuffer, chunkSize * 2, &outLen, inputBuffer, bytesRead);
      
      if (ret == 0) {
        // Append encoded chunk to result
        for (size_t i = 0; i < outLen; i++) {
          result += (char)outputBuffer[i];
        }
        totalRead += bytesRead;
        
        if (totalRead % 30000 == 0) {
          Serial.printf("Encoded: %d bytes\n", totalRead);
          yield();
        }
      } else {
        Serial.printf("ERROR: Base64 encode failed at offset %d, ret=%d\n", totalRead, ret);
        break;
      }
    }
  }
  
  file.close();
  free(inputBuffer);
  free(outputBuffer);
  
  Serial.printf("Encoding complete: %d input bytes -> %d base64 chars\n", totalRead, result.length());
  Serial.printf("Free heap after encoding: %d bytes\n", ESP.getFreeHeap());
  
  return result;
}

String getHttpErrorString(int code) {
  switch(code) {
    case HTTPC_ERROR_CONNECTION_REFUSED: return "Connection refused";
    case HTTPC_ERROR_SEND_HEADER_FAILED: return "Send header failed";
    case HTTPC_ERROR_SEND_PAYLOAD_FAILED: return "Send payload failed";
    case HTTPC_ERROR_NOT_CONNECTED: return "Not connected";
    case HTTPC_ERROR_CONNECTION_LOST: return "Connection lost";
    case HTTPC_ERROR_NO_STREAM: return "No stream";
    case HTTPC_ERROR_NO_HTTP_SERVER: return "No HTTP server";
    case HTTPC_ERROR_TOO_LESS_RAM: return "Not enough RAM";
    case HTTPC_ERROR_ENCODING: return "Encoding error";
    case HTTPC_ERROR_STREAM_WRITE: return "Stream write error";
    case HTTPC_ERROR_READ_TIMEOUT: return "Read timeout";
    default: return "Unknown error (" + String(code) + ")";
  }
}

String transcribeAudioWithWhisper(String audioFilePath) {
  Serial.println("\n=== OpenAI Whisper API ===");
  
  File audioFile = SD_MMC.open(audioFilePath, FILE_READ);
  if (!audioFile) {
    Serial.println("ERROR: Failed to open audio file");
    return "Error: File not found";
  }
  
  size_t fileSize = audioFile.size();
  Serial.printf("Audio file size: %d bytes\n", fileSize);
  
  uint8_t* audioData = (uint8_t*)ps_malloc(fileSize);
  if (!audioData) audioData = (uint8_t*)malloc(fileSize);
  if (!audioData) { 
    audioFile.close(); 
    Serial.println("ERROR: Memory allocation failed");
    return "Error: Memory allocation failed"; 
  }
  Serial.println("Memory allocated for audio data");
  
  audioFile.read(audioData, fileSize);
  audioFile.close();
  Serial.println("Audio file loaded into memory");
  
  WiFiClientSecure client;
  Serial.println("Setting up secure connection (INSECURE MODE)...");
  client.setInsecure(); // Skip certificate validation
  client.setTimeout(30);
  
  HTTPClient https;
  if (!https.begin(client, OPENAI_WHISPER_URL)) {
    free(audioData);
    Serial.println("ERROR: HTTPS begin failed");
    return "Error: Connection setup failed";
  }
  Serial.println("HTTPS connection initialized");
  
  https.addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));
  https.setTimeout(30000);
  
  String boundary = "----ESP32" + String(millis());
  https.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  
  String bodyStart = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String bodyEnd = "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n--" + boundary + "--\r\n";
  
  size_t totalSize = bodyStart.length() + fileSize + bodyEnd.length();
  Serial.printf("Request body size: %d bytes\n", totalSize);
  
  uint8_t* requestBody = (uint8_t*)ps_malloc(totalSize);
  if (!requestBody) requestBody = (uint8_t*)malloc(totalSize);
  if (!requestBody) { 
    free(audioData); 
    https.end();
    Serial.println("ERROR: Request body allocation failed");
    return "Error: Memory allocation failed"; 
  }
  Serial.println("Request body allocated");
  
  size_t offset = 0;
  memcpy(requestBody + offset, bodyStart.c_str(), bodyStart.length()); 
  offset += bodyStart.length();
  memcpy(requestBody + offset, audioData, fileSize); 
  offset += fileSize;
  memcpy(requestBody + offset, bodyEnd.c_str(), bodyEnd.length());
  
  Serial.println("Sending POST request to Whisper API...");
  Serial.printf("Free heap before POST: %d bytes\n", ESP.getFreeHeap());
  
  int httpCode = https.POST(requestBody, totalSize);
  
  Serial.printf("HTTP Response Code: %d\n", httpCode);
  if (httpCode < 0) {
    Serial.println("ERROR: " + getHttpErrorString(httpCode));
  }
  
  String transcription = "";
  
  if (httpCode == 200) {
    String response = https.getString();
    Serial.println("Response received, length: " + String(response.length()));
    Serial.println("Response preview: " + response.substring(0, min(200, (int)response.length())));
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      transcription = doc["text"].as<String>();
      Serial.println("SUCCESS: " + transcription);
    } else {
      Serial.println("ERROR: JSON parsing failed - " + String(error.c_str()));
      transcription = "Error: JSON parse failed";
    }
  } else if (httpCode > 0) {
    String errorResponse = https.getString();
    Serial.println("Server error response: " + errorResponse);
    transcription = "Error: Whisper API returned " + String(httpCode);
  } else {
    transcription = "Error: " + getHttpErrorString(httpCode);
  }
  
  free(audioData);
  free(requestBody);
  https.end();
  
  Serial.printf("Free heap after: %d bytes\n", ESP.getFreeHeap());
  
  return transcription;
}

String analyzeImageWithGPT4Vision(String imageFilePath, String audioTranscription) {
  Serial.println("\n=== GPT-4 Vision ===");
  Serial.printf("Free heap before base64: %d bytes\n", ESP.getFreeHeap());
  
  String imageBase64 = base64EncodeFileOptimized(imageFilePath);
  if (imageBase64.length() == 0) {
    Serial.println("ERROR: Base64 encoding failed");
    return "Error: Base64 encoding failed";
  }
  
  Serial.printf("Base64 length: %d chars\n", imageBase64.length());
  Serial.printf("Free heap after base64: %d bytes\n", ESP.getFreeHeap());
  
  // Check if we have enough memory
  if (ESP.getFreeHeap() < 50000) {
    Serial.println("ERROR: Insufficient memory for Vision API");
    imageBase64 = ""; // Free memory
    return "Error: Insufficient memory";
  }
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  
  if (!https.begin(client, OPENAI_CHAT_URL)) {
    Serial.println("ERROR: HTTPS begin failed");
    imageBase64 = "";
    return "Error: Connection setup failed";
  }
  
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));
  https.setTimeout(60000);
  
  size_t docSize = imageBase64.length() + 4096;
  Serial.printf("Allocating JSON doc: %d bytes\n", docSize);
  
  DynamicJsonDocument* doc = new DynamicJsonDocument(docSize);
  if (!doc) {
    Serial.println("ERROR: JSON allocation failed");
    imageBase64 = "";
    https.end();
    return "Error: JSON allocation failed";
  }
  
  (*doc)["model"] = "gpt-4o";
  (*doc)["max_tokens"] = 500;
  JsonArray messages = doc->createNestedArray("messages");
  JsonObject userMsg = messages.createNestedObject();
  userMsg["role"] = "user";
  JsonArray content = userMsg.createNestedArray("content");
  JsonObject textPart = content.createNestedObject();
  textPart["type"] = "text";
  String prompt = "Analyze this image concisely. ";
  if (audioTranscription.length() > 0 && !audioTranscription.startsWith("Error")) {
    prompt += "Audio: \"" + audioTranscription + "\". Consider both.";
  }
  textPart["text"] = prompt;
  JsonObject imagePart = content.createNestedObject();
  imagePart["type"] = "image_url";
  JsonObject imageUrl = imagePart.createNestedObject("image_url");
  imageUrl["url"] = "data:image/jpeg;base64," + imageBase64;
  
  String requestBody;
  serializeJson(*doc, requestBody);
  delete doc;
  
  // Free base64 string as soon as possible
  imageBase64 = "";
  
  Serial.printf("Request size: %d bytes\n", requestBody.length());
  Serial.printf("Free heap before POST: %d bytes\n", ESP.getFreeHeap());
  Serial.println("Sending POST to GPT-4 Vision...");
  
  int httpCode = https.POST(requestBody);
  requestBody = ""; // Free immediately
  
  Serial.printf("HTTP Response Code: %d\n", httpCode);
  
  String analysis = "";
  if (httpCode == 200) {
    String response = https.getString();
    Serial.printf("Response length: %d bytes\n", response.length());
    
    DynamicJsonDocument responseDoc(4096);
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (error == DeserializationError::Ok) {
      analysis = responseDoc["choices"][0]["message"]["content"].as<String>();
      Serial.println("SUCCESS: " + analysis);
    } else {
      Serial.println("ERROR: JSON parse failed - " + String(error.c_str()));
      analysis = "Error: JSON parse failed";
    }
  } else if (httpCode > 0) {
    String errorResponse = https.getString();
    Serial.println("Server error: " + errorResponse);
    analysis = "Error: GPT-4 returned " + String(httpCode);
  } else {
    Serial.println("ERROR: Connection failed");
    analysis = "Error: Connection failed";
  }
  
  https.end();
  Serial.printf("Free heap after Vision: %d bytes\n", ESP.getFreeHeap());
  
  return analysis;
}

String generateTTSAudio(String text, String analysisID) {
  Serial.println("\n=== OpenAI TTS (Non-blocking) ===");
  ttsInProgress = true;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);
  HTTPClient https;
  https.begin(client, OPENAI_TTS_URL);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));
  https.setTimeout(60000);
  https.setReuse(false);
  DynamicJsonDocument doc(4096);
  doc["model"] = "tts-1";
  doc["input"] = text;
  doc["voice"] = "alloy";
  doc["response_format"] = "mp3";
  String requestBody;
  serializeJson(doc, requestBody);
  int httpCode = https.POST(requestBody);
  String ttsFilename = "";
  if (httpCode == 200) {
    WiFiClient* stream = https.getStreamPtr();
    ttsCount++;
    ttsFilename = "TTS_" + analysisID + ".mp3";
    File ttsFile = SD_MMC.open("/tts/" + ttsFilename, FILE_WRITE);
    if (!ttsFile) { https.end(); ttsInProgress = false; return ""; }
    uint8_t buffer[512];
    size_t totalBytes = 0;
    unsigned long lastYield = millis();
    unsigned long startTime = millis();
    while (https.connected() && (stream->available() > 0 || (millis() - startTime < 30000))) {
      size_t size = stream->available();
      if (size > 0) {
        int len = stream->readBytes(buffer, min(size, sizeof(buffer)));
        if (len > 0) {
          ttsFile.write(buffer, len);
          totalBytes += len;
          if (millis() - lastYield > 100) { yield(); lastYield = millis(); }
          if (totalBytes % 10240 == 0) Serial.printf("TTS: %dKB\n", totalBytes / 1024);
        }
      } else { yield(); delay(10); }
      if (millis() - startTime > 60000) break;
    }
    ttsFile.close();
    Serial.printf("TTS saved: %d bytes\n", totalBytes);
  }
  https.end();
  ttsInProgress = false;
  return ttsFilename;
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>ESP32-S3 OpenAI + TTS</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

body {
  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  background: #f5f5f5;
  padding: 20px;
}

.main-grid {
  display: grid;
  grid-template-columns: 1.5fr 1fr;
  grid-template-rows: auto auto auto auto auto;
  gap: 20px;
  max-width: 1400px;
  margin: 0 auto;
}

/* Left column - Video section spans rows 1-4 */
.video-section {
  grid-column: 1;
  grid-row: 1 / 5;
  background: white;
  border-radius: 12px;
  border: 2px solid #ddd;
  padding: 20px;
}

/* Right column top - Audio playback */
.audio-section {
  grid-column: 2;
  grid-row: 1;
  background: white;
  border-radius: 12px;
  border: 2px solid #ddd;
  padding: 20px;
}

/* Right column second - Export queue */
.return-section {
  grid-column: 2;
  grid-row: 2;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 15px;
}

/* Right column third - File resource lists */
.files-section {
  grid-column: 2;
  grid-row: 3;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 15px;
}

/* Bottom - Message section */
.message-section {
  grid-column: 1 / 3;
  grid-row: 4;
  background: white;
  border-radius: 12px;
  border: 2px solid #ddd;
  padding: 20px;
}

.video-controls {
  display: flex;
  gap: 10px;
  margin-bottom: 15px;
  flex-wrap: wrap;
}

.video-display {
  background: #000;
  border-radius: 8px;
  position: relative;
  padding-bottom: 75%;
  overflow: hidden;
}

.video-display img {
  position: absolute;
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.audio-bar {
  background: #f0f0f0;
  border-radius: 8px;
  padding: 15px;
  margin-bottom: 15px;
  text-align: center;
  font-weight: bold;
  color: #333;
}

.recording-timer {
  font-size: 32px;
  color: #e74c3c;
  font-family: monospace;
  margin: 15px 0;
  text-align: center;
}

.processing-msg {
  text-align: center;
  color: #667eea;
  font-size: 16px;
  margin: 15px 0;
  font-weight: bold;
}

.file-column, .return-column {
  background: white;
  border-radius: 12px;
  border: 2px solid #ddd;
  padding: 15px;
}

.file-column h3, .return-column h3 {
  margin-bottom: 10px;
  color: #333;
  font-size: 14px;
  border-bottom: 2px solid #667eea;
  padding-bottom: 8px;
}

.file-list, .return-list {
  max-height: 150px;
  overflow-y: auto;
  margin-bottom: 10px;
}

.file-item {
  background: #f8f8f8;
  padding: 8px;
  margin-bottom: 5px;
  border-radius: 6px;
  cursor: pointer;
  border: 2px solid transparent;
  font-size: 12px;
}

.file-item:hover {
  background: #e8e8e8;
}

.file-item.selected {
  border-color: #667eea;
  background: #e8eeff;
}

.message-display {
  background: #f9f9f9;
  border-radius: 8px;
  padding: 15px;
  min-height: 150px;
  max-height: 300px;
  overflow-y: auto;
  margin-bottom: 15px;
  font-size: 14px;
  line-height: 1.6;
  border: 1px solid #ddd;
  white-space: pre-wrap;
}

.message-controls {
  display: flex;
  gap: 10px;
}

button {
  padding: 10px 20px;
  font-size: 14px;
  font-weight: 600;
  border: none;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.2s;
  color: white;
}

button:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0,0,0,0.15);
}

button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-stream { background: #3498db; }
.btn-stream.active { 
  background: #27ae60; 
  animation: pulse 1.5s infinite;
}
.btn-capture { background: #667eea; }
.btn-review { background: #9b59b6; }
.btn-record { background: #e74c3c; }
.btn-playback { background: #2ecc71; }
.btn-delete { background: #e67e22; }
.btn-save { background: #16a085; }
.btn-send { background: #2c3e50; flex: 1; }
.btn-replay { background: #34495e; flex: 1; }

.btn-record.recording {
  animation: pulse 1.5s infinite;
}

.streaming-indicator {
  position: absolute;
  top: 10px;
  left: 10px;
  background: rgba(39, 174, 96, 0.9);
  color: white;
  padding: 6px 12px;
  border-radius: 20px;
  font-size: 12px;
  font-weight: bold;
  display: none;
  z-index: 10;
  box-shadow: 0 2px 8px rgba(0,0,0,0.3);
}

.streaming-indicator.active {
  display: flex;
  align-items: center;
  gap: 8px;
}

.streaming-indicator::before {
  content: '';
  width: 8px;
  height: 8px;
  background: white;
  border-radius: 50%;
  animation: blink 1s infinite;
}

@keyframes blink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.3; }
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.6; }
}

.status-bar {
  position: fixed;
  top: 20px;
  right: 20px;
  background: #2ecc71;
  color: white;
  padding: 12px 20px;
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0,0,0,0.2);
  display: none;
  z-index: 1000;
  max-width: 300px;
}

.status-bar.show {
  display: block;
}

.status-bar.error {
  background: #e74c3c;
}

.info-banner {
  background: #fff3cd;
  padding: 12px;
  border-radius: 8px;
  margin-bottom: 15px;
  border-left: 4px solid #ffc107;
  font-size: 13px;
}

@media (max-width: 1024px) {
  .main-grid {
    grid-template-columns: 1fr;
  }
  
  .video-section, .audio-section, .capture-controls, 
  .return-section, .files-section, .message-section {
    grid-column: 1;
  }
  
  .video-section {
    grid-row: auto;
  }
}
</style>
</head>
<body>
<div id="statusBar" class="status-bar"></div>

<div class="main-grid">
  <!-- Left: Video Section -->
  <div class="video-section">
    <div class="info-banner">
      <strong>Pairing Workflow:</strong> Capture image/audio → Save to export → Send to OpenAI agent
    </div>
    <div class="video-controls">
      <button class="btn-stream" onclick="toggleStream()">Video Stream</button>
      <button class="btn-capture" onclick="captureImage()">Capture</button>
      <button class="btn-review" onclick="reviewLastImage()">Review</button>
    </div>
    <div class="video-display">
      <div id="streamingIndicator" class="streaming-indicator">LIVE</div>
      <img id="videoStream" src="">
    </div>
  </div>

  <!-- Right Row 1: Audio Playback -->
  <div class="audio-section">
    <div class="audio-bar">Audio Playback Bar</div>
    <audio id="audioPlayer" controls style="width:100%; margin-bottom:15px"></audio>
    <div id="recordingTimer" class="recording-timer" style="display:none">00:00</div>
    <div id="processingMsg" class="processing-msg" style="display:none">Recording audio, please wait...</div>
    <div class="video-controls">
      <button id="recordBtn" class="btn-record" onclick="toggleRecording()">Record</button>
      <button id="playbackBtn" class="btn-playback" onclick="playbackAudio()" disabled>Speaker</button>
    </div>
  </div>

  <!-- Right Row 2: Export pairing queue -->
  <div class="return-section">
    <div class="return-column">
      <h3>Image for Export</h3>
      <div class="file-item" id="exportedImageDisplay" style="min-height:60px; display:flex; align-items:center; justify-content:center; color:#999; font-size:13px">None selected</div>
    </div>
    <div class="return-column">
      <h3>Audio for Export</h3>
      <div class="file-item" id="exportedAudioDisplay" style="min-height:60px; display:flex; align-items:center; justify-content:center; color:#999; font-size:13px">None selected</div>
    </div>
  </div>

  <!-- Right Row 3: Resource file lists with controls -->
  <div class="files-section">
    <div class="file-column">
      <h3>Image Files (Resources)</h3>
      <div class="file-list" id="imageFileList"></div>
      <div class="video-controls" style="margin-top:10px">
        <button class="btn-save" onclick="addSelectedImageToExport()" style="flex:1" id="imageToExportBtn" disabled>Add to Export</button>
        <button class="btn-save" onclick="downloadSelectedImage()" style="flex:1; background:#27ae60" id="imageDownloadBtn" disabled>Save to PC</button>
      </div>
      <div class="video-controls" style="margin-top:5px">
        <button class="btn-delete" onclick="deleteSelectedImageFile()" style="flex:1" id="imageFileDeleteBtn" disabled>Delete File</button>
      </div>
    </div>
    <div class="file-column">
      <h3>Audio Files (Resources)</h3>
      <div class="file-list" id="audioFileList"></div>
      <div class="video-controls" style="margin-top:10px">
        <button class="btn-save" onclick="addSelectedAudioToExport()" style="flex:1" id="audioToExportBtn" disabled>Add to Export</button>
        <button class="btn-save" onclick="downloadSelectedAudio()" style="flex:1; background:#27ae60" id="audioDownloadBtn" disabled>Save to PC</button>
      </div>
      <div class="video-controls" style="margin-top:5px">
        <button class="btn-delete" onclick="deleteSelectedAudioFile()" style="flex:1" id="audioFileDeleteBtn" disabled>Delete File</button>
      </div>
    </div>
  </div>

  <!-- Bottom: Message Section -->
  <div class="message-section">
    <h3 style="margin-bottom:15px; color:#333">OpenAI Analysis Results</h3>
    <div class="message-display" id="messageDisplay">OpenAI analysis with TTS will appear here...</div>
    <div class="message-controls">
      <button class="btn-send" onclick="sendToOpenAI()">Send to OpenAI</button>
      <button class="btn-replay" onclick="replayTTSAudio()">Replay TTS</button>
      <button class="btn-save" onclick="downloadAnalysisPackage()">Export Package</button>
    </div>
  </div>
</div>

<script>
let recordingStartTime = 0;
let timerInterval = null;
let streamActive = false;
let streamInterval = null;
let selectedImageFile = "";
let selectedAudioFile = "";
let selectedImageForExport = "";
let selectedAudioForExport = "";
let lastAnalysisResponse = "";
let lastTTSAudioFile = "";
let lastAnalysisID = "";
let isRecording = false;

function showStatus(message, isError = false) {
  const statusBar = document.getElementById('statusBar');
  statusBar.textContent = message;
  statusBar.className = 'status-bar show' + (isError ? ' error' : '');
  setTimeout(() => statusBar.classList.remove('show'), 3000);
}

function toggleStream() {
  const videoStream = document.getElementById('videoStream');
  const indicator = document.getElementById('streamingIndicator');
  const streamBtn = document.querySelector('.btn-stream');
  
  streamActive = !streamActive;
  
  if (streamActive) {
    videoStream.src = '/stream?' + Date.now();
    startStreamRefresh();
    indicator.classList.add('active');
    streamBtn.classList.add('active');
    streamBtn.textContent = 'Stop Stream';
    showStatus('Stream started');
  } else {
    stopStreamRefresh();
    videoStream.src = '';
    indicator.classList.remove('active');
    streamBtn.classList.remove('active');
    streamBtn.textContent = 'Video Stream';
    showStatus('Stream stopped');
  }
}

function startStreamRefresh() {
  if (streamInterval) clearInterval(streamInterval);
  streamInterval = setInterval(() => {
    if (streamActive && !isRecording) {
      document.getElementById('videoStream').src = '/stream?' + Date.now();
    }
  }, 150);
}

function stopStreamRefresh() {
  if (streamInterval) {
    clearInterval(streamInterval);
    streamInterval = null;
  }
}

function captureImage() {
  fetch('/capture')
    .then(res => res.json())
    .then(data => {
      if (data.success) {
        showStatus('Image captured: ' + data.filename);
        loadImageList();
      } else {
        showStatus('Capture failed', true);
      }
    })
    .catch(err => showStatus('Capture error', true));
}

function reviewLastImage() {
  fetch('/image/latest')
    .then(res => res.json())
    .then(data => {
      if (data.success && data.filename) {
        stopStreamRefresh();
        streamActive = false;
        document.getElementById('videoStream').src = '/image?file=' + encodeURIComponent(data.filename);
        showStatus('Reviewing: ' + data.filename);
      } else {
        showStatus('No images to review', true);
      }
    })
    .catch(err => showStatus('Review failed', true));
}

function loadImageList() {
  fetch('/list/images')
    .then(res => res.json())
    .then(data => {
      const list = document.getElementById('imageFileList');
      list.innerHTML = '';
      
      if (data.files.length === 0) {
        list.innerHTML = '<div style="text-align:center; color:#999; padding:10px">No images</div>';
        return;
      }
      
      data.files.forEach(file => {
        const item = document.createElement('div');
        item.className = 'file-item';
        if (file === selectedImageFile) {
          item.classList.add('selected');
        }
        item.textContent = file;
        item.onclick = () => selectImageFile(file);
        list.appendChild(item);
      });
    });
}

function loadAudioList() {
  fetch('/list/audio')
    .then(res => res.json())
    .then(data => {
      const list = document.getElementById('audioFileList');
      list.innerHTML = '';
      
      if (data.files.length === 0) {
        list.innerHTML = '<div style="text-align:center; color:#999; padding:10px">No audio</div>';
        return;
      }
      
      data.files.forEach(file => {
        const item = document.createElement('div');
        item.className = 'file-item';
        if (file === selectedAudioFile) {
          item.classList.add('selected');
        }
        item.textContent = file;
        item.onclick = () => selectAudioFile(file);
        list.appendChild(item);
      });
    });
}

function selectImageFile(filename) {
  selectedImageFile = filename;
  
  // Update list highlighting
  document.querySelectorAll('#imageFileList .file-item').forEach(item => {
    item.classList.toggle('selected', item.textContent === filename);
  });
  
  // Enable buttons
  document.getElementById('imageToExportBtn').disabled = false;
  document.getElementById('imageDownloadBtn').disabled = false;
  document.getElementById('imageFileDeleteBtn').disabled = false;
  
  showStatus('Selected: ' + filename);
}

function selectAudioFile(filename) {
  selectedAudioFile = filename;
  
  // Update list highlighting
  document.querySelectorAll('#audioFileList .file-item').forEach(item => {
    item.classList.toggle('selected', item.textContent === filename);
  });
  
  // Enable buttons
  document.getElementById('audioToExportBtn').disabled = false;
  document.getElementById('audioDownloadBtn').disabled = false;
  document.getElementById('audioFileDeleteBtn').disabled = false;
  
  // Load in audio player
  const audioPlayer = document.getElementById('audioPlayer');
  audioPlayer.src = '/audio?file=' + encodeURIComponent(filename);
  audioPlayer.style.display = 'block';
  document.getElementById('playbackBtn').disabled = false;
  
  showStatus('Selected: ' + filename);
}

function addSelectedImageToExport() {
  if (!selectedImageFile) {
    showStatus('No image selected', true);
    return;
  }
  selectedImageForExport = selectedImageFile;
  document.getElementById('exportedImageDisplay').textContent = selectedImageFile;
  document.getElementById('exportedImageDisplay').style.color = '#333';
  showStatus('Added to export queue: ' + selectedImageFile);
}

function addSelectedAudioToExport() {
  if (!selectedAudioFile) {
    showStatus('No audio selected', true);
    return;
  }
  selectedAudioForExport = selectedAudioFile;
  document.getElementById('exportedAudioDisplay').textContent = selectedAudioFile;
  document.getElementById('exportedAudioDisplay').style.color = '#333';
  showStatus('Added to export queue: ' + selectedAudioFile);
}

function downloadSelectedImage() {
  if (!selectedImageFile) {
    showStatus('No image selected', true);
    return;
  }
  window.location.href = '/image?file=' + encodeURIComponent(selectedImageFile);
  showStatus('Downloading: ' + selectedImageFile);
}

function downloadSelectedAudio() {
  if (!selectedAudioFile) {
    showStatus('No audio selected', true);
    return;
  }
  window.location.href = '/audio?file=' + encodeURIComponent(selectedAudioFile);
  showStatus('Downloading: ' + selectedAudioFile);
}

function deleteSelectedImageFile() {
  if (!selectedImageFile) {
    showStatus('No image selected', true);
    return;
  }
  
  if (confirm('Delete ' + selectedImageFile + ' from SD card?')) {
    fetch('/delete/image?file=' + encodeURIComponent(selectedImageFile), { method: 'DELETE' })
      .then(() => {
        showStatus('Deleted: ' + selectedImageFile);
        
        // Clear export if this was the exported file
        if (selectedImageForExport === selectedImageFile) {
          selectedImageForExport = "";
          document.getElementById('exportedImageDisplay').textContent = 'None selected';
          document.getElementById('exportedImageDisplay').style.color = '#999';
        }
        
        selectedImageFile = "";
        document.getElementById('imageToExportBtn').disabled = true;
        document.getElementById('imageDownloadBtn').disabled = true;
        document.getElementById('imageFileDeleteBtn').disabled = true;
        
        loadImageList();
      });
  }
}

function deleteSelectedAudioFile() {
  if (!selectedAudioFile) {
    showStatus('No audio selected', true);
    return;
  }
  
  if (confirm('Delete ' + selectedAudioFile + ' from SD card?')) {
    fetch('/delete/audio?file=' + encodeURIComponent(selectedAudioFile), { method: 'DELETE' })
      .then(() => {
        showStatus('Deleted: ' + selectedAudioFile);
        
        // Clear export if this was the exported file
        if (selectedAudioForExport === selectedAudioFile) {
          selectedAudioForExport = "";
          document.getElementById('exportedAudioDisplay').textContent = 'None selected';
          document.getElementById('exportedAudioDisplay').style.color = '#999';
        }
        
        selectedAudioFile = "";
        document.getElementById('audioPlayer').src = ''; // Clear source but keep visible
        document.getElementById('playbackBtn').disabled = true;
        document.getElementById('audioToExportBtn').disabled = true;
        document.getElementById('audioDownloadBtn').disabled = true;
        document.getElementById('audioFileDeleteBtn').disabled = true;
        
        loadAudioList();
      });
  }
}

function updateTimer() {
  const elapsed = Math.floor((Date.now() - recordingStartTime) / 1000);
  const minutes = Math.floor(elapsed / 60);
  const seconds = elapsed % 60;
  document.getElementById('recordingTimer').textContent = 
    String(minutes).padStart(2, '0') + ':' + String(seconds).padStart(2, '0');
}

function toggleRecording() {
  const recordBtn = document.getElementById('recordBtn');
  
  if (isRecording) {
    clearInterval(timerInterval);
    const duration = Math.floor((Date.now() - recordingStartTime) / 1000);
    
    stopStreamRefresh();
    recordBtn.textContent = 'Processing...';
    recordBtn.disabled = true;
    recordBtn.classList.remove('recording');
    document.getElementById('recordingTimer').style.display = 'none';
    document.getElementById('processingMsg').style.display = 'block';
    
    showStatus('Recording ' + duration + ' seconds...');
    
    const timeout = (duration + 10) * 1000;
    Promise.race([
      fetch('/audio/record?duration=' + duration, { signal: AbortSignal.timeout(timeout) }),
      new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), timeout))
    ])
      .then(res => res.json())
      .then(data => {
        if (data.success) {
          showStatus('Audio saved: ' + data.filename);
          loadAudioList();
        } else {
          showStatus('Recording failed', true);
        }
      })
      .catch(err => {
        showStatus('Recording timeout - audio may still be saving', true);
        setTimeout(loadAudioList, 2000);
      })
      .finally(() => {
        recordBtn.textContent = 'Record';
        recordBtn.disabled = false;
        document.getElementById('processingMsg').style.display = 'none';
        isRecording = false;
        if (streamActive) startStreamRefresh();
      });
  } else {
    isRecording = true;
    recordingStartTime = Date.now();
    recordBtn.classList.add('recording');
    recordBtn.textContent = 'Stop';
    document.getElementById('recordingTimer').style.display = 'block';
    timerInterval = setInterval(updateTimer, 100);
    showStatus('Recording started');
  }
}

function playbackAudio() {
  if (!selectedAudioFile) {
    showStatus('No audio selected', true);
    return;
  }
  
  showStatus('Playing on speaker...');
  fetch('/audio/speaker?file=' + encodeURIComponent(selectedAudioFile), { signal: AbortSignal.timeout(60000) })
    .then(res => res.json())
    .then(data => {
      if (data.success) {
        showStatus('Playback complete');
      } else {
        showStatus('Playback failed', true);
      }
    })
    .catch(err => showStatus('Playback timeout', true));
}

function sendToOpenAI() {
  if (!selectedImageForExport || !selectedAudioForExport) {
    showStatus('Select image and audio for export first', true);
    return;
  }
  
  lastAnalysisID = Date.now().toString();
  stopStreamRefresh();
  
  showStatus('Starting OpenAI analysis...');
  document.getElementById('messageDisplay').textContent = 'Initializing analysis...';
  
  // Send request to start analysis
  fetch('/openai/analyze', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      image: selectedImageForExport,
      audio: selectedAudioForExport,
      id: lastAnalysisID
    })
  })
    .then(res => res.json())
    .then(data => {
      console.log('Analysis request response:', data);
      
      if (data.status === 'processing') {
        console.log('Starting progress polling...');
        
        // Function to check progress
        const checkProgress = () => {
          fetch('/openai/progress')
            .then(res => res.json())
            .then(progress => {
              console.log('Progress update:', progress);
              
              if (progress.resultReady) {
                // Analysis complete - show results
                clearInterval(progressInterval);
                console.log('Analysis complete!');
                
                if (progress.success) {
                  lastAnalysisResponse = progress.response;
                  lastTTSAudioFile = progress.ttsFile;
                  
                  document.getElementById('messageDisplay').textContent = progress.response;
                  showStatus('Analysis complete!');
                } else {
                  document.getElementById('messageDisplay').textContent = 
                    'Analysis failed:\n\n' + progress.response;
                  showStatus('Analysis failed', true);
                }
                
                if (streamActive) startStreamRefresh();
              } else if (progress.inProgress) {
                // Still processing - show progress
                const filledBlocks = Math.floor(progress.percent / 5);
                const progressBar = '=' .repeat(filledBlocks) + 
                                   '>' +
                                   '-'.repeat(Math.max(0, 19 - filledBlocks));
                document.getElementById('messageDisplay').textContent = 
                  `${progress.stage} - ${progress.percent}%\n[${progressBar}]\n\n${progress.detail}\n\n` +
                  `This process takes 60-90 seconds total.\nPlease wait...`;
                showStatus(`${progress.stage} - ${progress.percent}%`);
              }
            })
            .catch(err => {
              console.error('Progress check error:', err);
              clearInterval(progressInterval);
              showStatus('Progress check failed', true);
              if (streamActive) startStreamRefresh();
            });
        };
        
        // Check immediately, then every 1.5 seconds
        checkProgress();
        const progressInterval = setInterval(checkProgress, 1500);
      } else {
        console.error('Analysis start failed:', data);
        showStatus('Failed to start analysis', true);
        document.getElementById('messageDisplay').textContent = 'Error: ' + (data.error || 'Unknown error');
        if (streamActive) startStreamRefresh();
      }
    })
    .catch(err => {
      showStatus('Request failed', true);
      document.getElementById('messageDisplay').textContent = 'Request failed. Check serial monitor for details.';
      if (streamActive) startStreamRefresh();
    });
}

function replayTTSAudio() {
  if (!lastTTSAudioFile || lastTTSAudioFile === 'none') {
    showStatus('No TTS audio available', true);
    return;
  }
  
  const audioPlayer = document.getElementById('audioPlayer');
  audioPlayer.src = '/tts?file=' + encodeURIComponent(lastTTSAudioFile);
  audioPlayer.style.display = 'block';
  audioPlayer.currentTime = 0;
  audioPlayer.play();
  showStatus('Playing TTS response');
}

function downloadAnalysisPackage() {
  if (!lastAnalysisID) {
    showStatus('Complete an analysis first', true);
    return;
  }
  
  showStatus('Preparing package download...');
  
  // Download the entire analysis folder as a zip
  window.location.href = '/analysis/export?id=' + lastAnalysisID;
  
  setTimeout(() => {
    showStatus('Package downloaded');
  }, 1000);
}

// Initialize
loadImageList();
loadAudioList();

// Auto-start streaming
setTimeout(() => {
  toggleStream();
}, 500);
</script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("ESP32-S3 - OpenAI Interface v2.0.27");
  Serial.println("========================================\n");
  
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("ERROR: SD Card Mount Failed");
    return;
  }
  Serial.println("SD Card OK");
  
  SD_MMC.mkdir("/images");
  SD_MMC.mkdir("/audio");
  SD_MMC.mkdir("/tts");
  
  setupCamera();
  Serial.println("Camera OK");
  
  i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (!i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("PDM init failed!");
  } else {
    Serial.println("PDM Microphone OK");
  }
  
  i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  if (!i2s_spk.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Speaker init failed!");
  } else {
    Serial.println("Speaker OK");
  }
  
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println("IP: " + WiFi.localIP().toString());
  
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", index_html); });
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/image", HTTP_GET, handleImageView);
  server.on("/image/latest", HTTP_GET, handleLatestImage);
  server.on("/audio/record", HTTP_GET, handleAudioRecord);
  server.on("/audio", HTTP_GET, handleAudioStream);
  server.on("/audio/speaker", HTTP_GET, handleSpeakerPlayback);
  server.on("/tts", HTTP_GET, handleTTSStream);
  server.on("/list/images", HTTP_GET, handleListImages);
  server.on("/list/audio", HTTP_GET, handleListAudio);
  server.on("/delete/image", HTTP_DELETE, handleDeleteImage);
  server.on("/delete/audio", HTTP_DELETE, handleDeleteAudio);
  server.on("/openai/analyze", HTTP_POST, handleOpenAIAnalyze);
  server.on("/openai/progress", HTTP_GET, handleOpenAIProgress);
  server.on("/analysis/download", HTTP_POST, handleAnalysisDownload);
  server.on("/analysis/package", HTTP_GET, handleAnalysisPackage);
  server.on("/analysis/export", HTTP_GET, handleAnalysisExport);
  server.on("/analysis/file", HTTP_GET, handleAnalysisFile);
  
  server.begin();
  
  Serial.println("\n========================================");
  Serial.println("ESP32-S3 AI CAM - AUTOMATION AGENT");
  Serial.println("Version: 1.0.0 (Phase 1)");
  Serial.println("========================================");
  Serial.println("Serial API: ENABLED");
  Serial.println("Baud Rate: 115200");
  Serial.println("Web Interface: http://" + WiFi.localIP().toString());
  String apiKeyStatus = (String(OPENAI_API_KEY) == "sk-YOUR-OPENAI-API-KEY-HERE") ? "NOT SET!" : "Configured";
  Serial.println("API Key: " + apiKeyStatus);
  Serial.println("========================================");
  Serial.println("\nSerial Commands Available:");
  Serial.println("  CMD:PING");
  Serial.println("  CMD:VERSION");
  Serial.println("  CMD:STATUS");
  Serial.println("  CMD:CAPTURE");
  Serial.println("  CMD:RECORD:5");
  Serial.println("  CMD:LIST_IMAGES");
  Serial.println("  CMD:LIST_AUDIO");
  Serial.println("  CMD:GET_LAST_IMAGE");
  Serial.println("  CMD:GET_LAST_AUDIO");
  Serial.println("\nReady for commands...\n");
}

void handleStream() {
  if (recordingInProgress || ttsInProgress) { server.send(503); return; }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { server.send(503); return; }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { server.send(500, "application/json", "{\"success\":false}"); return; }
  imageCount++;
  String filename = "IMG_" + String(imageCount) + ".jpg";
  File file = SD_MMC.open("/images/" + filename, FILE_WRITE);
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + filename + "\"}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
  esp_camera_fb_return(fb);
}

void handleImageView() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/images/" + filename, FILE_READ);
  if (file) {
    server.streamFile(file, "image/jpeg");
    file.close();
  } else {
    server.send(404);
  }
}

void handleLatestImage() {
  File root = SD_MMC.open("/images");
  String latest = "";
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) latest = String(file.name());
  }
  if (latest.length() > 0) {
    server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + latest + "\"}");
  } else {
    server.send(200, "application/json", "{\"success\":false}");
  }
}

void handleAudioRecord() {
  if (recordingInProgress || ttsInProgress) {
    server.send(503, "application/json", "{\"success\":false,\"error\":\"busy\"}");
    return;
  }
  
  recordingInProgress = true;
  int duration = server.arg("duration").toInt();
  if (duration <= 0) duration = 5;
  
  int totalSamples = SAMPLE_RATE * duration;
  int16_t* audioBuffer = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) audioBuffer = (int16_t*)malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) {
    recordingInProgress = false;
    server.send(500, "application/json", "{\"success\":false,\"error\":\"memory\"}");
    return;
  }
  
  size_t samplesRead = 0;
  while (samplesRead < totalSamples) {
    size_t bytesRead = i2s_mic.readBytes((char*)(audioBuffer + samplesRead), 
                                         (totalSamples - samplesRead) * sizeof(int16_t));
    samplesRead += bytesRead / sizeof(int16_t);
    if (samplesRead % 16000 == 0) yield();
  }
  
  for (int i = 0; i < totalSamples; i++) {
    int32_t amplified = (int32_t)audioBuffer[i] * AMPLIFICATION;
    audioBuffer[i] = constrain(amplified, -32768, 32767);
  }
  
  audioCount++;
  String filename = "REC_" + String(audioCount) + ".wav";
  File file = SD_MMC.open("/audio/" + filename, FILE_WRITE);
  
  if (file) {
    uint32_t fileSize = 44 + (totalSamples * 2);
    file.write((uint8_t*)"RIFF", 4);
    uint32_t chunkSize = fileSize - 8;
    file.write((uint8_t*)&chunkSize, 4);
    file.write((uint8_t*)"WAVE", 4);
    file.write((uint8_t*)"fmt ", 4);
    uint32_t subchunk1Size = 16;
    file.write((uint8_t*)&subchunk1Size, 4);
    uint16_t audioFormat = 1;
    file.write((uint8_t*)&audioFormat, 2);
    uint16_t numChannels = 1;
    file.write((uint8_t*)&numChannels, 2);
    uint32_t sampleRate = SAMPLE_RATE;
    file.write((uint8_t*)&sampleRate, 4);
    uint32_t byteRate = SAMPLE_RATE * 2;
    file.write((uint8_t*)&byteRate, 4);
    uint16_t blockAlign = 2;
    file.write((uint8_t*)&blockAlign, 2);
    uint16_t bitsPerSample = 16;
    file.write((uint8_t*)&bitsPerSample, 2);
    file.write((uint8_t*)"data", 4);
    uint32_t dataSize = totalSamples * 2;
    file.write((uint8_t*)&dataSize, 4);
    file.write((uint8_t*)audioBuffer, totalSamples * 2);
    file.close();
    
    free(audioBuffer);
    recordingInProgress = false;
    server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + filename + "\"}");
  } else {
    free(audioBuffer);
    recordingInProgress = false;
    server.send(500, "application/json", "{\"success\":false}");
  }
}

void handleAudioStream() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/audio/" + filename, FILE_READ);
  if (file) {
    server.streamFile(file, "audio/wav");
    file.close();
  } else {
    server.send(404);
  }
}

void handleSpeakerPlayback() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/audio/" + filename, FILE_READ);
  if (!file) {
    server.send(404, "application/json", "{\"success\":false}");
    return;
  }
  
  // Skip WAV header (44 bytes)
  file.seek(44);
  
  // Buffer for reading mono samples and writing stereo
  uint8_t monoBuffer[256];  // Read 128 samples at a time
  uint8_t stereoBuffer[512]; // Write 128 stereo samples (256 mono samples duplicated)
  
  while (file.available()) {
    size_t bytesRead = file.read(monoBuffer, sizeof(monoBuffer));
    if (bytesRead > 0) {
      // Convert mono to stereo by duplicating each 16-bit sample
      int16_t* monoSamples = (int16_t*)monoBuffer;
      int16_t* stereoSamples = (int16_t*)stereoBuffer;
      size_t numSamples = bytesRead / 2; // Number of 16-bit mono samples
      
      for (size_t i = 0; i < numSamples; i++) {
        stereoSamples[i * 2] = monoSamples[i];      // Left channel
        stereoSamples[i * 2 + 1] = monoSamples[i];  // Right channel (duplicate)
      }
      
      // Write stereo data (twice the size of mono)
      i2s_spk.write(stereoBuffer, numSamples * 4);
    }
    yield();
  }
  file.close();
  
  server.send(200, "application/json", "{\"success\":true}");
}

void handleTTSStream() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/tts/" + filename, FILE_READ);
  if (file) {
    server.streamFile(file, "audio/mpeg");
    file.close();
  } else {
    server.send(404);
  }
}

void handleListImages() {
  File root = SD_MMC.open("/images");
  String json = "{\"files\":[";
  bool first = true;
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) {
      if (!first) json += ",";
      json += "\"" + String(file.name()) + "\"";
      first = false;
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleListAudio() {
  File root = SD_MMC.open("/audio");
  String json = "{\"files\":[";
  bool first = true;
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) {
      if (!first) json += ",";
      json += "\"" + String(file.name()) + "\"";
      first = false;
    }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleDeleteImage() {
  String filename = server.arg("file");
  if (SD_MMC.remove("/images/" + filename)) {
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
}

void handleDeleteAudio() {
  String filename = server.arg("file");
  if (SD_MMC.remove("/audio/" + filename)) {
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(500, "application/json", "{\"success\":false}");
  }
}

// Background task that runs the actual OpenAI analysis
void runOpenAIAnalysis() {
  analysisResultReady = false;
  analysisResultSuccess = false;
  
  Serial.println("\n========================================");
  Serial.println("OpenAI Analysis Request");
  Serial.println("========================================");
  Serial.println("Image: " + currentImagePath);
  Serial.println("Audio: " + currentAudioPath);
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  
  analysisProgressStage = "Validation";
  analysisProgressDetail = "Checking files...";
  analysisProgressPercent = 5;
  server.handleClient(); // Allow progress requests
  yield();
  
  if (!SD_MMC.exists(currentImagePath) || !SD_MMC.exists(currentAudioPath)) {
    Serial.println("ERROR: File not found");
    analysisInProgress = false;
    analysisProgressStage = "Error";
    analysisProgressDetail = "File not found";
    analysisResultText = "Error: File not found";
    analysisResultSuccess = false;
    analysisResultReady = true;
    return;
  }
  
  // Step 1: Whisper transcription
  analysisProgressStage = "Whisper";
  analysisProgressDetail = "Transcribing audio (30-40 sec)...";
  analysisProgressPercent = 10;
  server.handleClient(); // Allow progress requests
  yield();
  
  String transcription = transcribeAudioWithWhisper(currentAudioPath);
  if (transcription.startsWith("Error")) {
    Serial.println("ERROR: Whisper failed - " + transcription);
    analysisInProgress = false;
    analysisProgressStage = "Error";
    analysisProgressDetail = "Whisper failed";
    analysisResultText = transcription;
    analysisResultSuccess = false;
    analysisResultReady = true;
    return;
  }
  Serial.printf("Free heap after Whisper: %d bytes\n", ESP.getFreeHeap());
  
  // Step 2: Vision analysis
  analysisProgressStage = "Vision";
  analysisProgressDetail = "Analyzing image with GPT-4 (30-40 sec)...";
  analysisProgressPercent = 45;
  server.handleClient(); // Allow progress requests
  yield();
  
  String analysis = analyzeImageWithGPT4Vision(currentImagePath, transcription);
  if (analysis.startsWith("Error")) {
    Serial.println("ERROR: Vision failed - " + analysis);
    analysisInProgress = false;
    analysisProgressStage = "Error";
    analysisProgressDetail = "Vision failed";
    analysisResultText = analysis;
    analysisResultSuccess = false;
    analysisResultReady = true;
    return;
  }
  Serial.printf("Free heap after Vision: %d bytes\n", ESP.getFreeHeap());
  
  // Step 3: TTS generation
  analysisProgressStage = "TTS";
  analysisProgressDetail = "Generating speech audio (20-30 sec)...";
  analysisProgressPercent = 75;
  server.handleClient(); // Allow progress requests
  yield();
  
  String combinedText = "Audio Transcription: " + transcription + ". Image Analysis: " + analysis;
  String ttsFile = generateTTSAudio(combinedText, currentAnalysisID);
  Serial.printf("Free heap after TTS: %d bytes\n", ESP.getFreeHeap());
  
  if (ttsFile.length() == 0) ttsFile = "none";
  
  // Finalize
  analysisProgressStage = "Complete";
  analysisProgressDetail = "Analysis finished successfully";
  analysisProgressPercent = 100;
  
  String result = "=== AUDIO TRANSCRIPTION ===\n" + transcription + "\n\n=== IMAGE ANALYSIS ===\n" + analysis;
  
  analysisResultText = result;
  analysisResultTTSFile = ttsFile;
  analysisResultSuccess = true;
  analysisResultReady = true;
  
  Serial.println("\n=== Analysis Complete ===");
  Serial.println("TTS File: " + ttsFile);
  
  // Auto-save complete analysis package to SD card
  saveAnalysisPackage(currentAnalysisID, transcription, analysis, currentImagePath, currentAudioPath, ttsFile);
  
  analysisInProgress = false;
}

void saveAnalysisPackage(String analysisID, String transcription, String analysis, String imagePath, String audioPath, String ttsFile) {
  Serial.println("\n=== Saving Analysis Package ===");
  
  // Create analysis directory
  String analysisDir = "/analysis/" + analysisID;
  SD_MMC.mkdir("/analysis");
  SD_MMC.mkdir(analysisDir.c_str());
  
  Serial.println("Directory: " + analysisDir);
  
  // 1. Save prompt (audio transcription)
  File promptFile = SD_MMC.open(analysisDir + "/prompt.txt", FILE_WRITE);
  if (promptFile) {
    promptFile.println("Audio Transcription");
    promptFile.println("==================");
    promptFile.println(transcription);
    promptFile.close();
    Serial.println("Saved: prompt.txt");
  }
  
  // 2. Save response (image analysis)
  File responseFile = SD_MMC.open(analysisDir + "/response.txt", FILE_WRITE);
  if (responseFile) {
    responseFile.println("Image Analysis");
    responseFile.println("==============");
    responseFile.println(analysis);
    responseFile.close();
    Serial.println("Saved: response.txt");
  }
  
  // 3. Save combined analysis
  File combinedFile = SD_MMC.open(analysisDir + "/combined.txt", FILE_WRITE);
  if (combinedFile) {
    combinedFile.println("OpenAI Analysis Results");
    combinedFile.println("======================");
    combinedFile.println("Timestamp: " + analysisID);
    combinedFile.println("Image: " + imagePath);
    combinedFile.println("Audio: " + audioPath);
    combinedFile.println("");
    combinedFile.println("=== AUDIO TRANSCRIPTION ===");
    combinedFile.println(transcription);
    combinedFile.println("");
    combinedFile.println("=== IMAGE ANALYSIS ===");
    combinedFile.println(analysis);
    combinedFile.close();
    Serial.println("Saved: combined.txt");
  }
  
  // 4. Save metadata
  File metaFile = SD_MMC.open(analysisDir + "/metadata.txt", FILE_WRITE);
  if (metaFile) {
    metaFile.println("Analysis Metadata");
    metaFile.println("=================");
    metaFile.println("Analysis ID: " + analysisID);
    metaFile.println("Timestamp: " + analysisID);
    metaFile.println("Source Image: " + imagePath);
    metaFile.println("Source Audio: " + audioPath);
    metaFile.println("TTS File: /tts/" + ttsFile);
    metaFile.println("Prompt Length: " + String(transcription.length()) + " chars");
    metaFile.println("Response Length: " + String(analysis.length()) + " chars");
    metaFile.close();
    Serial.println("Saved: metadata.txt");
  }
  
  // 5. Copy TTS file to analysis folder (if it exists)
  if (ttsFile != "none" && ttsFile.length() > 0) {
    File srcTTS = SD_MMC.open("/tts/" + ttsFile, FILE_READ);
    File dstTTS = SD_MMC.open(analysisDir + "/audio.mp3", FILE_WRITE);
    if (srcTTS && dstTTS) {
      uint8_t buffer[512];
      while (srcTTS.available()) {
        size_t bytesRead = srcTTS.read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
          dstTTS.write(buffer, bytesRead);
        }
      }
      srcTTS.close();
      dstTTS.close();
      Serial.println("Saved: audio.mp3 (copied from /tts/" + ttsFile + ")");
    }
  }
  
  Serial.println("=== Package Saved Successfully ===");
  Serial.printf("Location: %s\n", analysisDir.c_str());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
}

void handleOpenAIAnalyze() {
  // Check if already processing
  if (analysisInProgress) {
    server.send(503, "application/json", "{\"success\":false,\"error\":\"Analysis already in progress\"}");
    return;
  }
  
  DynamicJsonDocument reqDoc(512);
  deserializeJson(reqDoc, server.arg("plain"));
  
  String imageFile = reqDoc["image"].as<String>();
  String audioFile = reqDoc["audio"].as<String>();
  currentAnalysisID = reqDoc["id"].as<String>();
  
  currentImagePath = "/images/" + imageFile;
  currentAudioPath = "/audio/" + audioFile;
  
  // Initialize progress tracking
  analysisInProgress = true;
  analysisResultReady = false;
  analysisProgressStage = "Starting";
  analysisProgressDetail = "Initializing analysis...";
  analysisProgressPercent = 0;
  
  // Respond immediately to allow polling
  server.send(202, "application/json", "{\"success\":true,\"status\":\"processing\"}");
  
  // Small delay to ensure response is sent
  delay(100);
  
  // Run analysis in "background" (it blocks but browser is already free to poll)
  runOpenAIAnalysis();
}

void handleOpenAIProgress() {
  DynamicJsonDocument doc(8192);
  doc["inProgress"] = analysisInProgress;
  doc["stage"] = analysisProgressStage;
  doc["detail"] = analysisProgressDetail;
  doc["percent"] = analysisProgressPercent;
  doc["resultReady"] = analysisResultReady;
  
  if (analysisResultReady) {
    doc["success"] = analysisResultSuccess;
    doc["response"] = analysisResultText;
    doc["ttsFile"] = analysisResultTTSFile;
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAnalysisDownload() {
  String body = server.arg("plain");
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, body);
  
  String id = doc["id"].as<String>();
  String analysisText = doc["text"].as<String>();
  
  String summaryFilename = "ANALYSIS_" + id + ".txt";
  File summaryFile = SD_MMC.open("/" + summaryFilename, FILE_WRITE);
  if (summaryFile) {
    summaryFile.println("OpenAI Analysis Results");
    summaryFile.println("======================");
    summaryFile.println(analysisText);
    summaryFile.close();
  }
  
  server.send(200, "application/json", "{\"success\":true,\"id\":\"" + id + "\"}");
}

void handleAnalysisPackage() {
  String id = server.arg("id");
  String filename = "ANALYSIS_" + id + ".txt";
  File file = SD_MMC.open("/" + filename, FILE_READ);
  if (file) {
    server.streamFile(file, "text/plain");
    file.close();
  } else {
    server.send(404);
  }
}

void handleAnalysisExport() {
  String id = server.arg("id");
  String analysisDir = "/analysis/" + id;
  
  Serial.println("\n=== Exporting Analysis Package ===");
  Serial.println("Analysis ID: " + id);
  Serial.println("Directory: " + analysisDir);
  
  // Check if directory exists
  File dir = SD_MMC.open(analysisDir);
  if (!dir || !dir.isDirectory()) {
    Serial.println("ERROR: Analysis directory not found");
    server.send(404, "text/plain", "Analysis not found");
    return;
  }
  
  // Create an HTML page that provides download links for all files
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Analysis Package " + id + "</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }";
  html += "h1 { color: #2c3e50; }";
  html += ".file-list { list-style: none; padding: 0; }";
  html += ".file-item { background: #ecf0f1; margin: 10px 0; padding: 15px; border-radius: 5px; }";
  html += ".file-item a { color: #3498db; text-decoration: none; font-weight: bold; font-size: 16px; }";
  html += ".file-item a:hover { text-decoration: underline; }";
  html += ".file-size { color: #7f8c8d; font-size: 14px; margin-left: 10px; }";
  html += ".download-all { background: #27ae60; color: white; padding: 12px 24px; ";
  html += "border: none; border-radius: 5px; cursor: pointer; font-size: 16px; margin: 20px 0; }";
  html += ".download-all:hover { background: #229954; }";
  html += "</style></head><body>";
  html += "<h1>Analysis Package</h1>";
  html += "<p><strong>Analysis ID:</strong> " + id + "</p>";
  html += "<p><strong>Location:</strong> " + analysisDir + "</p>";
  html += "<button class='download-all' onclick='downloadAll()'>Download All Files</button>";
  html += "<ul class='file-list'>";
  
  // List all files in directory
  File file = dir.openNextFile();
  int fileCount = 0;
  String fileList = "";
  
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      size_t filesize = file.size();
      fileCount++;
      
      html += "<li class='file-item'>";
      html += "<a href='/analysis/file?id=" + id + "&file=" + filename + "' download='" + filename + "'>";
      html += filename + "</a>";
      html += "<span class='file-size'>(" + String(filesize) + " bytes)</span>";
      html += "</li>";
      
      fileList += "'" + filename + "',";
    }
    file = dir.openNextFile();
  }
  dir.close();
  
  html += "</ul>";
  html += "<p>Total files: " + String(fileCount) + "</p>";
  
  // Add JavaScript for download all
  html += "<script>";
  html += "function downloadAll() {";
  html += "  const files = [" + fileList + "];";
  html += "  files.forEach((file, index) => {";
  html += "    setTimeout(() => {";
  html += "      const a = document.createElement('a');";
  html += "      a.href = '/analysis/file?id=" + id + "&file=' + file;";
  html += "      a.download = file;";
  html += "      document.body.appendChild(a);";
  html += "      a.click();";
  html += "      document.body.removeChild(a);";
  html += "    }, index * 500);";
  html += "  });";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
  Serial.println("=== Export Page Sent ===");
}

void handleAnalysisFile() {
  String id = server.arg("id");
  String filename = server.arg("file");
  String filepath = "/analysis/" + id + "/" + filename;
  
  Serial.println("Downloading: " + filepath);
  
  File file = SD_MMC.open(filepath, FILE_READ);
  if (file) {
    server.streamFile(file, "application/octet-stream");
    file.close();
  } else {
    server.send(404, "text/plain", "File not found");
  }
}

// ============================================
// SERIAL COMMAND INTERFACE - PHASE 1
// ============================================

void sendSerialResponse(String response) {
  Serial.println(response);
  Serial.flush();
}

void sendSerialOK(String data = "") {
  if (data.length() > 0) {
    sendSerialResponse("OK:" + data);
  } else {
    sendSerialResponse("OK");
  }
}

void sendSerialError(String message) {
  sendSerialResponse("ERROR:" + message);
}

String extractCommand(String input) {
  // Extract command from "CMD:COMMAND|PARAM:value" format
  int cmdStart = input.indexOf("CMD:");
  if (cmdStart == -1) return "";
  
  int cmdEnd = input.indexOf("|", cmdStart);
  if (cmdEnd == -1) cmdEnd = input.length();
  
  String cmd = input.substring(cmdStart + 4, cmdEnd);
  cmd.trim();
  return cmd;
}

String extractParam(String input, String paramName) {
  // Extract parameter value from "PARAM:value" format
  String search = paramName + ":";
  int paramStart = input.indexOf(search);
  if (paramStart == -1) return "";
  
  int valueStart = paramStart + search.length();
  int valueEnd = input.indexOf("|", valueStart);
  if (valueEnd == -1) valueEnd = input.length();
  
  String value = input.substring(valueStart, valueEnd);
  value.trim();
  return value;
}

void handleSerialPing() {
  sendSerialOK("PONG");
}

void handleSerialVersion() {
  sendSerialOK("AUTOMATION_AGENT_" + String(FIRMWARE_VERSION));
}

void handleSerialStatus() {
  String status = "READY";
  if (recordingInProgress) status = "RECORDING";
  if (analysisInProgress) status = "ANALYZING";
  
  // Count files
  int imageCount = 0;
  File imgDir = SD_MMC.open("/images");
  if (imgDir) {
    File file = imgDir.openNextFile();
    while (file) {
      if (!file.isDirectory()) imageCount++;
      file = imgDir.openNextFile();
    }
    imgDir.close();
  }
  
  int audioCount = 0;
  File audDir = SD_MMC.open("/audio");
  if (audDir) {
    File file = audDir.openNextFile();
    while (file) {
      if (!file.isDirectory()) audioCount++;
      file = audDir.openNextFile();
    }
    audDir.close();
  }
  
  String response = status;
  response += "|HEAP:" + String(ESP.getFreeHeap());
  response += "|IMAGES:" + String(imageCount);
  response += "|AUDIO:" + String(audioCount);
  response += "|STREAMING:" + String(streamingEnabled ? "ON" : "OFF");
  
  sendSerialOK(response);
}

void handleSerialCapture() {
  if (!streamingEnabled) {
    sendSerialError("Streaming not enabled");
    return;
  }
  
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    sendSerialError("Camera capture failed");
    return;
  }
  
  imageCount++;
  String filename = "IMG_" + String(imageCount) + ".jpg";
  File file = SD_MMC.open("/images/" + filename, FILE_WRITE);
  
  if (file) {
    file.write(fb->buf, fb->len);
    file.close();
    lastCapturedImage = filename;
    esp_camera_fb_return(fb);
    sendSerialOK(filename);
  } else {
    esp_camera_fb_return(fb);
    sendSerialError("SD card write failed");
  }
}

void handleSerialRecord(int duration) {
  if (recordingInProgress || ttsInProgress) {
    sendSerialError("System busy");
    return;
  }
  
  if (duration <= 0 || duration > 60) {
    sendSerialError("Duration must be 1-60 seconds");
    return;
  }
  
  recordingInProgress = true;
  int totalSamples = SAMPLE_RATE * duration;
  int16_t* audioBuffer = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) audioBuffer = (int16_t*)malloc(totalSamples * sizeof(int16_t));
  
  if (!audioBuffer) {
    recordingInProgress = false;
    sendSerialError("Memory allocation failed");
    return;
  }
  
  size_t samplesRead = 0;
  while (samplesRead < totalSamples) {
    size_t bytesRead = i2s_mic.readBytes((char*)(audioBuffer + samplesRead), 
                                         (totalSamples - samplesRead) * sizeof(int16_t));
    samplesRead += bytesRead / sizeof(int16_t);
    if (samplesRead % 16000 == 0) yield();
  }
  
  for (int i = 0; i < totalSamples; i++) {
    int32_t amplified = (int32_t)audioBuffer[i] * AMPLIFICATION;
    audioBuffer[i] = constrain(amplified, -32768, 32767);
  }
  
  audioCount++;
  String filename = "REC_" + String(audioCount) + ".wav";
  File file = SD_MMC.open("/audio/" + filename, FILE_WRITE);
  
  if (file) {
    uint32_t fileSize = 44 + (totalSamples * 2);
    file.write((uint8_t*)"RIFF", 4);
    uint32_t chunkSize = fileSize - 8;
    file.write((uint8_t*)&chunkSize, 4);
    file.write((uint8_t*)"WAVE", 4);
    file.write((uint8_t*)"fmt ", 4);
    uint32_t subchunk1Size = 16;
    file.write((uint8_t*)&subchunk1Size, 4);
    uint16_t audioFormat = 1;
    file.write((uint8_t*)&audioFormat, 2);
    uint16_t numChannels = 1;
    file.write((uint8_t*)&numChannels, 2);
    uint32_t sampleRate = SAMPLE_RATE;
    file.write((uint8_t*)&sampleRate, 4);
    uint32_t byteRate = SAMPLE_RATE * 2;
    file.write((uint8_t*)&byteRate, 4);
    uint16_t blockAlign = 2;
    file.write((uint8_t*)&blockAlign, 2);
    uint16_t bitsPerSample = 16;
    file.write((uint8_t*)&bitsPerSample, 2);
    file.write((uint8_t*)"data", 4);
    uint32_t dataSize = totalSamples * 2;
    file.write((uint8_t*)&dataSize, 4);
    file.write((uint8_t*)audioBuffer, totalSamples * 2);
    file.close();
    
    free(audioBuffer);
    recordingInProgress = false;
    lastRecordedAudio = filename;
    sendSerialOK(filename);
  } else {
    free(audioBuffer);
    recordingInProgress = false;
    sendSerialError("SD card write failed");
  }
}

void handleSerialListImages() {
  File root = SD_MMC.open("/images");
  if (!root) {
    sendSerialError("Cannot open images directory");
    return;
  }
  
  String fileList = "";
  int count = 0;
  File file = root.openNextFile();
  
  while (file) {
    if (!file.isDirectory()) {
      if (count > 0) fileList += ",";
      fileList += String(file.name());
      count++;
    }
    file = root.openNextFile();
  }
  root.close();
  
  if (count == 0) {
    sendSerialOK("NONE");
  } else {
    sendSerialOK(fileList);
  }
}

void handleSerialListAudio() {
  File root = SD_MMC.open("/audio");
  if (!root) {
    sendSerialError("Cannot open audio directory");
    return;
  }
  
  String fileList = "";
  int count = 0;
  File file = root.openNextFile();
  
  while (file) {
    if (!file.isDirectory()) {
      if (count > 0) fileList += ",";
      fileList += String(file.name());
      count++;
    }
    file = root.openNextFile();
  }
  root.close();
  
  if (count == 0) {
    sendSerialOK("NONE");
  } else {
    sendSerialOK(fileList);
  }
}

void handleSerialGetLastImage() {
  if (lastCapturedImage.length() > 0) {
    sendSerialOK(lastCapturedImage);
  } else {
    sendSerialError("No image captured yet");
  }
}

void handleSerialGetLastAudio() {
  if (lastRecordedAudio.length() > 0) {
    sendSerialOK(lastRecordedAudio);
  } else {
    sendSerialError("No audio recorded yet");
  }
}

void processSerialCommand(String cmdString) {
  cmdString.trim();
  
  if (cmdString.length() == 0) return;
  
  // Echo command for debugging
  Serial.println("# Processing: " + cmdString);
  
  String command = extractCommand(cmdString);
  
  if (command.length() == 0) {
    sendSerialError("Invalid command format. Use CMD:command");
    return;
  }
  
  // Route to appropriate handler
  if (command == "PING") {
    handleSerialPing();
  }
  else if (command == "VERSION") {
    handleSerialVersion();
  }
  else if (command == "STATUS") {
    handleSerialStatus();
  }
  else if (command == "CAPTURE") {
    handleSerialCapture();
  }
  else if (command == "RECORD") {
    String durationStr = extractParam(cmdString, "DURATION");
    if (durationStr.length() == 0) {
      // Try old format: CMD:RECORD:5
      int colonPos = cmdString.indexOf(":", 4); // After "CMD:"
      if (colonPos > 0) {
        durationStr = cmdString.substring(colonPos + 1);
      }
    }
    int duration = durationStr.toInt();
    handleSerialRecord(duration);
  }
  else if (command == "LIST_IMAGES") {
    handleSerialListImages();
  }
  else if (command == "LIST_AUDIO") {
    handleSerialListAudio();
  }
  else if (command == "GET_LAST_IMAGE") {
    handleSerialGetLastImage();
  }
  else if (command == "GET_LAST_AUDIO") {
    handleSerialGetLastAudio();
  }
  else {
    sendSerialError("Unknown command: " + command);
  }
}

void handleSerialCommands() {
  // Non-blocking serial command reader
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (serialCommandBuffer.length() > 0) {
        serialCommandReady = true;
        break;
      }
    } else {
      serialCommandBuffer += c;
      
      // Prevent buffer overflow
      if (serialCommandBuffer.length() > 256) {
        serialCommandBuffer = "";
        sendSerialError("Command too long (max 256 chars)");
      }
    }
  }
  
  if (serialCommandReady) {
    processSerialCommand(serialCommandBuffer);
    serialCommandBuffer = "";
    serialCommandReady = false;
  }
}

// ============================================
// END SERIAL COMMAND INTERFACE
// ============================================

void loop() {
  server.handleClient();
  handleSerialCommands();  // Process serial commands
  yield();
}
