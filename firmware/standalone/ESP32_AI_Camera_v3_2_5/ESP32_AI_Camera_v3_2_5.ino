#include "time.h"
/*
 * ESP32-S3 AI CAM - OpenAI Interface v3.2.5
 *
 * v3.2.5 FIX: TTS SPEAKER PLAYBACK SAMPLE RATE MISMATCH
 * - Root cause: generateTTSAudio() saves the OpenAI TTS response as a native
 *   24kHz WAV file (correct — browser plays it perfectly at 24kHz). However
 *   i2s_spk is initialized at SAMPLE_RATE (16000 Hz) in setup(). When
 *   handleSpeakerPlayback() fed the 24kHz PCM to the speaker at 16kHz clock
 *   rate, the result was playback at 67% speed (16000/24000), lower volume,
 *   and DMA buffer noise/halting from the sample rate mismatch.
 *   Web browser playback was unaffected because the browser reads the WAV
 *   fmt chunk and resamples correctly regardless of ESP32 I2S state.
 * - Fix: Added TTS_SAMPLE_RATE define (24000). Added dedicated
 *   handleTTSSpeakerPlayback() function that reinitializes i2s_spk at
 *   TTS_SAMPLE_RATE before playback and restores it to SAMPLE_RATE (16kHz)
 *   afterward. Registered as new route /tts/speaker in setup().
 *   handleSpeakerPlayback() (/audio/speaker) is unchanged — recorded audio
 *   (REC files) remains at 16kHz throughout.
 * - Added Speaker button to TTS audio playback bar in web UI. Clicking
 *   Speaker while a TTS file is loaded calls /tts/speaker to play through
 *   the MAX98357A at correct 24kHz rate.
 * - Note: The i2s_spk.end()/begin() reinit adds ~5ms overhead at the start
 *   of TTS speaker playback. This is inaudible and necessary for correct rate.
 *
 * v3.2.4 FIX: WiFiManager::monitor() GUARD + HEADER FILE UPDATES
 * - WiFiManager::monitor() in error_handling.h now guards against firing
 *   during active TTS download or analysis. monitor() checks every 5 seconds
 *   from loop(). The TTS download loop yields via delay()/yield() between
 *   chunk reads, allowing monitor() to fire. If WiFi status showed as
 *   disconnected during a download, attemptReconnection() called
 *   WiFi.disconnect() — killing the HTTPS stream and corrupting the WAV file.
 *   Guard: extern volatile bool ttsInProgress / analysisInProgress — these
 *   resolve to the flags already defined in this .ino.
 * - WiFi.setSleep(false) restored after reconnection in attemptReconnection().
 * - Captive portal API key input: maxlength="220" in credentials_manager.h.
 * - API key sanitized before NVS save in captive portal /configure handler.
 * - generateTTSWithRetry() in error_handling.h: nova voice, wav format,
 *   chunked transfer decoder (consistent with main firmware).
 * All fixes are in the header files — no .ino logic changes in this version.
 * Deploy alongside updated error_handling.h and credentials_manager.h.
 *
 * v3.2.3 FIX: IMAGE EXPORT QUEUE DISPLAY
 * - Bug 1: "Clear All" button appeared with a single image queued.
 *   Condition was `length < MAX_IMAGES_FOR_EXPORT` (inverted logic — true
 *   whenever there was room for more images, i.e. almost always).
 *   Fixed to only show "Clear All" when 2 or more images are queued.
 * - Bug 2: Each entry was prefixed with a sequential index number
 *   (e.g. "1. IMG_4.jpg") and a "Total: N image(s)" line always appeared,
 *   making single-image display unnecessarily verbose.
 *   Fixed: index number removed from each row; "Total" count and "Clear All"
 *   now only appear when 2+ images are in the queue.
 * - Filename display made overflow-safe (ellipsis on long names) so the
 *   Remove button is never pushed off screen.
 *
 * v3.2.2 FIX: CHUNKED TRANSFER DECODING IN generateTTSAudio()
 * - The "wav" response_format introduced in v3.2.1 exposed a latent bug:
 *   the OpenAI TTS endpoint uses HTTP chunked transfer encoding, and the
 *   previous stream read loop wrote the raw bytes directly to the SD file
 *   including the chunk size lines (e.g. "95e\r\n") and CRLF terminators.
 *   These framing bytes were embedded in the WAV file, corrupting it.
 *   The "mp3" format in v3.2.0 avoided this because HTTPClient happened to
 *   handle that response differently; switching to "wav" exposed the raw stream.
 *   Inspecting audio__21_.wav confirmed: first 5 bytes were "95e\r\n" (chunk
 *   size 2398 in hex), then the RIFF header started at offset 5. The audio
 *   data itself was valid 24kHz mono 16-bit PCM — only the framing was wrong.
 * - Fix: replace the simple stream.readBytes() loop with a manual chunked
 *   transfer decoder that reads and discards each hex chunk-size line and its
 *   trailing CRLF, then reads exactly that many payload bytes into the file,
 *   then reads and discards the trailing CRLF after each chunk's payload.
 *   Terminates on a zero-length chunk (the chunked EOF marker).
 *
 * v3.2.1 PATCH:
 * - SECURITY: Removed real credentials from DEFAULT_* constants.
 *   WiFi SSID, password, and OpenAI API key are now placeholder strings only.
 *   Real credentials must be entered via the /settings page after first flash,
 *   or replaced in this block before compiling. Never commit real keys to source.
 * - FIX: TTS response_format changed from "mp3" to "wav". The WAV format is
 *   natively playable in the browser <audio> element and is the correct container
 *   for speaker playback through the MAX98357A. All related filenames (.mp3 -> .wav),
 *   the handleTTSStream content-type (audio/mpeg -> audio/wav), and the analysis
 *   package copy destination (audio.mp3 -> audio.wav) updated consistently.
 * - FIX: API key settings page — added maxlength=220 and live character counter
 *   to the key input field. Added stored key health banner showing key length and
 *   first 7 / last 4 characters so users can verify the correct key is saved.
 *   Key validation threshold updated to accept modern 164-char sk-proj-* keys
 *   (previous check only caught keys under 10 chars or starting with "sk-YOUR").
 *   Key is sanitized (whitespace/control chars stripped) before saving to NVS.
 * - FIX: handleSpeakerPlayback() — PSRAM fast-path buffering added. The previous
 *   implementation read directly from SD in 256-byte chunks with yield() between
 *   each, causing DMA starvation and audible noise on longer recordings. The full
 *   PCM is now loaded into PSRAM before I2S begins (falls back to SD streaming if
 *   PSRAM is insufficient).
 * - FIX: handleSpeakerPlayback() — PDM microphone stopped before I2S playback and
 *   restarted after. The PDM clock (GPIO38, ~1MHz) was coupling into the I2S speaker
 *   signal lines (GPIO42/45/46) on the DFR1154 PCB, causing broadband noise during
 *   playback. i2s_mic.end() before play and i2s_mic.begin() after eliminates this.
 * - FIX: handleSpeakerPlayback() — DC-blocking IIR high-pass filter added (cutoff
 *   ~1.6Hz, alpha=0.9999). PDM microphone output contains a DC bias that causes an
 *   audible mechanical thud through the speaker at silence boundaries. The filter
 *   removes DC completely while passing all speech frequencies unchanged.
 *
 * v3.2.0 UPDATE:
 * - Added user-configurable credentials block at top of file
 *   (WIFI_SSID, WIFI_PASSWORD, OPENAI_API_KEY - easy one-stop setup)
 * - Dynamic IP (DHCP) always used - IP printed clearly to Serial Monitor on boot
 * - Serial Monitor banner now shows IP in a prominent, easy-to-read box
 * - Added /settings page on web portal: edit WiFi, password, and API key live
 *   without needing a factory reset; changes saved to NVS and device reboots
 * - Settings button visible in header of main web interface
 *
 * v3.1.4 PATCH:
 * - Fixed TTS replay functionality to actually play audio
 * - Auto-play TTS audio when analysis completes
 * - Load TTS audio into audio player bar for playback controls
 * - Added error handling for TTS playback
 *
 * v3.1.3 PATCH:
 * - Fixed success page not visible before AP closes
 * - Keeps setup page active for 30 seconds after WiFi connection
 * - User can now see IP address on success page before it closes
 * - Added countdown timer showing remaining time
 * 
 * v3.1.2 PATCH:
 * - Real-time IP address display on setup/configuration page
 * - Shows current IP while user is still on the page (before submission)
 * - Status polling every 5 seconds
 * - Fixes issue where user couldn't see IP until after closing the page
 * 
 * v3.1.1 PATCH:
 * - Enhanced IP address display on configuration success page
 * - Added copy-to-clipboard button for IP address
 * - Improved Serial Monitor output with clear IP address display
 * - Extended page display time (5 seconds) for user to note IP
 * - Added comprehensive network information on success page
 * 
 * NEW IN v3.1.0:
 * - Static IP configuration support in setup wizard
 * - IP assignment status indicator (DHCP/Static)
 * - Gateway, subnet mask, and DNS configuration
 * - Enhanced network configuration UI
 * 
 * v3.0.0 SECURE EDITION
 * 
 * EPS32S3 DEV Module
 * Tools Tab in IDE
 * USB CDC On Boot- Enabled
 * Flash Size - 16MB(128Mb)
 * Partition Scheme - 16M Flash (3MB APP/9.9MB FATFS)
 * PSRAM - OPI PSRAM
 *
 * v2.0.28 - MULTI-IMAGE VISION ANALYSIS
 * ======================================
 * Added support for analyzing multiple images (up to 5) in a single OpenAI request
 * 
 * New Features:
 * - Export queue now supports multiple images (1-5 images per analysis)
 * - Sequential base64 encoding for memory efficiency
 * - Updated UI with image count display and individual remove buttons
 * - Enhanced prompts for multi-image context
 * - Analysis packages now include all analyzed images
 * - Increased max_tokens to 1000 for detailed multi-image responses
 * 
 * Previous features maintained:
 * - Audio playback bar at top right
 * - Resource-based file selection with export queue
 * - Image/Audio file lists with Save to PC and Delete controls
 * - Fixed speaker playback speed by converting mono to stereo
 * - Real-time progress tracking displayed in web interface
 * - Auto-start video streaming on page load
 * - Visual streaming indicator with "LIVE" badge
 * - Audio player always visible
 * - Automatic analysis package saving to /analysis/[timestamp]/
 * - Export Package button downloads all analysis files to PC
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

// NEW v3.0: Security and reliability headers
#include "credentials_manager.h"
#include "ssl_validation.h"
#include "error_handling.h"

#define FIRMWARE_VERSION "3.2.5"
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__
#define SSL_SECURITY_MODE 0
int currentSSLMode = SSL_SECURITY_MODE;

// ============================================================================
// USER CONFIGURATION - Edit these values before uploading
// ============================================================================
// These values are used on FIRST BOOT or after a factory reset.
// Once saved, credentials are stored in NVS (non-volatile storage).
// To update credentials after first boot, use the /settings page in the
// web interface - no need to reflash the firmware.
// To force re-entry, hold the BOOT button for 3 seconds on startup.
// SECURITY: Never replace these placeholders with real credentials in source
// code that may be shared or committed to version control.

#define DEFAULT_WIFI_SSID       "YourSSID"           // Your WiFi network name
#define DEFAULT_WIFI_PASSWORD   "YourPassword"        // Your WiFi password
#define DEFAULT_OPENAI_API_KEY  "sk-..."              // Your OpenAI API key (sk-proj-...)
#define DEFAULT_DEVICE_NAME     "AI-Camera"           // Friendly name for this device

// ============================================================================

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
#define SAMPLE_RATE     16000   // PDM mic recording rate + recorded audio playback rate
#define TTS_SAMPLE_RATE 24000   // OpenAI TTS WAV native output rate (must match for speaker)
#define AMPLIFICATION      15

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

I2SClass i2s_mic;
I2SClass i2s_spk;


WiFiClientSecure* createSecureClient() {
  WiFiClientSecure* client = new WiFiClientSecure();
  
  switch (currentSSLMode) {
    case 0:
      client->setInsecure();
      client->setTimeout(30);
      break;
    case 1:
      delete client;
      client = SecureOpenAIClient_Pinned::createClient();
      break;
    case 2:
      delete client;
      client = SecureOpenAIClient_RootCA::createClient();
      break;
    default:
      client->setInsecure();
      client->setTimeout(30);
      currentSSLMode = 0;
  }
  return client;
}

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
  Serial.println("\n=== Whisper API (with retry) ===");
  
  RetryConfig config;
  config.maxRetries = 3;
  config.initialDelayMs = 2000;
  config.backoffMultiplier = 2.0;
  config.maxDelayMs = 16000;
  
  String result = RetryableAPIClient::transcribeWithRetry(
    audioFilePath,
    CredentialsManager::getOpenAIKey().c_str(),
    config
  );
  
  return result;
}

String analyzeImageWithGPT4Vision(String imageFilePaths, String audioTranscription) {
  Serial.println("\n=== GPT-4 Vision (Multi-Image) ===");
  
  // Parse comma-separated image paths
  String imagePaths[5];
  int imageCount = 0;
  int startPos = 0;
  int commaPos = imageFilePaths.indexOf(',');
  
  while (commaPos != -1 && imageCount < 5) {
    imagePaths[imageCount] = imageFilePaths.substring(startPos, commaPos);
    imagePaths[imageCount].trim();
    imageCount++;
    startPos = commaPos + 1;
    commaPos = imageFilePaths.indexOf(',', startPos);
  }
  // Get last path (or only path if no commas)
  if (startPos < imageFilePaths.length() && imageCount < 5) {
    imagePaths[imageCount] = imageFilePaths.substring(startPos);
    imagePaths[imageCount].trim();
    imageCount++;
  }
  
  Serial.printf("Analyzing %d image(s)\n", imageCount);
  Serial.printf("Free heap before processing: %d bytes\n", ESP.getFreeHeap());
  
  // Setup connection
  WiFiClientSecure* client = createSecureClient();
  HTTPClient https;
  
  if (!https.begin(*client, OPENAI_CHAT_URL)) {
    Serial.println("ERROR: HTTPS begin failed");
    return "Error: Connection setup failed";
  }
  
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + CredentialsManager::getOpenAIKey());
  https.setTimeout(90000);  // Increased timeout for multi-image
  
  // Build JSON manually for better memory control
  String requestBody = "{\"model\":\"gpt-4o\",\"max_tokens\":1000,\"messages\":[{\"role\":\"user\",\"content\":[";
  
  // Add text prompt
  requestBody += "{\"type\":\"text\",\"text\":\"";
  if (imageCount > 1) {
    requestBody += "Analyze these " + String(imageCount) + " images. ";
  } else {
    requestBody += "Analyze this image. ";
  }
  if (audioTranscription.length() > 0 && !audioTranscription.startsWith("Error")) {
    requestBody += "Audio: \\\"" + audioTranscription + "\\\". Consider both.";
  }
  requestBody += "\"},";
  
  // Process each image sequentially
  for (int i = 0; i < imageCount; i++) {
    Serial.printf("\nProcessing image %d/%d: %s\n", i+1, imageCount, imagePaths[i].c_str());
    Serial.printf("Free heap before image %d: %d bytes\n", i+1, ESP.getFreeHeap());
    
    // Check memory before each image
    if (ESP.getFreeHeap() < 40000) {
      Serial.println("ERROR: Insufficient memory for next image");
      https.end();
      return "Error: Insufficient memory for image " + String(i+1);
    }
    
    String imageBase64 = base64EncodeFileOptimized(imagePaths[i]);
    if (imageBase64.length() == 0) {
      Serial.println("ERROR: Base64 encoding failed for image " + String(i+1));
      https.end();
      return "Error: Base64 encoding failed for image " + String(i+1);
    }
    
    Serial.printf("Image %d base64 length: %d chars\n", i+1, imageBase64.length());
    
    // Add image to JSON
    requestBody += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
    requestBody += imageBase64;
    requestBody += "\"}}";
    
    if (i < imageCount - 1) {
      requestBody += ",";
    }
    
    // Free base64 immediately
    imageBase64 = "";
    yield();  // Allow other operations
    server.handleClient();  // Allow progress updates
    
    Serial.printf("Free heap after image %d: %d bytes\n", i+1, ESP.getFreeHeap());
  }
  
  requestBody += "]}]}";
  
  Serial.printf("\nTotal request size: %d bytes\n", requestBody.length());
  Serial.printf("Free heap before POST: %d bytes\n", ESP.getFreeHeap());
  Serial.println("Sending POST to GPT-4 Vision...");
  
  int httpCode = https.POST(requestBody);
  requestBody = ""; // Free immediately
  
  Serial.printf("HTTP Response Code: %d\n", httpCode);
  
  String analysis = "";
  if (httpCode == 200) {
    String response = https.getString();
    Serial.printf("Response length: %d bytes\n", response.length());
    
    DynamicJsonDocument responseDoc(6144);  // Increased for longer multi-image responses
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
  WiFiClientSecure* client = createSecureClient();
  client->setTimeout(60);
  HTTPClient https;
  https.begin(*client, OPENAI_TTS_URL);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + CredentialsManager::getOpenAIKey());
  https.setTimeout(60000);
  https.setReuse(false);
  DynamicJsonDocument doc(4096);
  doc["model"] = "tts-1";
  doc["input"] = text;
  doc["voice"] = "nova";           // nova: warmer voice, better for small speaker
  doc["response_format"] = "wav";  // wav: browser-playable and speaker-compatible
  String requestBody;
  serializeJson(doc, requestBody);
  int httpCode = https.POST(requestBody);
  String ttsFilename = "";
  if (httpCode == 200) {
    WiFiClient* stream = https.getStreamPtr();
    ttsCount++;
    ttsFilename = "TTS_" + analysisID + ".wav";
    File ttsFile = SD_MMC.open("/tts/" + ttsFilename, FILE_WRITE);
    if (!ttsFile) { https.end(); ttsInProgress = false; return ""; }

    // Manual chunked transfer decoder (v3.2.2)
    // The OpenAI TTS endpoint uses HTTP chunked transfer encoding.
    // Raw stream bytes include hex chunk-size lines and CRLF framing which
    // must be stripped — writing them directly corrupts the WAV file.
    // Protocol per chunk:  "<hex-size>\r\n" + <payload bytes> + "\r\n"
    // Terminated by:       "0\r\n\r\n"
    uint8_t buffer[512];
    size_t totalBytes = 0;
    bool done = false;
    unsigned long lastDataMs = millis();

    while (!done && https.connected() && (millis() - lastDataMs < 15000)) {
      // Read chunk size line (hex digits followed by \r\n)
      String sizeLine = "";
      unsigned long lineStart = millis();
      while (millis() - lineStart < 2000) {
        if (stream->available()) {
          char c = stream->read();
          if (c == '\n') break;        // end of size line
          if (c != '\r') sizeLine += c; // accumulate hex digits, skip \r
        } else {
          yield();
        }
      }

      if (sizeLine.length() == 0) { yield(); continue; }

      size_t chunkLen = (size_t)strtoul(sizeLine.c_str(), nullptr, 16);
      if (chunkLen == 0) { done = true; break; }  // zero chunk = end of stream

      lastDataMs = millis();

      // Read exactly chunkLen payload bytes
      size_t remaining = chunkLen;
      while (remaining > 0 && https.connected() && (millis() - lastDataMs < 10000)) {
        size_t toRead = min(remaining, sizeof(buffer));
        int len = stream->readBytes(buffer, toRead);
        if (len > 0) {
          ttsFile.write(buffer, len);
          totalBytes += len;
          remaining  -= len;
          lastDataMs  = millis();
          if (totalBytes % 10240 == 0) Serial.printf("TTS: %dKB\n", totalBytes / 1024);
        } else {
          yield();
        }
      }

      // Consume trailing CRLF after payload
      while (stream->available() < 2 && millis() - lastDataMs < 1000) yield();
      if (stream->available() >= 2) { stream->read(); stream->read(); }

      yield();
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
  s->set_vflip(s, 0);
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

<div style="background:#667eea;color:white;padding:10px 20px;display:flex;align-items:center;justify-content:space-between;margin:-20px -20px 20px -20px;">
  <div style="font-weight:700;font-size:16px;">ESP32-S3 AI Camera v3.2.5</div>
  <div style="display:flex;align-items:center;gap:16px;">
    <span id="headerIP" style="font-family:monospace;font-size:13px;background:rgba(255,255,255,0.2);padding:4px 10px;border-radius:6px;"></span>
    <a href="/settings" style="color:white;background:rgba(255,255,255,0.25);padding:6px 14px;border-radius:8px;text-decoration:none;font-size:13px;font-weight:600;border:1px solid rgba(255,255,255,0.4);">&#9881; Settings</a>
  </div>
</div>

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
      <button id="ttsSpeakerBtn" class="btn-playback" onclick="playTTSOnSpeaker()" disabled style="background:#8e44ad;" title="Play last TTS response through speaker">TTS Spkr</button>
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
let selectedImagesForExport = [];  // Changed from single to array
let selectedAudioForExport = "";
let lastAnalysisResponse = "";
let lastTTSAudioFile = "";
let lastAnalysisID = "";
let isRecording = false;
const MAX_IMAGES_FOR_EXPORT = 5;  // Limit for memory safety

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
        selectedImageFile = data.filename;  // Auto-select the captured image
        loadImageList();
      } else {
        showStatus('Capture failed', true);
      }
    })
    .catch(err => showStatus('Capture error', true));
}

function reviewLastImage() {
  if (!selectedImageFile) {
    showStatus('No image selected to review', true);
    return;
  }
  
  stopStreamRefresh();
  streamActive = false;
  
  const indicator = document.getElementById('streamingIndicator');
  const streamBtn = document.querySelector('.btn-stream');
  indicator.classList.remove('active');
  streamBtn.classList.remove('active');
  streamBtn.textContent = 'Video Stream';
  
  document.getElementById('videoStream').src = '/image?file=' + encodeURIComponent(selectedImageFile);
  showStatus('Reviewing: ' + selectedImageFile);
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
  
  // Check if already in export list
  if (selectedImagesForExport.includes(selectedImageFile)) {
    showStatus('Image already in export queue', true);
    return;
  }
  
  // Check max limit
  if (selectedImagesForExport.length >= MAX_IMAGES_FOR_EXPORT) {
    showStatus(`Maximum ${MAX_IMAGES_FOR_EXPORT} images allowed`, true);
    return;
  }
  
  selectedImagesForExport.push(selectedImageFile);
  updateExportImageDisplay();
  showStatus('Added to export queue: ' + selectedImageFile);
}

function removeImageFromExport(filename) {
  const index = selectedImagesForExport.indexOf(filename);
  if (index > -1) {
    selectedImagesForExport.splice(index, 1);
    updateExportImageDisplay();
    showStatus('Removed from export: ' + filename);
  }
}

function clearAllExportImages() {
  selectedImagesForExport = [];
  updateExportImageDisplay();
  showStatus('Cleared all images from export');
}

function updateExportImageDisplay() {
  const display = document.getElementById('exportedImageDisplay');
  
  if (selectedImagesForExport.length === 0) {
    display.innerHTML = '<span style="color:#999; font-size:13px">None selected</span>';
  } else {
    let html = '<div style="display:flex; flex-direction:column; gap:5px;">';
    selectedImagesForExport.forEach((filename) => {
      html += `<div style="display:flex; align-items:center; justify-content:space-between; padding:5px; background:#f0f0f0; border-radius:4px;">`;
      html += `<span style="font-size:13px; color:#333; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; max-width:70%;">${filename}</span>`;
      html += `<button onclick="removeImageFromExport('${filename}')" style="background:#e74c3c; color:white; border:none; padding:3px 8px; border-radius:3px; cursor:pointer; font-size:11px; flex-shrink:0; margin-left:6px;">Remove</button>`;
      html += `</div>`;
    });
    html += '</div>';
    if (selectedImagesForExport.length > 1) {
      html += `<div style="margin-top:6px; font-size:12px; color:#666;">Total: ${selectedImagesForExport.length} image(s)</div>`;
      html += `<button onclick="clearAllExportImages()" style="margin-top:5px; background:#e67e22; color:white; border:none; padding:5px 10px; border-radius:4px; cursor:pointer; font-size:12px; width:100%;">Clear All</button>`;
    }
    display.innerHTML = html;
  }
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
  if (selectedImagesForExport.length === 0 || !selectedAudioForExport) {
    showStatus('Select at least one image and audio for export first', true);
    return;
  }
  
  lastAnalysisID = Date.now().toString();
  stopStreamRefresh();
  
  const imageCount = selectedImagesForExport.length;
  showStatus(`Starting OpenAI analysis with ${imageCount} image(s)...`);
  document.getElementById('messageDisplay').textContent = 'Initializing analysis...';
  
  // Send request to start analysis
  fetch('/openai/analyze', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      images: selectedImagesForExport,  // Send array instead of single image
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
                  
                  // Enable TTS speaker button now that a TTS file is available
                  const ttsSpeakerBtn = document.getElementById('ttsSpeakerBtn');
                  if (ttsSpeakerBtn) ttsSpeakerBtn.disabled = false;
                  
                  // NEW v3.1.4: Auto-play TTS and load into audio player
                  if (progress.ttsFile && progress.ttsFile !== 'none' && progress.ttsFile !== '') {
                    console.log('Loading TTS audio:', progress.ttsFile);
                    const audioPlayer = document.getElementById('audioPlayer');
                    audioPlayer.src = '/tts?file=' + encodeURIComponent(progress.ttsFile);
                    audioPlayer.style.display = 'block';
                    audioPlayer.currentTime = 0;
                    
                    // Auto-play TTS response
                    audioPlayer.play()
                      .then(() => {
                        console.log('TTS playback started');
                        showStatus('Playing TTS response');
                      })
                      .catch(err => {
                        console.error('TTS playback error:', err);
                        showStatus('TTS loaded (click play)');
                      });
                  }
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

// Play last TTS response through the physical MAX98357A speaker at correct 24kHz rate.
// Uses /tts/speaker route (handleTTSSpeakerPlayback) which reinitializes i2s_spk
// at TTS_SAMPLE_RATE (24kHz) before playback — fixes half-speed / noisy playback
// that occurred when the 24kHz WAV was fed to a 16kHz-clocked I2S peripheral.
function playTTSOnSpeaker() {
  if (!lastTTSAudioFile || lastTTSAudioFile === 'none') {
    showStatus('No TTS audio available', true);
    return;
  }
  const btn = document.getElementById('ttsSpeakerBtn');
  if (btn) btn.disabled = true;
  showStatus('Playing TTS on speaker...');
  fetch('/tts/speaker?file=' + encodeURIComponent(lastTTSAudioFile),
        { signal: AbortSignal.timeout(120000) })
    .then(res => res.json())
    .then(data => {
      if (btn) btn.disabled = false;
      showStatus(data.success ? 'Speaker playback complete' : 'Speaker playback failed', !data.success);
    })
    .catch(err => {
      if (btn) btn.disabled = false;
      showStatus('Speaker request failed', true);
      console.error('TTS speaker error:', err);
    });
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

// Show IP in header
fetch('/system/status').then(r=>r.json()).then(d=>{
  const el = document.getElementById('headerIP');
  if (el && d.wifiConnected) el.textContent = location.hostname;
}).catch(()=>{});

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
  Serial.println("ESP32-S3 AI Camera v" FIRMWARE_VERSION);
  Serial.println("Built: " FIRMWARE_BUILD_DATE " " FIRMWARE_BUILD_TIME);
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
  
  Serial.println("\n=== Credentials Manager ===");
  if (!CredentialsManager::begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD, 
                                  DEFAULT_OPENAI_API_KEY, DEFAULT_DEVICE_NAME)) {
    Serial.println("ERROR: Credentials manager failed");
    while(1) { delay(1000); }
  }
  
  Serial.println("\n=== WiFi Manager ===");
  if (!WiFiManager::connect(
    CredentialsManager::getWiFiSSID().c_str(),
    CredentialsManager::getWiFiPassword().c_str()
  )) {
    Serial.println("ERROR: WiFi failed");
    while(1) { delay(1000); }
  }
  
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", index_html); });
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/image", HTTP_GET, handleImageView);
  server.on("/image/latest", HTTP_GET, handleLatestImage);
  server.on("/audio/record", HTTP_GET, handleAudioRecord);
  server.on("/audio", HTTP_GET, handleAudioStream);
  server.on("/audio/speaker", HTTP_GET, handleSpeakerPlayback);
  server.on("/tts", HTTP_GET, handleTTSStream);
  server.on("/tts/speaker", HTTP_GET, handleTTSSpeakerPlayback);
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

  server.on("/system/status", HTTP_GET, []() {
    DynamicJsonDocument doc(512);
    doc["wifiConnected"] = WiFiManager::isCurrentlyConnected();
    doc["wifiStatus"] = WiFiManager::getConnectionStatus();
    doc["deviceName"] = CredentialsManager::getDeviceName();
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.on("/settings", HTTP_GET, handleSettingsPage);
  server.on("/settings/save", HTTP_POST, handleSettingsSave);

  server.on("/diagnostics", HTTP_GET, []() {
    String html = "<html><body><pre>";
    html += "ESP32-S3 AI Camera Diagnostics v3.0.0\\n\\n";
    html += "Firmware: " + String(FIRMWARE_VERSION) + "\\n";
    html += "Device: " + CredentialsManager::getDeviceName() + "\\n";
    html += "IP: " + WiFi.localIP().toString() + "\\n";
    html += "Signal: " + String(WiFi.RSSI()) + " dBm\\n";
    html += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\\n";
    html += "</pre></body></html>";
    server.send(200, "text/html", html);
  });
  
  
  server.begin();
  
  // ============================================================
  //  SYSTEM READY - Print IP address prominently to Serial Monitor
  // ============================================================
  String localIP = WiFi.localIP().toString();
  String apiKeyStatus = (CredentialsManager::getOpenAIKey().startsWith("sk-YOUR") || 
                         CredentialsManager::getOpenAIKey().length() < 10) ? "NOT SET!" : "Configured";

  Serial.println("\n");
  Serial.println("****************************************************");
  Serial.println("*                                                  *");
  Serial.println("*           AI CAMERA - SYSTEM READY              *");
  Serial.println("*                                                  *");
  Serial.println("*  Open your browser and go to:                   *");
  Serial.printf( "*  --> http://%-36s*\n", (localIP + "  ").c_str());
  Serial.println("*                                                  *");
  Serial.println("*  To edit WiFi/API settings:                     *");
  Serial.printf( "*  --> http://%-36s*\n", (localIP + "/settings  ").c_str());
  Serial.println("*                                                  *");
  Serial.println("****************************************************");
  Serial.println();
  Serial.printf("Firmware   : v%s\n", FIRMWARE_VERSION);
  Serial.printf("Device     : %s\n", CredentialsManager::getDeviceName().c_str());
  Serial.printf("IP Address : %s\n", localIP.c_str());
  Serial.printf("Signal     : %d dBm\n", WiFi.RSSI());
  Serial.printf("API Key    : %s\n", apiKeyStatus.c_str());
  Serial.printf("Free Heap  : %d bytes\n", ESP.getFreeHeap());
  Serial.println();
  
  // Initialize file counters based on existing files
  initializeFileCounters();
}

void initializeFileCounters() {
  // Count existing images and set imageCount to highest number
  File root = SD_MMC.open("/images");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        // Extract number from IMG_X.jpg format
        if (filename.startsWith("IMG_") && filename.endsWith(".jpg")) {
          int underscorePos = filename.indexOf('_');
          int dotPos = filename.indexOf('.');
          if (underscorePos > 0 && dotPos > underscorePos) {
            String numStr = filename.substring(underscorePos + 1, dotPos);
            int num = numStr.toInt();
            if (num > imageCount) imageCount = num;
          }
        }
      }
      file = root.openNextFile();
    }
    root.close();
  }
  
  // Count existing audio and set audioCount to highest number
  root = SD_MMC.open("/audio");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        // Extract number from REC_X.wav format
        if (filename.startsWith("REC_") && filename.endsWith(".wav")) {
          int underscorePos = filename.indexOf('_');
          int dotPos = filename.indexOf('.');
          if (underscorePos > 0 && dotPos > underscorePos) {
            String numStr = filename.substring(underscorePos + 1, dotPos);
            int num = numStr.toInt();
            if (num > audioCount) audioCount = num;
          }
        }
      }
      file = root.openNextFile();
    }
    root.close();
  }
  
  // Count existing TTS and set ttsCount to highest number
  root = SD_MMC.open("/tts");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        ttsCount++;
      }
      file = root.openNextFile();
    }
    root.close();
  }
  
  Serial.printf("Initialized counters - Images: %d, Audio: %d, TTS: %d\n", imageCount, audioCount, ttsCount);
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
  if (!root) {
    server.send(200, "application/json", "{\"success\":false}");
    return;
  }
  
  String latest = "";
  File file = root.openNextFile();
  
  while (file) {
    if (!file.isDirectory()) {
      latest = String(file.name());
    }
    file = root.openNextFile();
  }
  
  root.close();
  
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

  // Stop PDM mic to eliminate ~1MHz PDM clock coupling into I2S speaker lines.
  // On the compact DFR1154 PCB, GPIO38 (PDM CLK) runs adjacent to GPIO42/45/46.
  // Confirmed fix: eliminated broadband noise during recorded audio playback.
  i2s_mic.end();

  File file = SD_MMC.open("/audio/" + filename, FILE_READ);
  if (!file) {
    server.send(404, "application/json", "{\"success\":false}");
    // Restart mic even on early return
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }

  // Skip WAV header (44 bytes standard)
  size_t fileSize = file.size();
  if (fileSize <= 44) {
    file.close();
    server.send(200, "application/json", "{\"success\":true}");
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }
  file.seek(44);
  size_t pcmSize = fileSize - 44;

  // DC-blocking IIR high-pass filter state (v3.2.1)
  // y[n] = x[n] - x[n-1] + 0.9999 * y[n-1]
  // Cutoff ~1.6Hz at 16kHz — removes DC bias completely, preserves all speech.
  // PDM microphone output has a DC offset that causes audible thuds at silence
  // boundaries when the speaker cone is suddenly pushed off its rest position.
  static const int32_t HP_ALPHA_Q15 = 32764;  // 0.9999 × 32768
  int32_t hp_x_prev = 0;
  int32_t hp_y_prev = 0;

  // PSRAM fast-path: load entire PCM into PSRAM before I2S begins.
  // Eliminates SD read latency (~5-50ms per chunk) during playback, which
  // causes DMA starvation and audible noise on longer recordings.
  size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint8_t* pcmBuf = nullptr;
  bool usingPSRAM = false;

  if (psramFree > pcmSize + 65536) {
    pcmBuf = (uint8_t*)heap_caps_malloc(pcmSize, MALLOC_CAP_SPIRAM);
    if (pcmBuf) {
      size_t bytesRead = file.read(pcmBuf, pcmSize);
      file.close();
      if (bytesRead == pcmSize) {
        usingPSRAM = true;
        Serial.printf("[Audio] PSRAM fast-path: %u bytes\n", pcmSize);
      } else {
        heap_caps_free(pcmBuf);
        pcmBuf = nullptr;
        file = SD_MMC.open("/audio/" + filename, FILE_READ);
        if (file) file.seek(44);
      }
    }
  }

  static uint8_t monoBuffer[4096];
  static uint8_t stereoBuffer[8192];

  if (usingPSRAM) {
    size_t offset = 0;
    uint8_t yieldCount = 0;
    while (offset < pcmSize) {
      size_t chunk = min((size_t)sizeof(monoBuffer), pcmSize - offset);
      memcpy(monoBuffer, pcmBuf + offset, chunk);
      offset += chunk;
      if (chunk & 1) chunk--;
      int16_t* ms = (int16_t*)monoBuffer;
      int16_t* ss = (int16_t*)stereoBuffer;
      size_t n = chunk / 2;
      for (size_t i = 0; i < n; i++) {
        int32_t x = (int32_t)ms[i];
        // DC-blocking high-pass filter
        int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
        hp_x_prev = x;
        hp_y_prev = y;
        int16_t s = (int16_t)constrain(y, -32768, 32767);
        ss[i * 2]     = s;
        ss[i * 2 + 1] = s;
      }
      i2s_spk.write(stereoBuffer, n * 4);
      if (++yieldCount >= 32) { yieldCount = 0; taskYIELD(); }
    }
    heap_caps_free(pcmBuf);

  } else {
    // SD fallback — stream directly when PSRAM insufficient
    Serial.printf("[Audio] SD fallback: %u bytes\n", pcmSize);
    uint8_t chunkCount = 0;
    while (file && file.available()) {
      size_t bytesRead = file.read(monoBuffer, sizeof(monoBuffer));
      if (bytesRead > 1) {
        if (bytesRead & 1) bytesRead--;
        int16_t* ms = (int16_t*)monoBuffer;
        int16_t* ss = (int16_t*)stereoBuffer;
        size_t n = bytesRead / 2;
        for (size_t i = 0; i < n; i++) {
          int32_t x = (int32_t)ms[i];
          int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
          hp_x_prev = x;
          hp_y_prev = y;
          int16_t s = (int16_t)constrain(y, -32768, 32767);
          ss[i * 2]     = s;
          ss[i * 2 + 1] = s;
        }
        i2s_spk.write(stereoBuffer, n * 4);
      }
      if (++chunkCount >= 32) { chunkCount = 0; taskYIELD(); }
    }
    if (file) file.close();
  }

  // Restart PDM mic after playback
  i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (!i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[Audio] WARNING: PDM mic restart failed after playback");
  }
  delay(20);  // PDM settle time

  server.send(200, "application/json", "{\"success\":true}");
}

// handleTTSSpeakerPlayback() — plays a TTS WAV file through the MAX98357A speaker.
// TTS files are saved at 24kHz (OpenAI native rate). i2s_spk must be reinitialized
// at TTS_SAMPLE_RATE before playback and restored to SAMPLE_RATE (16kHz) afterward.
// Route: GET /tts/speaker?file=TTS_xxx.wav
// This is intentionally separate from handleSpeakerPlayback() (/audio/speaker) which
// handles recorded audio (REC files) at 16kHz — do not merge these two functions.
void handleTTSSpeakerPlayback() {
  String filename = server.arg("file");

  // Stop PDM mic — eliminates ~1MHz PDM clock coupling into I2S speaker lines.
  i2s_mic.end();

  File file = SD_MMC.open("/tts/" + filename, FILE_READ);
  if (!file) {
    server.send(404, "application/json", "{\"success\":false,\"error\":\"file not found\"}");
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }

  size_t fileSize = file.size();
  if (fileSize <= 44) {
    file.close();
    server.send(200, "application/json", "{\"success\":true}");
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }
  file.seek(44);
  size_t pcmSize = fileSize - 44;

  // Reinitialize speaker at TTS_SAMPLE_RATE (24kHz).
  // The saved WAV contains 24kHz PCM — playing it through a 16kHz-clocked I2S
  // peripheral produces 67% speed playback with noise and DMA halting.
  // i2s_spk.end() + begin() at 24kHz corrects the clock before any PCM is written.
  i2s_spk.end();
  i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  if (!i2s_spk.begin(I2S_MODE_STD, TTS_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[TTS Speaker] ERROR: i2s_spk reinit at 24kHz failed");
    file.close();
    server.send(500, "application/json", "{\"success\":false,\"error\":\"i2s init failed\"}");
    // Restore speaker and mic to known state
    i2s_spk.end();
    i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
    i2s_spk.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
    i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    delay(20);
    return;
  }
  Serial.printf("[TTS Speaker] Playing %s at 24kHz (%u bytes PCM)\n", filename.c_str(), pcmSize);

  // DC-blocking IIR high-pass filter — removes DC bias, prevents speaker thud.
  static const int32_t HP_ALPHA_Q15 = 32764;  // 0.9999 × 32768
  int32_t hp_x_prev = 0;
  int32_t hp_y_prev = 0;

  // PSRAM fast-path: load entire PCM before I2S begins — eliminates SD latency
  // stalls that cause DMA starvation and audible dropouts during playback.
  size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  uint8_t* pcmBuf  = nullptr;
  bool usingPSRAM  = false;

  if (psramFree > pcmSize + 65536) {
    pcmBuf = (uint8_t*)heap_caps_malloc(pcmSize, MALLOC_CAP_SPIRAM);
    if (pcmBuf) {
      size_t bytesRead = file.read(pcmBuf, pcmSize);
      file.close();
      if (bytesRead == pcmSize) {
        usingPSRAM = true;
        Serial.printf("[TTS Speaker] PSRAM fast-path: %u bytes\n", pcmSize);
      } else {
        heap_caps_free(pcmBuf);
        pcmBuf = nullptr;
        file = SD_MMC.open("/tts/" + filename, FILE_READ);
        if (file) file.seek(44);
      }
    }
  }

  static uint8_t monoBuffer[4096];
  static uint8_t stereoBuffer[8192];

  if (usingPSRAM) {
    size_t offset    = 0;
    uint8_t yieldCnt = 0;
    while (offset < pcmSize) {
      size_t chunk = min((size_t)sizeof(monoBuffer), pcmSize - offset);
      memcpy(monoBuffer, pcmBuf + offset, chunk);
      offset += chunk;
      if (chunk & 1) chunk--;
      int16_t* ms = (int16_t*)monoBuffer;
      int16_t* ss = (int16_t*)stereoBuffer;
      size_t n = chunk / 2;
      for (size_t i = 0; i < n; i++) {
        int32_t x = (int32_t)ms[i];
        int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
        hp_x_prev = x;
        hp_y_prev = y;
        int16_t s = (int16_t)constrain(y, -32768, 32767);
        ss[i * 2]     = s;
        ss[i * 2 + 1] = s;
      }
      i2s_spk.write(stereoBuffer, n * 4);
      if (++yieldCnt >= 32) { yieldCnt = 0; taskYIELD(); }
    }
    heap_caps_free(pcmBuf);

  } else {
    // SD fallback — stream directly when PSRAM is insufficient
    Serial.printf("[TTS Speaker] SD fallback: %u bytes\n", pcmSize);
    uint8_t chunkCnt = 0;
    while (file && file.available()) {
      size_t bytesRead = file.read(monoBuffer, sizeof(monoBuffer));
      if (bytesRead > 1) {
        if (bytesRead & 1) bytesRead--;
        int16_t* ms = (int16_t*)monoBuffer;
        int16_t* ss = (int16_t*)stereoBuffer;
        size_t n = bytesRead / 2;
        for (size_t i = 0; i < n; i++) {
          int32_t x = (int32_t)ms[i];
          int32_t y = x - hp_x_prev + ((HP_ALPHA_Q15 * hp_y_prev) >> 15);
          hp_x_prev = x;
          hp_y_prev = y;
          int16_t s = (int16_t)constrain(y, -32768, 32767);
          ss[i * 2]     = s;
          ss[i * 2 + 1] = s;
        }
        i2s_spk.write(stereoBuffer, n * 4);
      }
      if (++chunkCnt >= 32) { chunkCnt = 0; taskYIELD(); }
    }
    if (file) file.close();
  }

  // Restore speaker to 16kHz for recorded audio playback compatibility.
  i2s_spk.end();
  i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  if (!i2s_spk.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[TTS Speaker] WARNING: i2s_spk restore to 16kHz failed");
  }

  // Restart PDM mic after playback.
  i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (!i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("[TTS Speaker] WARNING: PDM mic restart failed after TTS playback");
  }
  delay(20);  // PDM settle time

  server.send(200, "application/json", "{\"success\":true}");
}

void handleTTSStream() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/tts/" + filename, FILE_READ);
  if (file) {
    server.streamFile(file, "audio/wav");  // wav format (v3.2.1: was audio/mpeg)
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
  analysisProgressDetail = "Analyzing images with GPT-4 (may take longer for multiple images)...";
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

void saveAnalysisPackage(String analysisID, String transcription, String analysis, String imagePaths, String audioPath, String ttsFile) {
  Serial.println("\n=== Saving Analysis Package ===");
  
  // Create analysis directory
  String analysisDir = "/analysis/" + analysisID;
  SD_MMC.mkdir("/analysis");
  SD_MMC.mkdir(analysisDir.c_str());
  
  Serial.println("Directory: " + analysisDir);
  
  // Parse image paths (comma-separated)
  String imagePathArray[5];
  int imageCount = 0;
  int startPos = 0;
  int commaPos = imagePaths.indexOf(',');
  
  while (commaPos != -1 && imageCount < 5) {
    imagePathArray[imageCount] = imagePaths.substring(startPos, commaPos);
    imagePathArray[imageCount].trim();
    imageCount++;
    startPos = commaPos + 1;
    commaPos = imagePaths.indexOf(',', startPos);
  }
  if (startPos < imagePaths.length() && imageCount < 5) {
    imagePathArray[imageCount] = imagePaths.substring(startPos);
    imagePathArray[imageCount].trim();
    imageCount++;
  }
  
  Serial.printf("Saving analysis with %d image(s)\n", imageCount);
  
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
    combinedFile.println("Images (" + String(imageCount) + "): " + imagePaths);
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
    metaFile.println("Image Count: " + String(imageCount));
    metaFile.println("Source Images: " + imagePaths);
    metaFile.println("Source Audio: " + audioPath);
    metaFile.println("TTS File: /tts/" + ttsFile);
    metaFile.println("Prompt Length: " + String(transcription.length()) + " chars");
    metaFile.println("Response Length: " + String(analysis.length()) + " chars");
    metaFile.close();
    Serial.println("Saved: metadata.txt");
  }
  
  // 5. Copy all analyzed images to analysis folder
  for (int i = 0; i < imageCount; i++) {
    File srcImg = SD_MMC.open(imagePathArray[i], FILE_READ);
    String dstFilename = analysisDir + "/image_" + String(i + 1) + ".jpg";
    File dstImg = SD_MMC.open(dstFilename, FILE_WRITE);
    
    if (srcImg && dstImg) {
      uint8_t buffer[512];
      while (srcImg.available()) {
        size_t bytesRead = srcImg.read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
          dstImg.write(buffer, bytesRead);
        }
      }
      srcImg.close();
      dstImg.close();
      Serial.println("Saved: image_" + String(i + 1) + ".jpg (copied from " + imagePathArray[i] + ")");
    }
    yield();
  }
  
  // 6. Copy TTS file to analysis folder (if it exists)
  if (ttsFile != "none" && ttsFile.length() > 0) {
    File srcTTS = SD_MMC.open("/tts/" + ttsFile, FILE_READ);
    File dstTTS = SD_MMC.open(analysisDir + "/audio.wav", FILE_WRITE);
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
      Serial.println("Saved: audio.wav (copied from /tts/" + ttsFile + ")");
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
  
  DynamicJsonDocument reqDoc(2048);  // Increased for image array
  deserializeJson(reqDoc, server.arg("plain"));
  
  // Get image array from request
  JsonArray imagesArray = reqDoc["images"].as<JsonArray>();
  String audioFile = reqDoc["audio"].as<String>();
  currentAnalysisID = reqDoc["id"].as<String>();
  
  // Build image paths array (max 5)
  currentImagePath = "";  // Will store comma-separated list for metadata
  int imageCount = 0;
  for (JsonVariant imageVar : imagesArray) {
    if (imageCount >= 5) break;  // Safety limit
    String imageFile = imageVar.as<String>();
    if (imageCount > 0) currentImagePath += ",";
    currentImagePath += "/images/" + imageFile;
    imageCount++;
  }
  
  currentAudioPath = "/audio/" + audioFile;
  
  Serial.printf("Multi-image analysis: %d images\n", imageCount);
  
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

// ============================================================================
// SETTINGS PAGE - Edit credentials via web interface
// ============================================================================

void handleSettingsPage() {
  String localIP = WiFi.localIP().toString();
  String html = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>AI Camera - Settings</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;
  background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);
  min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}
.container{background:white;border-radius:20px;padding:40px;max-width:520px;
  width:100%;box-shadow:0 20px 60px rgba(0,0,0,0.3);}
h1{color:#667eea;margin-bottom:6px;font-size:26px;text-align:center;}
.subtitle{text-align:center;color:#888;margin-bottom:30px;font-size:13px;}
.back-link{display:inline-block;margin-bottom:20px;color:#667eea;text-decoration:none;
  font-size:14px;font-weight:600;}
.back-link:hover{text-decoration:underline;}
.section-title{font-size:12px;font-weight:700;color:#667eea;text-transform:uppercase;
  letter-spacing:1px;margin:24px 0 16px;padding-bottom:6px;border-bottom:2px solid #e8e8f0;}
.form-group{margin-bottom:18px;}
label{display:block;margin-bottom:6px;color:#333;font-weight:600;font-size:14px;}
input{width:100%;padding:12px 14px;border:2px solid #e0e0e0;border-radius:10px;
  font-size:14px;transition:border-color 0.2s;}
input:focus{outline:none;border-color:#667eea;}
.help{color:#999;font-size:12px;margin-top:4px;}
.ip-badge{background:#f0f4ff;border:2px solid #667eea;border-radius:10px;
  padding:14px 18px;margin-bottom:24px;text-align:center;}
.ip-badge .label{font-size:11px;color:#888;text-transform:uppercase;letter-spacing:1px;}
.ip-badge .ip{font-size:22px;font-weight:700;color:#667eea;font-family:monospace;margin-top:4px;}
.btn{width:100%;padding:14px;background:linear-gradient(135deg,#667eea,#764ba2);
  color:white;border:none;border-radius:10px;font-size:16px;font-weight:700;
  cursor:pointer;margin-top:10px;transition:opacity 0.2s;}
.btn:hover{opacity:0.9;}
.warning{background:#fff8e1;border:2px solid #ffc107;border-radius:10px;
  padding:12px 16px;margin-top:20px;font-size:13px;color:#795548;}
.warning strong{color:#e65100;}
.toggle-pw{float:right;font-size:12px;color:#667eea;cursor:pointer;margin-top:-22px;
  font-weight:600;user-select:none;}
</style>
</head>
<body>
<div class="container">
  <a class="back-link" href="/">&larr; Back to Camera</a>
  <h1>Camera Settings</h1>
  <p class="subtitle">Update WiFi and API credentials</p>

  <div class="ip-badge">
    <div class="label">Current IP Address</div>
    <div class="ip">)rawliteral";
  html += localIP;
  html += R"rawliteral(</div>
  </div>

  <form method="POST" action="/settings/save" id="settingsForm">

    <div class="section-title">WiFi Network</div>

    <div class="form-group">
      <label>WiFi Network (SSID)</label>
      <input type="text" name="ssid" placeholder="Your network name" value=")rawliteral";
  html += CredentialsManager::getWiFiSSID();
  html += R"rawliteral(" required>
    </div>

    <div class="form-group">
      <label>WiFi Password</label>
      <input type="password" name="password" id="wifiPw" placeholder="Leave blank to keep current">
      <span class="toggle-pw" onclick="togglePw('wifiPw',this)">Show</span>
      <div class="help">Leave blank to keep your existing password</div>
    </div>

    <div class="section-title">OpenAI API Key</div>

    <div class="form-group">
      <label>API Key</label>
      <input type="password" name="apikey" id="apiPw"
             maxlength="220"
             placeholder="sk-... (leave blank to keep current)"
             oninput="document.getElementById('apikeyCount').textContent=this.value.length+' / 220 chars'">
      <span class="toggle-pw" onclick="togglePw('apiPw',this)">Show</span>
      <div class="help" id="apikeyCount">0 / 220 chars</div>
      <div class="help">Get your key at platform.openai.com &mdash; leave blank to keep existing</div>
    </div>
    <div id="keyHealthBanner" style="margin-bottom:12px"></div>
    <script>
    (function(){
      var k=')rawliteral";
  html += CredentialsManager::getOpenAIKey();
  html += R"rawliteral(';
      var n=k.length, pre=k.substring(0,7), suf=k.substring(n-4);
      var el=document.getElementById('keyHealthBanner');
      if(n<20){
        el.innerHTML='<div style="background:#fff3cd;border:1px solid #ffc107;border-radius:8px;padding:10px;font-size:13px">&#9888; No key stored — enter your OpenAI API key above.</div>';
      } else if(n>220){
        el.innerHTML='<div style="background:#f8d7da;border:1px solid #dc3545;border-radius:8px;padding:10px;font-size:13px">&#10007; Stored key is unusually long ('+n+' chars). Re-enter your key.</div>';
      } else {
        el.innerHTML='<div style="background:#d4edda;border:1px solid #28a745;border-radius:8px;padding:10px;font-size:13px">&#10003; Key stored: '+n+' chars &mdash; <code>'+pre+'...'+suf+'</code> (leave blank to keep)</div>';
      }
    })();
    </script>

    <div class="section-title">Device</div>

    <div class="form-group">
      <label>Device Name</label>
      <input type="text" name="devicename" placeholder="AI-Camera" value=")rawliteral";
  html += CredentialsManager::getDeviceName();
  html += R"rawliteral(">
    </div>

    <button type="submit" class="btn">Save &amp; Restart Camera</button>

    <div class="warning">
      <strong>Note:</strong> The camera will restart after saving. 
      Reconnect to your WiFi and navigate back to 
      <strong>http://)rawliteral";
  html += localIP;
  html += R"rawliteral(</strong> (IP may change if using DHCP &mdash; check Serial Monitor).
    </div>
  </form>
</div>
<script>
function togglePw(id, el) {
  const f = document.getElementById(id);
  if (f.type === 'password') { f.type='text'; el.textContent='Hide'; }
  else { f.type='password'; el.textContent='Show'; }
}
</script>
</body>
</html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleSettingsSave() {
  String ssid       = server.arg("ssid");
  String password   = server.arg("password");
  String apiKey     = server.arg("apikey");
  String deviceName = server.arg("devicename");

  // Keep existing values if fields are blank
  if (ssid.length() == 0)        ssid       = CredentialsManager::getWiFiSSID();
  if (password.length() == 0)    password   = CredentialsManager::getWiFiPassword();
  if (apiKey.length() == 0)      apiKey     = CredentialsManager::getOpenAIKey();
  if (deviceName.length() == 0)  deviceName = CredentialsManager::getDeviceName();

  // Sanitize API key: strip whitespace and control characters that corrupt the key
  // (copy-paste from some browsers/tools adds trailing newlines or invisible chars)
  apiKey.trim();
  String cleanKey = "";
  cleanKey.reserve(apiKey.length());
  for (int i = 0; i < (int)apiKey.length(); i++) {
    char c = apiKey[i];
    if (c >= 0x20 && c < 0x7F) cleanKey += c;
  }
  apiKey = cleanKey;
  Serial.printf("[Settings] API key: %d chars after sanitization\n", apiKey.length());

  // Save to NVS (no static IP - always DHCP in v3.2.0)
  CredentialsManager::saveCredentials(ssid, password, apiKey, deviceName,
                                      false, "", "", "255.255.255.0", "8.8.8.8");

  Serial.println("\n=== Settings Updated via Web Interface ===");
  Serial.println("SSID: " + ssid);
  Serial.println("Device: " + deviceName);
  Serial.println("API Key: " + String(apiKey.startsWith("sk-") ? "Configured" : "Check format"));
  Serial.println("Restarting in 3 seconds...");

  String html = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);
  min-height:100vh;display:flex;align-items:center;justify-content:center;margin:0;}
.box{background:white;border-radius:20px;padding:40px;max-width:420px;text-align:center;
  box-shadow:0 20px 60px rgba(0,0,0,0.3);}
.icon{font-size:56px;margin-bottom:16px;}
h2{color:#667eea;margin-bottom:12px;}
p{color:#555;line-height:1.6;margin-bottom:8px;}
.note{background:#f0f4ff;border-radius:10px;padding:14px;margin-top:20px;
  font-size:13px;color:#444;}
</style></head>
<body><div class="box">
<div class="icon">&#10003;</div>
<h2>Settings Saved!</h2>
<p>Your credentials have been saved. The camera is restarting now.</p>
<div class="note">
  <strong>Check the Serial Monitor</strong> for the new IP address after restart,
  or try <strong>http://)rawliteral";
  html += server.arg("ssid").length() > 0 ? CredentialsManager::getWiFiSSID() : ssid;
  html += R"rawliteral(</strong> after a few seconds.
</div>
</div></body></html>)rawliteral";

  server.send(200, "text/html", html);
  delay(3000);
  ESP.restart();
}

void loop() {
  server.handleClient();
  WiFiManager::monitor();
  yield();
}
