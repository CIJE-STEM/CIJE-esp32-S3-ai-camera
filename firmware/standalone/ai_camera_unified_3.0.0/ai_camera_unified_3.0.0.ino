#include "time.h"
/*
 * ESP32-S3 AI CAM - Unified Firmware v3.0.0
 *
 * Board Settings (Arduino IDE):
 *   Board: ESP32S3 Dev Module
 *   USB CDC On Boot: Enabled
 *   Flash Size: 16MB (128Mb)
 *   Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)
 *   PSRAM: OPI PSRAM
 *
 * v3.0.0 - UNIFIED FIRMWARE
 * =========================
 * Combines the OpenAI camera interface (v2.0.28) with the WiFi
 * configuration portal (v4 + WPS). No more hardcoded credentials.
 *
 * First boot flow:
 *   1. Device creates "AI-Camera-Setup" WiFi hotspot
 *   2. Connect with phone/laptop, captive portal opens automatically
 *   3. Scan and select your WiFi network
 *   4. Enter your OpenAI API key
 *   5. Device saves config and reboots into camera mode
 *
 * Factory reset: Hold BOOT button (GPIO 0) for 3 seconds
 *
 * Based on:
 *   - open_ai_2_0_28.ino (camera + OpenAI)
 *   - wifi_app_4_wps (WiFi configuration portal)
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
#include <DNSServer.h>
#include <Preferences.h>
#include "esp_wps.h"
#include <ESPmDNS.h>

// ============================================================
// mDNS Configuration
// ============================================================
#define MDNS_NAME "ai-camera"  // Access at http://ai-camera.local

// ============================================================
// OpenAI API URLs
// ============================================================
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

// ============================================================
// Hardware Pin Definitions (ESP32-S3 AI CAM - DFRobot DFR1154)
// ============================================================
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

// ============================================================
// WiFi Manager Configuration
// ============================================================
#define RESET_BUTTON_PIN 0        // GPIO 0 - BOOT button
#define RESET_HOLD_TIME 3000      // 3 seconds to factory reset
#define AP_SSID "AI-Camera-Setup"
#define AP_PASSWORD ""            // Open network for easy setup
#define DNS_PORT 53
#define WEB_PORT 80
#define CONFIG_TIMEOUT 300000     // 5 minutes in AP mode before restart
#define WIFI_CONNECT_TIMEOUT 20000
#define WIFI_RECONNECT_INTERVAL 30000
#define MAX_RECONNECT_ATTEMPTS 5
#define WPS_TIMEOUT 120000

// ============================================================
// Global Objects
// ============================================================
WebServer server(WEB_PORT);
DNSServer dnsServer;
Preferences preferences;
I2SClass i2s_mic;
I2SClass i2s_spk;

// ============================================================
// Device Configuration (stored in Preferences)
// ============================================================
struct DeviceConfig {
  char ssid[32];
  char password[64];
  char openaiKey[200];
  bool useStaticIP;
  IPAddress staticIP;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;
};

DeviceConfig deviceConfig;

// ============================================================
// WiFi Manager State
// ============================================================
bool configMode = false;
unsigned long configStartTime = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastWiFiCheck = 0;
int reconnectAttempts = 0;
bool shouldReconnect = true;

// Reset button state
unsigned long buttonPressStartTime = 0;
bool buttonPressed = false;
bool resetTriggered = false;

// WPS state
bool wpsRunning = false;
unsigned long wpsStartTime = 0;
unsigned long lastWPSCheck = 0;

// ============================================================
// Camera App State
// ============================================================
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

// ============================================================
// Forward Declarations
// ============================================================
void saveAnalysisPackage(String analysisID, String transcription, String analysis, String imagePaths, String audioPath, String ttsFile);
void startConfigMode();
void startWPS();
void handleWPS();
void setupApplicationServer();
void connectToWiFi();
void attemptReconnect();
void factoryReset();

// ============================================================
// SETUP PAGE HTML (Captive Portal)
// ============================================================
const char SETUP_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AI Camera Setup</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&family=Fira+Code:wght@400;500&display=swap');
        * { margin: 0; padding: 0; box-sizing: border-box; }
        :root {
            --primary: #00d4ff;
            --primary-dark: #0099cc;
            --bg-dark: #0a0e1a;
            --bg-card: #141824;
            --text: #e8eaed;
            --text-dim: #9aa0a6;
            --success: #34d399;
            --warning: #fbbf24;
            --error: #ef4444;
            --border: rgba(255, 255, 255, 0.1);
        }
        body {
            font-family: 'Outfit', sans-serif;
            background: var(--bg-dark);
            color: var(--text);
            min-height: 100vh;
            padding: 20px;
            background-image:
                radial-gradient(circle at 20% 50%, rgba(0, 212, 255, 0.08) 0%, transparent 50%),
                radial-gradient(circle at 80% 80%, rgba(52, 211, 153, 0.06) 0%, transparent 50%);
        }
        .container { max-width: 600px; margin: 0 auto; animation: fadeIn 0.6s ease-out; }
        @keyframes fadeIn { from { opacity: 0; transform: translateY(20px); } to { opacity: 1; transform: translateY(0); } }
        .header { text-align: center; margin-bottom: 40px; padding: 30px 20px; }
        .logo {
            width: 60px; height: 60px; margin: 0 auto 20px;
            background: linear-gradient(135deg, var(--primary), var(--primary-dark));
            border-radius: 16px; display: flex; align-items: center;
            justify-content: center; font-size: 30px;
            animation: pulse 2s ease-in-out infinite;
        }
        @keyframes pulse { 0%, 100% { box-shadow: 0 0 0 0 rgba(0, 212, 255, 0.7); } 50% { box-shadow: 0 0 0 15px rgba(0, 212, 255, 0); } }
        h1 {
            font-size: 2rem; font-weight: 700; margin-bottom: 10px;
            background: linear-gradient(135deg, var(--primary), #34d399);
            -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text;
        }
        .subtitle { color: var(--text-dim); font-size: 1rem; }
        .card {
            background: var(--bg-card); border-radius: 20px; padding: 30px;
            margin-bottom: 20px; border: 1px solid var(--border);
            animation: slideUp 0.6s ease-out; animation-delay: 0.2s; animation-fill-mode: backwards;
        }
        @keyframes slideUp { from { opacity: 0; transform: translateY(30px); } to { opacity: 1; transform: translateY(0); } }
        .section-title {
            font-size: 1.2rem; font-weight: 600; margin-bottom: 20px;
            display: flex; align-items: center; gap: 10px;
        }
        .section-icon { font-size: 1.5rem; }
        .form-group { margin-bottom: 24px; }
        label {
            display: block; margin-bottom: 8px; color: var(--text-dim);
            font-size: 0.9rem; font-weight: 500; text-transform: uppercase; letter-spacing: 0.5px;
        }
        input, select {
            width: 100%; padding: 14px 16px; background: var(--bg-dark);
            border: 2px solid var(--border); border-radius: 12px;
            color: var(--text); font-size: 1rem; font-family: 'Fira Code', monospace;
            transition: all 0.3s ease;
        }
        input:focus, select:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 0 4px rgba(0, 212, 255, 0.1); }
        .network-list { max-height: 300px; overflow-y: auto; margin-bottom: 20px; padding: 10px; background: var(--bg-dark); border-radius: 12px; }
        .network-item {
            padding: 16px; margin-bottom: 8px; background: var(--bg-card);
            border: 2px solid var(--border); border-radius: 10px; cursor: pointer;
            transition: all 0.3s ease; display: flex; justify-content: space-between; align-items: center;
        }
        .network-item:hover { border-color: var(--primary); transform: translateX(5px); }
        .network-item.selected { border-color: var(--primary); background: rgba(0, 212, 255, 0.1); }
        .network-name { font-weight: 600; font-family: 'Fira Code', monospace; }
        .network-strength { display: flex; align-items: center; gap: 8px; color: var(--text-dim); }
        .signal-bars { display: flex; gap: 2px; align-items: flex-end; }
        .signal-bar { width: 4px; background: var(--text-dim); border-radius: 2px; }
        .toggle-group { display: flex; gap: 10px; margin-bottom: 20px; }
        .toggle-btn {
            flex: 1; padding: 14px; background: var(--bg-dark);
            border: 2px solid var(--border); border-radius: 12px;
            color: var(--text); cursor: pointer; transition: all 0.3s ease;
            font-weight: 600; text-align: center;
        }
        .toggle-btn:hover { border-color: var(--primary); }
        .toggle-btn.active { background: var(--primary); border-color: var(--primary); color: var(--bg-dark); }
        .static-ip-fields { display: none; animation: slideDown 0.3s ease-out; }
        .static-ip-fields.show { display: block; }
        @keyframes slideDown { from { opacity: 0; max-height: 0; } to { opacity: 1; max-height: 500px; } }
        .btn {
            width: 100%; padding: 16px;
            background: linear-gradient(135deg, var(--primary), var(--primary-dark));
            border: none; border-radius: 12px; color: white;
            font-size: 1.1rem; font-weight: 700; cursor: pointer;
            transition: all 0.3s ease; text-transform: uppercase; letter-spacing: 1px;
        }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 10px 25px rgba(0, 212, 255, 0.3); }
        .btn:active { transform: translateY(0); }
        .btn-wps { background: linear-gradient(135deg, #8b5cf6, #6366f1); margin-top: 10px; }
        .btn-wps:hover { box-shadow: 0 10px 25px rgba(139, 92, 246, 0.3); }
        .btn-secondary { background: var(--bg-dark); border: 2px solid var(--border); margin-top: 10px; }
        .btn-secondary:hover { border-color: var(--primary); box-shadow: none; }
        .status-message { padding: 16px; border-radius: 12px; margin-bottom: 20px; display: none; animation: slideDown 0.3s ease-out; }
        .status-message.show { display: block; }
        .status-message.success { background: rgba(52, 211, 153, 0.1); border: 2px solid var(--success); color: var(--success); }
        .status-message.error { background: rgba(239, 68, 68, 0.1); border: 2px solid var(--error); color: var(--error); }
        .loading { display: none; text-align: center; padding: 20px; }
        .loading.show { display: block; }
        .spinner {
            width: 40px; height: 40px; margin: 0 auto 10px;
            border: 4px solid var(--border); border-top-color: var(--primary);
            border-radius: 50%; animation: spin 1s linear infinite;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
        .footer { text-align: center; margin-top: 40px; color: var(--text-dim); font-size: 0.9rem; }
        .help-text { font-size: 0.85rem; color: var(--text-dim); margin-top: 8px; }
        .help-text a { color: var(--primary); text-decoration: none; }
        .help-text a:hover { text-decoration: underline; }
        @media (max-width: 640px) { .container { padding: 10px; } .card { padding: 20px; } h1 { font-size: 1.5rem; } }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="logo">📷</div>
            <h1>AI Camera Setup</h1>
            <p class="subtitle">Connect your camera to WiFi and OpenAI</p>
        </div>

        <div id="statusMessage" class="status-message"></div>

        <!-- WiFi Network Selection -->
        <div class="card">
            <div class="section-title">
                <span class="section-icon">🔍</span>
                Select Network
            </div>
            <button class="btn-secondary btn" onclick="scanNetworks()">Scan for Networks</button>
            <div id="loading" class="loading">
                <div class="spinner"></div>
                <p>Scanning for networks...</p>
            </div>
            <div id="networkList" class="network-list"></div>
            <div class="form-group">
                <label for="ssid">Network Name (SSID)</label>
                <input type="text" id="ssid" placeholder="Select a network above" readonly>
            </div>
            <div class="form-group">
                <label for="password">Network Password</label>
                <input type="password" id="password" placeholder="Enter WiFi password">
            </div>
        </div>

        <!-- OpenAI API Key -->
        <div class="card">
            <div class="section-title">
                <span class="section-icon">🤖</span>
                OpenAI API Key
            </div>
            <div class="form-group">
                <label for="openaiKey">API Key</label>
                <input type="password" id="openaiKey" placeholder="sk-...">
            </div>
            <p class="help-text">
                Get your key at <a href="https://platform.openai.com/api-keys" target="_blank">platform.openai.com/api-keys</a>
            </p>
        </div>

        <!-- IP Configuration -->
        <div class="card">
            <div class="section-title">
                <span class="section-icon">🌐</span>
                IP Configuration
            </div>
            <div class="toggle-group">
                <div class="toggle-btn active" id="dhcpBtn" onclick="selectIPMode('dhcp')">DHCP (Automatic)</div>
                <div class="toggle-btn" id="staticBtn" onclick="selectIPMode('static')">Static IP</div>
            </div>
            <div id="staticIPFields" class="static-ip-fields">
                <div class="form-group"><label for="staticIP">IP Address</label><input type="text" id="staticIP" placeholder="192.168.1.100"></div>
                <div class="form-group"><label for="gateway">Gateway</label><input type="text" id="gateway" placeholder="192.168.1.1"></div>
                <div class="form-group"><label for="subnet">Subnet Mask</label><input type="text" id="subnet" placeholder="255.255.255.0"></div>
                <div class="form-group"><label for="dns1">Primary DNS</label><input type="text" id="dns1" placeholder="8.8.8.8"></div>
                <div class="form-group"><label for="dns2">Secondary DNS</label><input type="text" id="dns2" placeholder="8.8.4.4"></div>
            </div>
        </div>

        <button class="btn" onclick="saveConfig()">Connect & Start Camera</button>

        <div style="margin: 20px 0; text-align: center; color: var(--text-dim);"><p>— OR —</p></div>
        <button class="btn btn-wps" onclick="startWPS()">📶 Use WPS (Push Button)</button>
        <p style="margin-top: 10px; font-size: 0.85rem; color: var(--text-dim); text-align: center;">
            Press WPS button on your router, then click above. You'll still need to enter the OpenAI key after connecting.
        </p>

        <div class="footer"><p>AI Camera Firmware v3.0.0</p></div>
    </div>

    <script>
        let selectedSSID = '';
        let ipMode = 'dhcp';

        window.addEventListener('load', () => { scanNetworks(); });

        function showStatus(message, type) {
            const status = document.getElementById('statusMessage');
            status.textContent = message;
            status.className = 'status-message show ' + type;
            setTimeout(() => { status.className = 'status-message'; }, 5000);
        }

        function scanNetworks() {
            document.getElementById('loading').className = 'loading show';
            document.getElementById('networkList').innerHTML = '';
            fetch('/scan')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('loading').className = 'loading';
                    displayNetworks(data.networks);
                })
                .catch(error => {
                    document.getElementById('loading').className = 'loading';
                    showStatus('Failed to scan networks', 'error');
                });
        }

        function displayNetworks(networks) {
            const list = document.getElementById('networkList');
            list.innerHTML = '';
            if (networks.length === 0) {
                list.innerHTML = '<p style="text-align: center; padding: 20px; color: var(--text-dim);">No networks found</p>';
                return;
            }
            networks.forEach(network => {
                const item = document.createElement('div');
                item.className = 'network-item';
                item.onclick = () => selectNetwork(network.ssid);
                const strength = getSignalStrength(network.rssi);
                const bars = generateSignalBars(strength);
                item.innerHTML = `
                    <div class="network-name">${network.ssid}</div>
                    <div class="network-strength">
                        ${network.encryption ? '🔒' : '🔓'}
                        ${bars}
                        <span>${network.rssi} dBm</span>
                    </div>`;
                list.appendChild(item);
            });
        }

        function getSignalStrength(rssi) {
            if (rssi >= -50) return 4;
            if (rssi >= -60) return 3;
            if (rssi >= -70) return 2;
            return 1;
        }

        function generateSignalBars(strength) {
            const heights = ['8px', '12px', '16px', '20px'];
            let html = '<div class="signal-bars">';
            for (let i = 0; i < 4; i++) {
                const active = i < strength ? 'var(--primary)' : 'var(--border)';
                html += `<div class="signal-bar" style="height: ${heights[i]}; background: ${active};"></div>`;
            }
            html += '</div>';
            return html;
        }

        function selectNetwork(ssid) {
            selectedSSID = ssid;
            document.getElementById('ssid').value = ssid;
            document.querySelectorAll('.network-item').forEach(item => {
                item.classList.remove('selected');
                if (item.querySelector('.network-name').textContent === ssid) {
                    item.classList.add('selected');
                }
            });
            document.getElementById('password').focus();
        }

        function selectIPMode(mode) {
            ipMode = mode;
            document.getElementById('dhcpBtn').classList.toggle('active', mode === 'dhcp');
            document.getElementById('staticBtn').classList.toggle('active', mode === 'static');
            const fields = document.getElementById('staticIPFields');
            if (mode === 'static') { fields.classList.add('show'); } else { fields.classList.remove('show'); }
        }

        function saveConfig() {
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const openaiKey = document.getElementById('openaiKey').value;

            if (!ssid) { showStatus('Please select a network', 'error'); return; }
            if (!openaiKey || !openaiKey.startsWith('sk-')) {
                showStatus('Please enter a valid OpenAI API key (starts with sk-)', 'error');
                return;
            }

            const config = {
                ssid: ssid,
                password: password,
                openaiKey: openaiKey,
                useStaticIP: ipMode === 'static'
            };

            if (ipMode === 'static') {
                config.staticIP = document.getElementById('staticIP').value;
                config.gateway = document.getElementById('gateway').value;
                config.subnet = document.getElementById('subnet').value;
                config.dns1 = document.getElementById('dns1').value;
                config.dns2 = document.getElementById('dns2').value;
                if (!config.staticIP || !config.gateway || !config.subnet) {
                    showStatus('Please fill in all required IP fields', 'error');
                    return;
                }
            }

            showStatus('Connecting to network...', 'success');

            fetch('/save', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    showStatus('Connected! Camera is restarting... Open http://' + data.ip + ' in a moment.', 'success');
                } else {
                    showStatus('Failed to connect: ' + data.message, 'error');
                }
            })
            .catch(error => { showStatus('Connection error. Please try again.', 'error'); });
        }

        function startWPS() {
            showStatus('Starting WPS mode...', 'success');
            fetch('/wps', { method: 'POST' })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    showStatus('WPS started! Press the WPS button on your router now. Waiting up to 2 minutes...', 'success');
                } else {
                    showStatus('Failed to start WPS: ' + data.message, 'error');
                }
            })
            .catch(error => { showStatus('WPS error. Please try again.', 'error'); });
        }
    </script>
</body>
</html>
)rawliteral";

// ============================================================
// CAMERA APP HTML (served after WiFi is connected)
// ============================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head><title>ESP32-S3 OpenAI + TTS</title><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f5f5f5; padding: 20px; }
.main-grid { display: grid; grid-template-columns: 1.5fr 1fr; grid-template-rows: auto auto auto auto auto; gap: 20px; max-width: 1400px; margin: 0 auto; }
.video-section { grid-column: 1; grid-row: 1 / 5; background: white; border-radius: 12px; border: 2px solid #ddd; padding: 20px; }
.audio-section { grid-column: 2; grid-row: 1; background: white; border-radius: 12px; border: 2px solid #ddd; padding: 20px; }
.return-section { grid-column: 2; grid-row: 2; display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
.files-section { grid-column: 2; grid-row: 3; display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
.message-section { grid-column: 1 / 3; grid-row: 4; background: white; border-radius: 12px; border: 2px solid #ddd; padding: 20px; }
.video-controls { display: flex; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; }
.video-display { background: #000; border-radius: 8px; position: relative; padding-bottom: 75%; overflow: hidden; }
.video-display img { position: absolute; width: 100%; height: 100%; object-fit: contain; }
.audio-bar { background: #f0f0f0; border-radius: 8px; padding: 15px; margin-bottom: 15px; text-align: center; font-weight: bold; color: #333; }
.recording-timer { font-size: 32px; color: #e74c3c; font-family: monospace; margin: 15px 0; text-align: center; }
.processing-msg { text-align: center; color: #667eea; font-size: 16px; margin: 15px 0; font-weight: bold; }
.file-column, .return-column { background: white; border-radius: 12px; border: 2px solid #ddd; padding: 15px; }
.file-column h3, .return-column h3 { margin-bottom: 10px; color: #333; font-size: 14px; border-bottom: 2px solid #667eea; padding-bottom: 8px; }
.file-list, .return-list { max-height: 150px; overflow-y: auto; margin-bottom: 10px; }
.file-item { background: #f8f8f8; padding: 8px; margin-bottom: 5px; border-radius: 6px; cursor: pointer; border: 2px solid transparent; font-size: 12px; }
.file-item:hover { background: #e8e8e8; }
.file-item.selected { border-color: #667eea; background: #e8eeff; }
.message-display { background: #f9f9f9; border-radius: 8px; padding: 15px; min-height: 150px; max-height: 300px; overflow-y: auto; margin-bottom: 15px; font-size: 14px; line-height: 1.6; border: 1px solid #ddd; white-space: pre-wrap; }
.message-controls { display: flex; gap: 10px; }
button { padding: 10px 20px; font-size: 14px; font-weight: 600; border: none; border-radius: 8px; cursor: pointer; transition: all 0.2s; color: white; }
button:hover:not(:disabled) { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.15); }
button:disabled { opacity: 0.5; cursor: not-allowed; }
.btn-stream { background: #3498db; }
.btn-stream.active { background: #27ae60; animation: pulse 1.5s infinite; }
.btn-capture { background: #667eea; }
.btn-review { background: #9b59b6; }
.btn-record { background: #e74c3c; }
.btn-playback { background: #2ecc71; }
.btn-delete { background: #e67e22; }
.btn-save { background: #16a085; }
.btn-send { background: #2c3e50; flex: 1; }
.btn-replay { background: #34495e; flex: 1; }
.btn-record.recording { animation: pulse 1.5s infinite; }
.streaming-indicator { position: absolute; top: 10px; left: 10px; background: rgba(39, 174, 96, 0.9); color: white; padding: 6px 12px; border-radius: 20px; font-size: 12px; font-weight: bold; display: none; z-index: 10; box-shadow: 0 2px 8px rgba(0,0,0,0.3); }
.streaming-indicator.active { display: flex; align-items: center; gap: 8px; }
.streaming-indicator::before { content: ''; width: 8px; height: 8px; background: white; border-radius: 50%; animation: blink 1s infinite; }
@keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
@keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.6; } }
.status-bar { position: fixed; top: 20px; right: 20px; background: #2ecc71; color: white; padding: 12px 20px; border-radius: 8px; box-shadow: 0 4px 12px rgba(0,0,0,0.2); display: none; z-index: 1000; max-width: 300px; }
.status-bar.show { display: block; }
.status-bar.error { background: #e74c3c; }
.info-banner { background: #fff3cd; padding: 12px; border-radius: 8px; margin-bottom: 15px; border-left: 4px solid #ffc107; font-size: 13px; }
@media (max-width: 1024px) { .main-grid { grid-template-columns: 1fr; } .video-section, .audio-section, .capture-controls, .return-section, .files-section, .message-section { grid-column: 1; } .video-section { grid-row: auto; } }
</style>
</head>
<body>
<div id="statusBar" class="status-bar"></div>
<div id="connectionBanner" class="info-banner" style="max-width:1400px;margin:0 auto 20px;background:#d4edda;border-left-color:#28a745;">
  <strong>Connection Info:</strong> <span id="connectionInfo">Loading...</span>
</div>
<div class="main-grid">
  <div class="video-section">
    <div class="info-banner"><strong>Pairing Workflow:</strong> Capture image/audio → Save to export → Send to OpenAI agent</div>
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
  <div class="return-section">
    <div class="return-column"><h3>Image for Export</h3><div class="file-item" id="exportedImageDisplay" style="min-height:60px; display:flex; align-items:center; justify-content:center; color:#999; font-size:13px">None selected</div></div>
    <div class="return-column"><h3>Audio for Export</h3><div class="file-item" id="exportedAudioDisplay" style="min-height:60px; display:flex; align-items:center; justify-content:center; color:#999; font-size:13px">None selected</div></div>
  </div>
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
let selectedImagesForExport = [];
let selectedAudioForExport = "";
let lastAnalysisResponse = "";
let lastTTSAudioFile = "";
let lastAnalysisID = "";
let isRecording = false;
const MAX_IMAGES_FOR_EXPORT = 5;

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
    videoStream.src = '/stream?' + Date.now(); startStreamRefresh();
    indicator.classList.add('active'); streamBtn.classList.add('active');
    streamBtn.textContent = 'Stop Stream'; showStatus('Stream started');
  } else {
    stopStreamRefresh(); videoStream.src = '';
    indicator.classList.remove('active'); streamBtn.classList.remove('active');
    streamBtn.textContent = 'Video Stream'; showStatus('Stream stopped');
  }
}
function startStreamRefresh() { if (streamInterval) clearInterval(streamInterval); streamInterval = setInterval(() => { if (streamActive && !isRecording) { document.getElementById('videoStream').src = '/stream?' + Date.now(); } }, 150); }
function stopStreamRefresh() { if (streamInterval) { clearInterval(streamInterval); streamInterval = null; } }
function captureImage() { fetch('/capture').then(res => res.json()).then(data => { if (data.success) { showStatus('Image captured: ' + data.filename); selectedImageFile = data.filename; loadImageList(); } else { showStatus('Capture failed', true); } }).catch(err => showStatus('Capture error', true)); }
function reviewLastImage() { if (!selectedImageFile) { showStatus('No image selected to review', true); return; } stopStreamRefresh(); streamActive = false; const indicator = document.getElementById('streamingIndicator'); const streamBtn = document.querySelector('.btn-stream'); indicator.classList.remove('active'); streamBtn.classList.remove('active'); streamBtn.textContent = 'Video Stream'; document.getElementById('videoStream').src = '/image?file=' + encodeURIComponent(selectedImageFile); showStatus('Reviewing: ' + selectedImageFile); }
function loadImageList() { fetch('/list/images').then(res => res.json()).then(data => { const list = document.getElementById('imageFileList'); list.innerHTML = ''; if (data.files.length === 0) { list.innerHTML = '<div style="text-align:center; color:#999; padding:10px">No images</div>'; return; } data.files.forEach(file => { const item = document.createElement('div'); item.className = 'file-item'; if (file === selectedImageFile) { item.classList.add('selected'); } item.textContent = file; item.onclick = () => selectImageFile(file); list.appendChild(item); }); }); }
function loadAudioList() { fetch('/list/audio').then(res => res.json()).then(data => { const list = document.getElementById('audioFileList'); list.innerHTML = ''; if (data.files.length === 0) { list.innerHTML = '<div style="text-align:center; color:#999; padding:10px">No audio</div>'; return; } data.files.forEach(file => { const item = document.createElement('div'); item.className = 'file-item'; if (file === selectedAudioFile) { item.classList.add('selected'); } item.textContent = file; item.onclick = () => selectAudioFile(file); list.appendChild(item); }); }); }
function selectImageFile(filename) { selectedImageFile = filename; document.querySelectorAll('#imageFileList .file-item').forEach(item => { item.classList.toggle('selected', item.textContent === filename); }); document.getElementById('imageToExportBtn').disabled = false; document.getElementById('imageDownloadBtn').disabled = false; document.getElementById('imageFileDeleteBtn').disabled = false; showStatus('Selected: ' + filename); }
function selectAudioFile(filename) { selectedAudioFile = filename; document.querySelectorAll('#audioFileList .file-item').forEach(item => { item.classList.toggle('selected', item.textContent === filename); }); document.getElementById('audioToExportBtn').disabled = false; document.getElementById('audioDownloadBtn').disabled = false; document.getElementById('audioFileDeleteBtn').disabled = false; const audioPlayer = document.getElementById('audioPlayer'); audioPlayer.src = '/audio?file=' + encodeURIComponent(filename); audioPlayer.style.display = 'block'; document.getElementById('playbackBtn').disabled = false; showStatus('Selected: ' + filename); }
function addSelectedImageToExport() { if (!selectedImageFile) { showStatus('No image selected', true); return; } if (selectedImagesForExport.includes(selectedImageFile)) { showStatus('Image already in export queue', true); return; } if (selectedImagesForExport.length >= MAX_IMAGES_FOR_EXPORT) { showStatus('Maximum ' + MAX_IMAGES_FOR_EXPORT + ' images allowed', true); return; } selectedImagesForExport.push(selectedImageFile); updateExportImageDisplay(); showStatus('Added to export queue: ' + selectedImageFile); }
function removeImageFromExport(filename) { const index = selectedImagesForExport.indexOf(filename); if (index > -1) { selectedImagesForExport.splice(index, 1); updateExportImageDisplay(); showStatus('Removed from export: ' + filename); } }
function clearAllExportImages() { selectedImagesForExport = []; updateExportImageDisplay(); showStatus('Cleared all images from export'); }
function updateExportImageDisplay() { const display = document.getElementById('exportedImageDisplay'); if (selectedImagesForExport.length === 0) { display.innerHTML = '<span style="color:#999; font-size:13px">None selected</span>'; } else { let html = '<div style="display:flex; flex-direction:column; gap:5px;">'; selectedImagesForExport.forEach((filename, index) => { html += '<div style="display:flex; align-items:center; justify-content:space-between; padding:5px; background:#f0f0f0; border-radius:4px;">'; html += '<span style="font-size:13px; color:#333;">' + (index + 1) + '. ' + filename + '</span>'; html += '<button onclick="removeImageFromExport(\'' + filename + '\')" style="background:#e74c3c; color:white; border:none; padding:3px 8px; border-radius:3px; cursor:pointer; font-size:11px;">Remove</button>'; html += '</div>'; }); html += '</div>'; html += '<div style="margin-top:8px; font-size:12px; color:#666;">Total: ' + selectedImagesForExport.length + ' image(s)</div>'; if (selectedImagesForExport.length < MAX_IMAGES_FOR_EXPORT) { html += '<button onclick="clearAllExportImages()" style="margin-top:5px; background:#e67e22; color:white; border:none; padding:5px 10px; border-radius:4px; cursor:pointer; font-size:12px; width:100%;">Clear All</button>'; } display.innerHTML = html; } }
function addSelectedAudioToExport() { if (!selectedAudioFile) { showStatus('No audio selected', true); return; } selectedAudioForExport = selectedAudioFile; document.getElementById('exportedAudioDisplay').textContent = selectedAudioFile; document.getElementById('exportedAudioDisplay').style.color = '#333'; showStatus('Added to export queue: ' + selectedAudioFile); }
function downloadSelectedImage() { if (!selectedImageFile) { showStatus('No image selected', true); return; } window.location.href = '/image?file=' + encodeURIComponent(selectedImageFile); showStatus('Downloading: ' + selectedImageFile); }
function downloadSelectedAudio() { if (!selectedAudioFile) { showStatus('No audio selected', true); return; } window.location.href = '/audio?file=' + encodeURIComponent(selectedAudioFile); showStatus('Downloading: ' + selectedAudioFile); }
function deleteSelectedImageFile() { if (!selectedImageFile) { showStatus('No image selected', true); return; } if (confirm('Delete ' + selectedImageFile + ' from SD card?')) { fetch('/delete/image?file=' + encodeURIComponent(selectedImageFile), { method: 'DELETE' }).then(() => { showStatus('Deleted: ' + selectedImageFile); selectedImageFile = ""; document.getElementById('imageToExportBtn').disabled = true; document.getElementById('imageDownloadBtn').disabled = true; document.getElementById('imageFileDeleteBtn').disabled = true; loadImageList(); }); } }
function deleteSelectedAudioFile() { if (!selectedAudioFile) { showStatus('No audio selected', true); return; } if (confirm('Delete ' + selectedAudioFile + ' from SD card?')) { fetch('/delete/audio?file=' + encodeURIComponent(selectedAudioFile), { method: 'DELETE' }).then(() => { showStatus('Deleted: ' + selectedAudioFile); selectedAudioFile = ""; document.getElementById('audioPlayer').src = ''; document.getElementById('playbackBtn').disabled = true; document.getElementById('audioToExportBtn').disabled = true; document.getElementById('audioDownloadBtn').disabled = true; document.getElementById('audioFileDeleteBtn').disabled = true; loadAudioList(); }); } }
function updateTimer() { const elapsed = Math.floor((Date.now() - recordingStartTime) / 1000); const minutes = Math.floor(elapsed / 60); const seconds = elapsed % 60; document.getElementById('recordingTimer').textContent = String(minutes).padStart(2, '0') + ':' + String(seconds).padStart(2, '0'); }
function toggleRecording() { const recordBtn = document.getElementById('recordBtn'); if (isRecording) { clearInterval(timerInterval); const duration = Math.floor((Date.now() - recordingStartTime) / 1000); stopStreamRefresh(); recordBtn.textContent = 'Processing...'; recordBtn.disabled = true; recordBtn.classList.remove('recording'); document.getElementById('recordingTimer').style.display = 'none'; document.getElementById('processingMsg').style.display = 'block'; showStatus('Recording ' + duration + ' seconds...'); const timeout = (duration + 10) * 1000; Promise.race([ fetch('/audio/record?duration=' + duration, { signal: AbortSignal.timeout(timeout) }), new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), timeout)) ]).then(res => res.json()).then(data => { if (data.success) { showStatus('Audio saved: ' + data.filename); loadAudioList(); } else { showStatus('Recording failed', true); } }).catch(err => { showStatus('Recording timeout - audio may still be saving', true); setTimeout(loadAudioList, 2000); }).finally(() => { recordBtn.textContent = 'Record'; recordBtn.disabled = false; document.getElementById('processingMsg').style.display = 'none'; isRecording = false; if (streamActive) startStreamRefresh(); }); } else { isRecording = true; recordingStartTime = Date.now(); recordBtn.classList.add('recording'); recordBtn.textContent = 'Stop'; document.getElementById('recordingTimer').style.display = 'block'; timerInterval = setInterval(updateTimer, 100); showStatus('Recording started'); } }
function playbackAudio() { if (!selectedAudioFile) { showStatus('No audio selected', true); return; } showStatus('Playing on speaker...'); fetch('/audio/speaker?file=' + encodeURIComponent(selectedAudioFile), { signal: AbortSignal.timeout(60000) }).then(res => res.json()).then(data => { if (data.success) { showStatus('Playback complete'); } else { showStatus('Playback failed', true); } }).catch(err => showStatus('Playback timeout', true)); }
function sendToOpenAI() { if (selectedImagesForExport.length === 0 || !selectedAudioForExport) { showStatus('Select at least one image and audio for export first', true); return; } lastAnalysisID = Date.now().toString(); stopStreamRefresh(); const imageCount = selectedImagesForExport.length; showStatus('Starting OpenAI analysis with ' + imageCount + ' image(s)...'); document.getElementById('messageDisplay').textContent = 'Initializing analysis...'; fetch('/openai/analyze', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ images: selectedImagesForExport, audio: selectedAudioForExport, id: lastAnalysisID }) }).then(res => res.json()).then(data => { if (data.status === 'processing') { const checkProgress = () => { fetch('/openai/progress').then(res => res.json()).then(progress => { if (progress.resultReady) { clearInterval(progressInterval); if (progress.success) { lastAnalysisResponse = progress.response; lastTTSAudioFile = progress.ttsFile; document.getElementById('messageDisplay').textContent = progress.response; showStatus('Analysis complete!'); } else { document.getElementById('messageDisplay').textContent = 'Analysis failed:\n\n' + progress.response; showStatus('Analysis failed', true); } if (streamActive) startStreamRefresh(); } else if (progress.inProgress) { const filledBlocks = Math.floor(progress.percent / 5); const progressBar = '='.repeat(filledBlocks) + '>' + '-'.repeat(Math.max(0, 19 - filledBlocks)); document.getElementById('messageDisplay').textContent = progress.stage + ' - ' + progress.percent + '%\n[' + progressBar + ']\n\n' + progress.detail + '\n\nThis process takes 60-90 seconds total.\nPlease wait...'; showStatus(progress.stage + ' - ' + progress.percent + '%'); } }).catch(err => { clearInterval(progressInterval); showStatus('Progress check failed', true); if (streamActive) startStreamRefresh(); }); }; checkProgress(); const progressInterval = setInterval(checkProgress, 1500); } else { showStatus('Failed to start analysis', true); document.getElementById('messageDisplay').textContent = 'Error: ' + (data.error || 'Unknown error'); if (streamActive) startStreamRefresh(); } }).catch(err => { showStatus('Request failed', true); document.getElementById('messageDisplay').textContent = 'Request failed. Check serial monitor for details.'; if (streamActive) startStreamRefresh(); }); }
function replayTTSAudio() { if (!lastTTSAudioFile || lastTTSAudioFile === 'none') { showStatus('No TTS audio available', true); return; } const audioPlayer = document.getElementById('audioPlayer'); audioPlayer.src = '/tts?file=' + encodeURIComponent(lastTTSAudioFile); audioPlayer.style.display = 'block'; audioPlayer.currentTime = 0; audioPlayer.play(); showStatus('Playing TTS response'); }
function downloadAnalysisPackage() { if (!lastAnalysisID) { showStatus('Complete an analysis first', true); return; } showStatus('Preparing package download...'); window.location.href = '/analysis/export?id=' + lastAnalysisID; setTimeout(() => { showStatus('Package downloaded'); }, 1000); }
loadImageList();
loadAudioList();
setTimeout(() => { toggleStream(); }, 500);

// Fetch and display device connection info
fetch('/device-info').then(res => res.json()).then(info => {
  const connInfo = document.getElementById('connectionInfo');
  connInfo.innerHTML = 'IP: <strong>' + info.ip + '</strong> | ' +
    'Bookmark: <a href="http://' + info.mdns + '" style="color:#155724;font-weight:bold;">http://' + info.mdns + '</a> | ' +
    'Signal: ' + info.rssi + ' dBm';
}).catch(() => {
  document.getElementById('connectionInfo').textContent = 'Could not load device info';
});
</script>
</body>
</html>
)rawliteral";

// ============================================================
// CAMERA & OPENAI HELPER FUNCTIONS
// ============================================================

String base64EncodeFileOptimized(String filePath) {
  Serial.println("=== Base64 Encoding ===");
  File file = SD_MMC.open(filePath, FILE_READ);
  if (!file) { Serial.println("ERROR: Failed to open file"); return ""; }
  size_t fileSize = file.size();
  Serial.printf("File size: %d bytes\n", fileSize);
  size_t base64Size = ((fileSize + 2) / 3) * 4;
  String result = "";
  result.reserve(base64Size + 100);
  const size_t chunkSize = 3000;
  uint8_t* inputBuffer = (uint8_t*)malloc(chunkSize);
  uint8_t* outputBuffer = (uint8_t*)malloc(chunkSize * 2);
  if (!inputBuffer || !outputBuffer) {
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
        for (size_t i = 0; i < outLen; i++) { result += (char)outputBuffer[i]; }
        totalRead += bytesRead;
        if (totalRead % 30000 == 0) { Serial.printf("Encoded: %d bytes\n", totalRead); yield(); }
      } else { Serial.printf("ERROR: Base64 encode failed at offset %d\n", totalRead); break; }
    }
  }
  file.close();
  free(inputBuffer);
  free(outputBuffer);
  Serial.printf("Encoding complete: %d input -> %d base64\n", totalRead, result.length());
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
  if (!audioFile) { return "Error: File not found"; }
  size_t fileSize = audioFile.size();
  uint8_t* audioData = (uint8_t*)ps_malloc(fileSize);
  if (!audioData) audioData = (uint8_t*)malloc(fileSize);
  if (!audioData) { audioFile.close(); return "Error: Memory allocation failed"; }
  audioFile.read(audioData, fileSize);
  audioFile.close();
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);
  HTTPClient https;
  if (!https.begin(client, OPENAI_WHISPER_URL)) { free(audioData); return "Error: Connection setup failed"; }
  https.addHeader("Authorization", "Bearer " + String(deviceConfig.openaiKey));
  https.setTimeout(30000);
  String boundary = "----ESP32" + String(millis());
  https.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  String bodyStart = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String bodyEnd = "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n--" + boundary + "--\r\n";
  size_t totalSize = bodyStart.length() + fileSize + bodyEnd.length();
  uint8_t* requestBody = (uint8_t*)ps_malloc(totalSize);
  if (!requestBody) requestBody = (uint8_t*)malloc(totalSize);
  if (!requestBody) { free(audioData); https.end(); return "Error: Memory allocation failed"; }
  size_t offset = 0;
  memcpy(requestBody + offset, bodyStart.c_str(), bodyStart.length()); offset += bodyStart.length();
  memcpy(requestBody + offset, audioData, fileSize); offset += fileSize;
  memcpy(requestBody + offset, bodyEnd.c_str(), bodyEnd.length());
  int httpCode = https.POST(requestBody, totalSize);
  String transcription = "";
  if (httpCode == 200) {
    String response = https.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, response);
    if (!error) { transcription = doc["text"].as<String>(); } else { transcription = "Error: JSON parse failed"; }
  } else if (httpCode > 0) {
    transcription = "Error: Whisper API returned " + String(httpCode);
  } else {
    transcription = "Error: " + getHttpErrorString(httpCode);
  }
  free(audioData);
  free(requestBody);
  https.end();
  return transcription;
}

String analyzeImageWithGPT4Vision(String imageFilePaths, String audioTranscription) {
  Serial.println("\n=== GPT-4 Vision (Multi-Image) ===");
  String imagePaths[5];
  int imgCount = 0;
  int startPos = 0;
  int commaPos = imageFilePaths.indexOf(',');
  while (commaPos != -1 && imgCount < 5) {
    imagePaths[imgCount] = imageFilePaths.substring(startPos, commaPos);
    imagePaths[imgCount].trim();
    imgCount++;
    startPos = commaPos + 1;
    commaPos = imageFilePaths.indexOf(',', startPos);
  }
  if (startPos < imageFilePaths.length() && imgCount < 5) {
    imagePaths[imgCount] = imageFilePaths.substring(startPos);
    imagePaths[imgCount].trim();
    imgCount++;
  }
  Serial.printf("Analyzing %d image(s)\n", imgCount);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (!https.begin(client, OPENAI_CHAT_URL)) { return "Error: Connection setup failed"; }
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + String(deviceConfig.openaiKey));
  https.setTimeout(90000);
  String requestBody = "{\"model\":\"gpt-4o\",\"max_tokens\":1000,\"messages\":[{\"role\":\"user\",\"content\":[";
  requestBody += "{\"type\":\"text\",\"text\":\"";
  if (imgCount > 1) { requestBody += "Analyze these " + String(imgCount) + " images. "; }
  else { requestBody += "Analyze this image. "; }
  if (audioTranscription.length() > 0 && !audioTranscription.startsWith("Error")) {
    requestBody += "Audio: \\\"" + audioTranscription + "\\\". Consider both.";
  }
  requestBody += "\"},";
  for (int i = 0; i < imgCount; i++) {
    Serial.printf("Processing image %d/%d: %s\n", i+1, imgCount, imagePaths[i].c_str());
    if (ESP.getFreeHeap() < 40000) { https.end(); return "Error: Insufficient memory for image " + String(i+1); }
    String imageBase64 = base64EncodeFileOptimized(imagePaths[i]);
    if (imageBase64.length() == 0) { https.end(); return "Error: Base64 encoding failed for image " + String(i+1); }
    requestBody += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
    requestBody += imageBase64;
    requestBody += "\"}}";
    if (i < imgCount - 1) { requestBody += ","; }
    imageBase64 = "";
    yield();
    server.handleClient();
  }
  requestBody += "]}]}";
  int httpCode = https.POST(requestBody);
  requestBody = "";
  String analysis = "";
  if (httpCode == 200) {
    String response = https.getString();
    DynamicJsonDocument responseDoc(6144);
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error == DeserializationError::Ok) { analysis = responseDoc["choices"][0]["message"]["content"].as<String>(); }
    else { analysis = "Error: JSON parse failed"; }
  } else if (httpCode > 0) {
    analysis = "Error: GPT-4 returned " + String(httpCode);
  } else {
    analysis = "Error: Connection failed";
  }
  https.end();
  return analysis;
}

String generateTTSAudio(String text, String analysisID) {
  Serial.println("\n=== OpenAI TTS ===");
  ttsInProgress = true;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(60);
  HTTPClient https;
  https.begin(client, OPENAI_TTS_URL);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("Authorization", "Bearer " + String(deviceConfig.openaiKey));
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

// ============================================================
// CAMERA SETUP
// ============================================================
void setupCamera() {
  camera_config_t camConfig;
  camConfig.ledc_channel = LEDC_CHANNEL_0;
  camConfig.ledc_timer = LEDC_TIMER_0;
  camConfig.pin_d0 = Y2_GPIO_NUM;
  camConfig.pin_d1 = Y3_GPIO_NUM;
  camConfig.pin_d2 = Y4_GPIO_NUM;
  camConfig.pin_d3 = Y5_GPIO_NUM;
  camConfig.pin_d4 = Y6_GPIO_NUM;
  camConfig.pin_d5 = Y7_GPIO_NUM;
  camConfig.pin_d6 = Y8_GPIO_NUM;
  camConfig.pin_d7 = Y9_GPIO_NUM;
  camConfig.pin_xclk = XCLK_GPIO_NUM;
  camConfig.pin_pclk = PCLK_GPIO_NUM;
  camConfig.pin_vsync = VSYNC_GPIO_NUM;
  camConfig.pin_href = HREF_GPIO_NUM;
  camConfig.pin_sscb_sda = SIOD_GPIO_NUM;
  camConfig.pin_sscb_scl = SIOC_GPIO_NUM;
  camConfig.pin_pwdn = PWDN_GPIO_NUM;
  camConfig.pin_reset = RESET_GPIO_NUM;
  camConfig.xclk_freq_hz = 20000000;
  camConfig.pixel_format = PIXFORMAT_JPEG;
  camConfig.frame_size = FRAMESIZE_SVGA;
  camConfig.jpeg_quality = 12;
  camConfig.fb_count = 1;
  camConfig.grab_mode = CAMERA_GRAB_LATEST;
  esp_err_t err = esp_camera_init(&camConfig);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, 0);
  s->set_hmirror(s, 1);
}

// ============================================================
// CAMERA HTTP HANDLERS
// ============================================================
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
  if (file) { file.write(fb->buf, fb->len); file.close(); server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + filename + "\"}"); }
  else { server.send(500, "application/json", "{\"success\":false}"); }
  esp_camera_fb_return(fb);
}

void handleImageView() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/images/" + filename, FILE_READ);
  if (file) { server.streamFile(file, "image/jpeg"); file.close(); } else { server.send(404); }
}

void handleLatestImage() {
  File root = SD_MMC.open("/images");
  if (!root) { server.send(200, "application/json", "{\"success\":false}"); return; }
  String latest = "";
  File file = root.openNextFile();
  while (file) { if (!file.isDirectory()) { latest = String(file.name()); } file = root.openNextFile(); }
  root.close();
  if (latest.length() > 0) { server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + latest + "\"}"); }
  else { server.send(200, "application/json", "{\"success\":false}"); }
}

void handleAudioRecord() {
  if (recordingInProgress || ttsInProgress) { server.send(503, "application/json", "{\"success\":false,\"error\":\"busy\"}"); return; }
  recordingInProgress = true;
  int duration = server.arg("duration").toInt();
  if (duration <= 0) duration = 5;
  int totalSamples = SAMPLE_RATE * duration;
  int16_t* audioBuffer = (int16_t*)ps_malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) audioBuffer = (int16_t*)malloc(totalSamples * sizeof(int16_t));
  if (!audioBuffer) { recordingInProgress = false; server.send(500, "application/json", "{\"success\":false,\"error\":\"memory\"}"); return; }
  size_t samplesRead = 0;
  while (samplesRead < totalSamples) {
    size_t bytesRead = i2s_mic.readBytes((char*)(audioBuffer + samplesRead), (totalSamples - samplesRead) * sizeof(int16_t));
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
    uint32_t chunkSize = fileSize - 8; file.write((uint8_t*)&chunkSize, 4);
    file.write((uint8_t*)"WAVE", 4);
    file.write((uint8_t*)"fmt ", 4);
    uint32_t subchunk1Size = 16; file.write((uint8_t*)&subchunk1Size, 4);
    uint16_t audioFormat = 1; file.write((uint8_t*)&audioFormat, 2);
    uint16_t numChannels = 1; file.write((uint8_t*)&numChannels, 2);
    uint32_t sampleRate = SAMPLE_RATE; file.write((uint8_t*)&sampleRate, 4);
    uint32_t byteRate = SAMPLE_RATE * 2; file.write((uint8_t*)&byteRate, 4);
    uint16_t blockAlign = 2; file.write((uint8_t*)&blockAlign, 2);
    uint16_t bitsPerSample = 16; file.write((uint8_t*)&bitsPerSample, 2);
    file.write((uint8_t*)"data", 4);
    uint32_t dataSize = totalSamples * 2; file.write((uint8_t*)&dataSize, 4);
    file.write((uint8_t*)audioBuffer, totalSamples * 2);
    file.close();
    free(audioBuffer);
    recordingInProgress = false;
    server.send(200, "application/json", "{\"success\":true,\"filename\":\"" + filename + "\"}");
  } else {
    free(audioBuffer); recordingInProgress = false;
    server.send(500, "application/json", "{\"success\":false}");
  }
}

void handleAudioStream() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/audio/" + filename, FILE_READ);
  if (file) { server.streamFile(file, "audio/wav"); file.close(); } else { server.send(404); }
}

void handleSpeakerPlayback() {
  String filename = server.arg("file");
  File file = SD_MMC.open("/audio/" + filename, FILE_READ);
  if (!file) { server.send(404, "application/json", "{\"success\":false}"); return; }
  file.seek(44);
  uint8_t monoBuffer[256];
  uint8_t stereoBuffer[512];
  while (file.available()) {
    size_t bytesRead = file.read(monoBuffer, sizeof(monoBuffer));
    if (bytesRead > 0) {
      int16_t* monoSamples = (int16_t*)monoBuffer;
      int16_t* stereoSamples = (int16_t*)stereoBuffer;
      size_t numSamples = bytesRead / 2;
      for (size_t i = 0; i < numSamples; i++) {
        stereoSamples[i * 2] = monoSamples[i];
        stereoSamples[i * 2 + 1] = monoSamples[i];
      }
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
  if (file) { server.streamFile(file, "audio/mpeg"); file.close(); } else { server.send(404); }
}

void handleListImages() {
  File root = SD_MMC.open("/images");
  String json = "{\"files\":[";
  bool first = true;
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) { if (!first) json += ","; json += "\"" + String(file.name()) + "\""; first = false; }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleListAudio() {
  File root = SD_MMC.open("/audio");
  String json = "{\"files\":[";
  bool first = true;
  while (File file = root.openNextFile()) {
    if (!file.isDirectory()) { if (!first) json += ","; json += "\"" + String(file.name()) + "\""; first = false; }
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleDeleteImage() {
  String filename = server.arg("file");
  if (SD_MMC.remove("/images/" + filename)) { server.send(200, "application/json", "{\"success\":true}"); }
  else { server.send(500, "application/json", "{\"success\":false}"); }
}

void handleDeleteAudio() {
  String filename = server.arg("file");
  if (SD_MMC.remove("/audio/" + filename)) { server.send(200, "application/json", "{\"success\":true}"); }
  else { server.send(500, "application/json", "{\"success\":false}"); }
}

// ============================================================
// OPENAI ANALYSIS PIPELINE
// ============================================================
void runOpenAIAnalysis() {
  analysisResultReady = false;
  analysisResultSuccess = false;
  Serial.println("\n======== OpenAI Analysis ========");
  Serial.println("Image: " + currentImagePath);
  Serial.println("Audio: " + currentAudioPath);
  analysisProgressStage = "Validation";
  analysisProgressDetail = "Checking files...";
  analysisProgressPercent = 5;
  server.handleClient(); yield();
  if (!SD_MMC.exists(currentImagePath) || !SD_MMC.exists(currentAudioPath)) {
    analysisInProgress = false; analysisProgressStage = "Error"; analysisProgressDetail = "File not found";
    analysisResultText = "Error: File not found"; analysisResultSuccess = false; analysisResultReady = true; return;
  }
  analysisProgressStage = "Whisper"; analysisProgressDetail = "Transcribing audio (30-40 sec)..."; analysisProgressPercent = 10;
  server.handleClient(); yield();
  String transcription = transcribeAudioWithWhisper(currentAudioPath);
  if (transcription.startsWith("Error")) {
    analysisInProgress = false; analysisProgressStage = "Error"; analysisProgressDetail = "Whisper failed";
    analysisResultText = transcription; analysisResultSuccess = false; analysisResultReady = true; return;
  }
  analysisProgressStage = "Vision"; analysisProgressDetail = "Analyzing images with GPT-4..."; analysisProgressPercent = 45;
  server.handleClient(); yield();
  String analysis = analyzeImageWithGPT4Vision(currentImagePath, transcription);
  if (analysis.startsWith("Error")) {
    analysisInProgress = false; analysisProgressStage = "Error"; analysisProgressDetail = "Vision failed";
    analysisResultText = analysis; analysisResultSuccess = false; analysisResultReady = true; return;
  }
  analysisProgressStage = "TTS"; analysisProgressDetail = "Generating speech audio (20-30 sec)..."; analysisProgressPercent = 75;
  server.handleClient(); yield();
  String combinedText = "Audio Transcription: " + transcription + ". Image Analysis: " + analysis;
  String ttsFile = generateTTSAudio(combinedText, currentAnalysisID);
  if (ttsFile.length() == 0) ttsFile = "none";
  analysisProgressStage = "Complete"; analysisProgressDetail = "Analysis finished"; analysisProgressPercent = 100;
  String result = "=== AUDIO TRANSCRIPTION ===\n" + transcription + "\n\n=== IMAGE ANALYSIS ===\n" + analysis;
  analysisResultText = result; analysisResultTTSFile = ttsFile; analysisResultSuccess = true; analysisResultReady = true;
  saveAnalysisPackage(currentAnalysisID, transcription, analysis, currentImagePath, currentAudioPath, ttsFile);
  analysisInProgress = false;
}

void saveAnalysisPackage(String analysisID, String transcription, String analysis, String imagePaths, String audioPath, String ttsFile) {
  String analysisDir = "/analysis/" + analysisID;
  SD_MMC.mkdir("/analysis"); SD_MMC.mkdir(analysisDir.c_str());
  String imagePathArray[5]; int imgCount = 0; int startPos = 0;
  int commaPos = imagePaths.indexOf(',');
  while (commaPos != -1 && imgCount < 5) { imagePathArray[imgCount] = imagePaths.substring(startPos, commaPos); imagePathArray[imgCount].trim(); imgCount++; startPos = commaPos + 1; commaPos = imagePaths.indexOf(',', startPos); }
  if (startPos < imagePaths.length() && imgCount < 5) { imagePathArray[imgCount] = imagePaths.substring(startPos); imagePathArray[imgCount].trim(); imgCount++; }
  File promptFile = SD_MMC.open(analysisDir + "/prompt.txt", FILE_WRITE);
  if (promptFile) { promptFile.println("Audio Transcription\n=================="); promptFile.println(transcription); promptFile.close(); }
  File responseFile = SD_MMC.open(analysisDir + "/response.txt", FILE_WRITE);
  if (responseFile) { responseFile.println("Image Analysis\n=============="); responseFile.println(analysis); responseFile.close(); }
  File combinedFile = SD_MMC.open(analysisDir + "/combined.txt", FILE_WRITE);
  if (combinedFile) { combinedFile.println("OpenAI Analysis Results\n======================"); combinedFile.println("Timestamp: " + analysisID); combinedFile.println("Images: " + imagePaths); combinedFile.println("Audio: " + audioPath); combinedFile.println("\n=== AUDIO TRANSCRIPTION ==="); combinedFile.println(transcription); combinedFile.println("\n=== IMAGE ANALYSIS ==="); combinedFile.println(analysis); combinedFile.close(); }
  File metaFile = SD_MMC.open(analysisDir + "/metadata.txt", FILE_WRITE);
  if (metaFile) { metaFile.println("Analysis ID: " + analysisID); metaFile.println("Images: " + imagePaths); metaFile.println("Audio: " + audioPath); metaFile.println("TTS: /tts/" + ttsFile); metaFile.close(); }
  for (int i = 0; i < imgCount; i++) {
    File srcImg = SD_MMC.open(imagePathArray[i], FILE_READ);
    File dstImg = SD_MMC.open(analysisDir + "/image_" + String(i + 1) + ".jpg", FILE_WRITE);
    if (srcImg && dstImg) { uint8_t buffer[512]; while (srcImg.available()) { size_t bytesRead = srcImg.read(buffer, sizeof(buffer)); if (bytesRead > 0) dstImg.write(buffer, bytesRead); } srcImg.close(); dstImg.close(); }
    yield();
  }
  if (ttsFile != "none" && ttsFile.length() > 0) {
    File srcTTS = SD_MMC.open("/tts/" + ttsFile, FILE_READ);
    File dstTTS = SD_MMC.open(analysisDir + "/audio.mp3", FILE_WRITE);
    if (srcTTS && dstTTS) { uint8_t buffer[512]; while (srcTTS.available()) { size_t bytesRead = srcTTS.read(buffer, sizeof(buffer)); if (bytesRead > 0) dstTTS.write(buffer, bytesRead); } srcTTS.close(); dstTTS.close(); }
  }
  Serial.println("Analysis package saved: " + analysisDir);
}

void handleOpenAIAnalyze() {
  if (analysisInProgress) { server.send(503, "application/json", "{\"success\":false,\"error\":\"Analysis already in progress\"}"); return; }
  DynamicJsonDocument reqDoc(2048);
  deserializeJson(reqDoc, server.arg("plain"));
  JsonArray imagesArray = reqDoc["images"].as<JsonArray>();
  String audioFile = reqDoc["audio"].as<String>();
  currentAnalysisID = reqDoc["id"].as<String>();
  currentImagePath = "";
  int imgCount = 0;
  for (JsonVariant imageVar : imagesArray) {
    if (imgCount >= 5) break;
    String imageFile = imageVar.as<String>();
    if (imgCount > 0) currentImagePath += ",";
    currentImagePath += "/images/" + imageFile;
    imgCount++;
  }
  currentAudioPath = "/audio/" + audioFile;
  analysisInProgress = true; analysisResultReady = false;
  analysisProgressStage = "Starting"; analysisProgressDetail = "Initializing..."; analysisProgressPercent = 0;
  server.send(202, "application/json", "{\"success\":true,\"status\":\"processing\"}");
  delay(100);
  runOpenAIAnalysis();
}

void handleOpenAIProgress() {
  DynamicJsonDocument doc(8192);
  doc["inProgress"] = analysisInProgress; doc["stage"] = analysisProgressStage;
  doc["detail"] = analysisProgressDetail; doc["percent"] = analysisProgressPercent;
  doc["resultReady"] = analysisResultReady;
  if (analysisResultReady) { doc["success"] = analysisResultSuccess; doc["response"] = analysisResultText; doc["ttsFile"] = analysisResultTTSFile; }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleAnalysisDownload() {
  String body = server.arg("plain");
  DynamicJsonDocument doc(4096); deserializeJson(doc, body);
  String id = doc["id"].as<String>(); String analysisText = doc["text"].as<String>();
  String summaryFilename = "ANALYSIS_" + id + ".txt";
  File summaryFile = SD_MMC.open("/" + summaryFilename, FILE_WRITE);
  if (summaryFile) { summaryFile.println("OpenAI Analysis Results\n======================"); summaryFile.println(analysisText); summaryFile.close(); }
  server.send(200, "application/json", "{\"success\":true,\"id\":\"" + id + "\"}");
}

void handleAnalysisPackage() {
  String id = server.arg("id"); String filename = "ANALYSIS_" + id + ".txt";
  File file = SD_MMC.open("/" + filename, FILE_READ);
  if (file) { server.streamFile(file, "text/plain"); file.close(); } else { server.send(404); }
}

void handleAnalysisExport() {
  String id = server.arg("id"); String analysisDir = "/analysis/" + id;
  File dir = SD_MMC.open(analysisDir);
  if (!dir || !dir.isDirectory()) { server.send(404, "text/plain", "Analysis not found"); return; }
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Analysis " + id + "</title>";
  html += "<style>body{font-family:Arial,sans-serif;max-width:800px;margin:50px auto;padding:20px;}h1{color:#2c3e50;}.file-list{list-style:none;padding:0;}.file-item{background:#ecf0f1;margin:10px 0;padding:15px;border-radius:5px;}.file-item a{color:#3498db;text-decoration:none;font-weight:bold;}.download-all{background:#27ae60;color:white;padding:12px 24px;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin:20px 0;}</style></head><body>";
  html += "<h1>Analysis Package</h1><p><strong>ID:</strong> " + id + "</p>";
  html += "<button class='download-all' onclick='downloadAll()'>Download All</button><ul class='file-list'>";
  File file = dir.openNextFile(); String fileList = "";
  while (file) {
    if (!file.isDirectory()) {
      String filename = String(file.name());
      html += "<li class='file-item'><a href='/analysis/file?id=" + id + "&file=" + filename + "' download='" + filename + "'>" + filename + "</a> (" + String(file.size()) + " bytes)</li>";
      fileList += "'" + filename + "',";
    }
    file = dir.openNextFile();
  }
  dir.close();
  html += "</ul><script>function downloadAll(){const files=[" + fileList + "];files.forEach((file,i)=>{setTimeout(()=>{const a=document.createElement('a');a.href='/analysis/file?id=" + id + "&file='+file;a.download=file;document.body.appendChild(a);a.click();document.body.removeChild(a);},i*500);});}</script></body></html>";
  server.send(200, "text/html", html);
}

void handleAnalysisFile() {
  String id = server.arg("id"); String filename = server.arg("file");
  String filepath = "/analysis/" + id + "/" + filename;
  File file = SD_MMC.open(filepath, FILE_READ);
  if (file) { server.streamFile(file, "application/octet-stream"); file.close(); } else { server.send(404); }
}

// ============================================================
// WIFI MANAGER: CONFIGURATION PERSISTENCE
// ============================================================
void saveDeviceConfiguration() {
  preferences.putString("ssid", deviceConfig.ssid);
  preferences.putString("password", deviceConfig.password);
  preferences.putString("openaiKey", deviceConfig.openaiKey);
  preferences.putBool("useStaticIP", deviceConfig.useStaticIP);
  if (deviceConfig.useStaticIP) {
    preferences.putUInt("staticIP", (uint32_t)deviceConfig.staticIP);
    preferences.putUInt("gateway", (uint32_t)deviceConfig.gateway);
    preferences.putUInt("subnet", (uint32_t)deviceConfig.subnet);
    preferences.putUInt("dns1", (uint32_t)deviceConfig.dns1);
    preferences.putUInt("dns2", (uint32_t)deviceConfig.dns2);
  }
  Serial.println("Configuration saved");
}

void loadDeviceConfiguration() {
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  String openaiKey = preferences.getString("openaiKey", "");
  ssid.toCharArray(deviceConfig.ssid, 32);
  password.toCharArray(deviceConfig.password, 64);
  openaiKey.toCharArray(deviceConfig.openaiKey, 200);
  deviceConfig.useStaticIP = preferences.getBool("useStaticIP", false);
  if (deviceConfig.useStaticIP) {
    deviceConfig.staticIP = preferences.getUInt("staticIP", 0);
    deviceConfig.gateway = preferences.getUInt("gateway", 0);
    deviceConfig.subnet = preferences.getUInt("subnet", 0);
    deviceConfig.dns1 = preferences.getUInt("dns1", 0);
    deviceConfig.dns2 = preferences.getUInt("dns2", 0);
  }
  Serial.println("Configuration loaded");
}

void clearDeviceSettings() {
  preferences.clear();
  memset(deviceConfig.ssid, 0, sizeof(deviceConfig.ssid));
  memset(deviceConfig.password, 0, sizeof(deviceConfig.password));
  memset(deviceConfig.openaiKey, 0, sizeof(deviceConfig.openaiKey));
  deviceConfig.useStaticIP = false;
  WiFi.disconnect(true, true);
  Serial.println("Settings cleared");
}

void factoryReset() {
  Serial.println("\n=== FACTORY RESET ===");
  clearDeviceSettings();
  Serial.println("Restarting in 2 seconds...");
  delay(2000);
  ESP.restart();
}

// ============================================================
// WIFI MANAGER: RESET BUTTON
// ============================================================
void checkResetButton() {
  bool currentState = (digitalRead(RESET_BUTTON_PIN) == LOW);
  if (currentState && !buttonPressed) {
    buttonPressed = true;
    buttonPressStartTime = millis();
    Serial.println("Reset button pressed...");
  } else if (currentState && buttonPressed) {
    unsigned long holdTime = millis() - buttonPressStartTime;
    if (holdTime >= RESET_HOLD_TIME && !resetTriggered) {
      resetTriggered = true;
      Serial.println("Reset triggered!");
      factoryReset();
    }
  } else if (!currentState && buttonPressed) {
    buttonPressed = false;
    resetTriggered = false;
  }
}

// ============================================================
// WIFI MANAGER: CONNECTION
// ============================================================
void WiFiEvent(WiFiEvent_t event) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
      reconnectAttempts = 0;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi disconnected");
      if (!configMode) { lastReconnectAttempt = millis(); shouldReconnect = true; }
      break;
    default: break;
  }
}

void connectToWiFi() {
  Serial.println("Connecting to: " + String(deviceConfig.ssid));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  if (deviceConfig.useStaticIP) {
    WiFi.config(deviceConfig.staticIP, deviceConfig.gateway, deviceConfig.subnet, deviceConfig.dns1, deviceConfig.dns2);
  }
  WiFi.begin(deviceConfig.ssid, deviceConfig.password);
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_CONNECT_TIMEOUT) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    configMode = false; reconnectAttempts = 0; shouldReconnect = true;
  } else {
    Serial.println("\nConnection failed");
  }
}

void attemptReconnect() {
  reconnectAttempts++;
  lastReconnectAttempt = millis();
  Serial.println("Reconnect attempt #" + String(reconnectAttempts));
  if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
    Serial.println("Max attempts reached, entering config mode");
    shouldReconnect = false; reconnectAttempts = 0;
    startConfigMode();
    return;
  }
  WiFi.disconnect(); delay(100);
  connectToWiFi();
}

// ============================================================
// WIFI MANAGER: CONFIG MODE (CAPTIVE PORTAL)
// ============================================================
void handleSetupRoot() { server.send_P(200, "text/html", SETUP_PAGE); }

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"encryption\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN) + "}";
  }
  json += "]}";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

void handleConfigSave() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    Serial.println("Config received: " + body);
    // Parse SSID
    int ssidStart = body.indexOf("\"ssid\":\"") + 8;
    int ssidEnd = body.indexOf("\"", ssidStart);
    String ssid = body.substring(ssidStart, ssidEnd);
    // Parse password
    int passStart = body.indexOf("\"password\":\"") + 12;
    int passEnd = body.indexOf("\"", passStart);
    String password = body.substring(passStart, passEnd);
    // Parse OpenAI key
    int keyStart = body.indexOf("\"openaiKey\":\"") + 13;
    int keyEnd = body.indexOf("\"", keyStart);
    String openaiKey = body.substring(keyStart, keyEnd);
    // Parse static IP flag
    int staticStart = body.indexOf("\"useStaticIP\":") + 14;
    bool useStatic = body.substring(staticStart, staticStart + 4) == "true";

    ssid.toCharArray(deviceConfig.ssid, 32);
    password.toCharArray(deviceConfig.password, 64);
    openaiKey.toCharArray(deviceConfig.openaiKey, 200);
    deviceConfig.useStaticIP = useStatic;

    if (useStatic) {
      int ipStart = body.indexOf("\"staticIP\":\"") + 12; int ipEnd = body.indexOf("\"", ipStart);
      deviceConfig.staticIP.fromString(body.substring(ipStart, ipEnd));
      int gwStart = body.indexOf("\"gateway\":\"") + 11; int gwEnd = body.indexOf("\"", gwStart);
      deviceConfig.gateway.fromString(body.substring(gwStart, gwEnd));
      int snStart = body.indexOf("\"subnet\":\"") + 10; int snEnd = body.indexOf("\"", snStart);
      deviceConfig.subnet.fromString(body.substring(snStart, snEnd));
      int d1Start = body.indexOf("\"dns1\":\"") + 8; int d1End = body.indexOf("\"", d1Start);
      deviceConfig.dns1.fromString(body.substring(d1Start, d1End));
      int d2Start = body.indexOf("\"dns2\":\"") + 8; int d2End = body.indexOf("\"", d2Start);
      deviceConfig.dns2.fromString(body.substring(d2Start, d2End));
    }

    saveDeviceConfiguration();
    WiFi.mode(WIFI_STA);
    connectToWiFi();

    String response;
    if (WiFi.status() == WL_CONNECTED) {
      response = "{\"success\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}";
      server.send(200, "application/json", response);
      delay(2000);
      ESP.restart();
    } else {
      response = "{\"success\":false,\"message\":\"Connection failed. Check password and try again.\"}";
      server.send(200, "application/json", response);
    }
  } else {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"No data\"}");
  }
}

void handleConfigNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleWPSRequest() {
  if (wpsRunning) { server.send(200, "application/json", "{\"success\":false,\"message\":\"WPS already running\"}"); return; }
  startWPS();
  server.send(200, "application/json", "{\"success\":true,\"message\":\"WPS started\"}");
}

void startConfigMode() {
  configMode = true;
  configStartTime = millis();
  Serial.println("\n=== Configuration Mode ===");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("AP IP: " + WiFi.softAPIP().toString());
  Serial.println("Connect to WiFi: " AP_SSID);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleSetupRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleConfigSave);
  server.on("/wps", HTTP_POST, handleWPSRequest);
  server.onNotFound(handleConfigNotFound);
  server.begin();
  Serial.println("Config server started at http://192.168.4.1");
}

// ============================================================
// WIFI MANAGER: WPS
// ============================================================
void startWPS() {
  if (wpsRunning) return;
  Serial.println("\n=== WPS Mode ===");
  WiFi.mode(WIFI_STA);
  wpsRunning = true;
  wpsStartTime = millis();
  lastWPSCheck = millis();
  esp_wps_config_t wpsConfig;
  wpsConfig.wps_type = WPS_TYPE_PBC;
  strcpy(wpsConfig.factory_info.manufacturer, "ESPRESSIF");
  strcpy(wpsConfig.factory_info.model_number, "ESP32-S3");
  strcpy(wpsConfig.factory_info.model_name, "AI-Camera");
  strcpy(wpsConfig.factory_info.device_name, "AI-Camera");
  if (esp_wifi_wps_enable(&wpsConfig) == ESP_OK) {
    if (esp_wifi_wps_start(0) == ESP_OK) {
      Serial.println("WPS initiated, press WPS on router");
    } else { Serial.println("WPS start failed"); wpsRunning = false; }
  } else { Serial.println("WPS enable failed"); wpsRunning = false; }
}

void handleWPS() {
  if (millis() - lastWPSCheck >= 500) {
    lastWPSCheck = millis();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWPS Connected! Network: " + WiFi.SSID() + " IP: " + WiFi.localIP().toString());
    String ssid = WiFi.SSID();
    String password = WiFi.psk();
    ssid.toCharArray(deviceConfig.ssid, 32);
    password.toCharArray(deviceConfig.password, 64);
    deviceConfig.useStaticIP = false;
    saveDeviceConfiguration();
    esp_wifi_wps_disable();
    wpsRunning = false;
    configMode = false;
    reconnectAttempts = 0;
    shouldReconnect = true;
    // Note: OpenAI key still needs to be entered. Restart into config mode
    // only if key is not yet set.
    if (strlen(deviceConfig.openaiKey) == 0) {
      Serial.println("WPS connected but OpenAI key not set. Restarting to config portal...");
      delay(2000);
      ESP.restart();
    }
    return;
  }
  if (millis() - wpsStartTime >= WPS_TIMEOUT) {
    Serial.println("WPS timeout");
    esp_wifi_wps_disable();
    wpsRunning = false;
    if (strlen(deviceConfig.ssid) == 0) { startConfigMode(); }
  }
}

// ============================================================
// APPLICATION SERVER (Camera routes - registered after WiFi connects)
// ============================================================
void setupApplicationServer() {
  Serial.println("\n=== Starting Camera Application ===");

  // Enable CORS for browser-based student apps
  server.enableCORS(true);

  // Handle preflight OPTIONS requests for POST endpoints
  server.on("/openai/analyze", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });

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

  // Device info endpoint for mDNS and IP display
  server.on("/device-info", HTTP_GET, []() {
    String json = "{";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"mdns\":\"" + String(MDNS_NAME) + ".local\",";
    json += "\"mac\":\"" + WiFi.macAddress() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();
  Serial.println("Camera server started at http://" + WiFi.localIP().toString());
}

void initializeFileCounters() {
  File root = SD_MMC.open("/images");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        if (filename.startsWith("IMG_") && filename.endsWith(".jpg")) {
          int underscorePos = filename.indexOf('_'); int dotPos = filename.indexOf('.');
          if (underscorePos > 0 && dotPos > underscorePos) {
            int num = filename.substring(underscorePos + 1, dotPos).toInt();
            if (num > imageCount) imageCount = num;
          }
        }
      }
      file = root.openNextFile();
    }
    root.close();
  }
  root = SD_MMC.open("/audio");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        if (filename.startsWith("REC_") && filename.endsWith(".wav")) {
          int underscorePos = filename.indexOf('_'); int dotPos = filename.indexOf('.');
          if (underscorePos > 0 && dotPos > underscorePos) {
            int num = filename.substring(underscorePos + 1, dotPos).toInt();
            if (num > audioCount) audioCount = num;
          }
        }
      }
      file = root.openNextFile();
    }
    root.close();
  }
  root = SD_MMC.open("/tts");
  if (root) { File file = root.openNextFile(); while (file) { if (!file.isDirectory()) ttsCount++; file = root.openNextFile(); } root.close(); }
  Serial.printf("File counters - Images: %d, Audio: %d, TTS: %d\n", imageCount, audioCount, ttsCount);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("AI Camera - Unified Firmware v3.0.0");
  Serial.println("========================================\n");

  // Hardware init
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true)) { Serial.println("ERROR: SD Card Mount Failed"); }
  else { Serial.println("SD Card OK"); SD_MMC.mkdir("/images"); SD_MMC.mkdir("/audio"); SD_MMC.mkdir("/tts"); }

  setupCamera();
  Serial.println("Camera OK");

  i2s_mic.setPinsPdmRx(PDM_CLOCK_PIN, PDM_DATA_PIN);
  if (i2s_mic.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) { Serial.println("PDM Microphone OK"); }
  else { Serial.println("PDM Microphone FAIL"); }

  i2s_spk.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);
  if (i2s_spk.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) { Serial.println("Speaker OK"); }
  else { Serial.println("Speaker FAIL"); }

  // Reset button
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // Check for factory reset at boot
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("Reset button held at boot...");
    unsigned long startWait = millis();
    while (millis() - startWait < RESET_HOLD_TIME) {
      if (digitalRead(RESET_BUTTON_PIN) == HIGH) break;
      delay(100);
    }
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
      Serial.println("Factory reset confirmed!");
      factoryReset();
    }
  }

  // Load configuration
  preferences.begin("ai-camera", false);
  WiFi.onEvent(WiFiEvent);
  loadDeviceConfiguration();

  // Connect or start config portal
  if (strlen(deviceConfig.ssid) > 0) {
    Serial.println("Saved WiFi found: " + String(deviceConfig.ssid));
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      String apiKeyStatus = (strlen(deviceConfig.openaiKey) > 0 && String(deviceConfig.openaiKey).startsWith("sk-")) ? "Configured" : "NOT SET";
      Serial.println("API Key: " + apiKeyStatus);
      setupApplicationServer();
      initializeFileCounters();

      // Start mDNS responder
      if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS started: http://" + String(MDNS_NAME) + ".local");
      } else {
        Serial.println("mDNS failed to start");
      }

      Serial.println("\n========================================");
      Serial.println("SYSTEM READY - Camera Mode");
      Serial.println("IP Address: " + WiFi.localIP().toString());
      Serial.println("mDNS: http://" + String(MDNS_NAME) + ".local");
      Serial.println("========================================\n");
    } else {
      startConfigMode();
    }
  } else {
    Serial.println("No saved WiFi credentials");
    startConfigMode();
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  checkResetButton();

  if (wpsRunning) { handleWPS(); }

  if (configMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (millis() - configStartTime > CONFIG_TIMEOUT) {
      Serial.println("Config timeout, restarting...");
      ESP.restart();
    }
  } else {
    server.handleClient();
    // WiFi health check every 5 seconds
    if (millis() - lastWiFiCheck >= 5000) {
      lastWiFiCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi check: disconnected");
      }
    }
    // Reconnection logic
    if (WiFi.status() != WL_CONNECTED && shouldReconnect) {
      if (millis() - lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
        attemptReconnect();
      }
    }
  }
  yield();
}
