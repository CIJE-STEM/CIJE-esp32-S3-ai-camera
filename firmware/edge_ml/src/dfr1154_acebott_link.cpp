#include "dfr1154_acebott_link.h"

#include <HardwareSerial.h>
#include <string.h>

// Use the core Serial1 object. A second HardwareSerial(1) on the same UART
// can silently fail to transmit (dashboard works, ACEBOTT never sees PAN/SPRAY).
static HardwareSerial& s_link = Serial1;
static char s_line[48];
static size_t s_line_len;
static bool s_begun;
static AcebottMotionCallback s_motion_cb;

static uint32_t s_last_rx_ms;
static uint32_t s_last_tx_ms;
static uint32_t s_last_ack_ms;
static uint32_t s_last_ping_ms;
static uint32_t s_rx_count;
static uint32_t s_tx_count;
static uint32_t s_ack_count;
static char s_last_rx[32];
static char s_last_tx[32];

static void remember_tx(const char* msg) {
  strncpy(s_last_tx, msg, sizeof(s_last_tx) - 1);
  s_last_tx[sizeof(s_last_tx) - 1] = '\0';
  s_last_tx_ms = millis();
  s_tx_count++;
}

static void remember_rx(const char* msg) {
  strncpy(s_last_rx, msg, sizeof(s_last_rx) - 1);
  s_last_rx[sizeof(s_last_rx) - 1] = '\0';
  s_last_rx_ms = millis();
  s_rx_count++;
}

static void tx_line(const char* line) {
  if (!s_begun) {
    Serial.printf("[acebott] TX ignored — link not started: %s\n", line);
    return;
  }
  s_link.print(line);
  s_link.print("\r\n");
  // Do not flush() — on ESP32-S3 GPIO 43/44 it can block forever and
  // the HTTP dashboard never gets served.
  remember_tx(line);
  Serial.printf("[acebott] TX %s\n", line);
}

void dfr1154_acebott_link_init() {
  s_line_len = 0;
  s_last_rx[0] = '\0';
  s_last_tx[0] = '\0';
  s_link.begin(115200, SERIAL_8N1, 44, 43);
  s_begun = true;
  s_last_ping_ms = millis();
  Serial.println("[acebott] UART link RX=44 TX=43 @ 115200 (Serial1)");
}

static void dispatch_line(AcebottMotionCallback on_motion) {
  if (s_line_len == 0) return;
  s_line[s_line_len] = '\0';
  remember_rx(s_line);
  Serial.printf("[acebott] RX [%s]\n", s_line);

  if (strcmp(s_line, "MOTION") == 0) {
    if (on_motion) on_motion();
  } else if (strcmp(s_line, "PONG") == 0 || strcmp(s_line, "HELLO") == 0 ||
             strncmp(s_line, "ACK ", 4) == 0) {
    s_last_ack_ms = millis();
    s_ack_count++;
  }

  s_line_len = 0;
}

static void drain_rx(AcebottMotionCallback on_motion) {
  while (s_link.available()) {
    char c = (char)s_link.read();
    if (c == '\0') continue;
    if (c == '\n' || c == '\r') {
      dispatch_line(on_motion);
      continue;
    }
    if (c < 32 || c > 126) continue;
    if (s_line_len + 1 < sizeof(s_line)) {
      s_line[s_line_len++] = c;
    }
  }
}

void dfr1154_acebott_link_poll(AcebottMotionCallback on_motion) {
  s_motion_cb = on_motion;
  drain_rx(on_motion);
}

void dfr1154_acebott_link_pump(uint32_t wait_ms) {
  uint32_t start = millis();
  while (millis() - start < wait_ms) {
    drain_rx(s_motion_cb);
    delay(5);
  }
}

void dfr1154_acebott_link_send_pan(int degrees) {
  degrees = constrain(degrees, 0, 180);
  char buf[20];
  snprintf(buf, sizeof(buf), "PAN %d", degrees);
  tx_line(buf);
}

void dfr1154_acebott_link_send_spray() {
  tx_line("SPRAY");
}

void dfr1154_acebott_link_send_ping() {
  tx_line("PING");
  s_last_ping_ms = millis();
}

void dfr1154_acebott_link_get_status(AcebottLinkStatus* out) {
  if (!out) return;
  uint32_t now = millis();
  out->begun = s_begun;
  out->last_rx_ms = s_last_rx_ms;
  out->last_tx_ms = s_last_tx_ms;
  out->last_ack_ms = s_last_ack_ms;
  out->rx_ago_ms = (s_last_rx_ms > 0) ? (now - s_last_rx_ms) : 999999;
  out->ack_ago_ms = (s_last_ack_ms > 0) ? (now - s_last_ack_ms) : 999999;
  out->heard_recent = (s_last_rx_ms > 0) && (out->rx_ago_ms < 5000);
  out->rx_count = s_rx_count;
  out->tx_count = s_tx_count;
  out->ack_count = s_ack_count;
  strncpy(out->last_rx, s_last_rx, sizeof(out->last_rx));
  out->last_rx[sizeof(out->last_rx) - 1] = '\0';
  strncpy(out->last_tx, s_last_tx, sizeof(out->last_tx));
  out->last_tx[sizeof(out->last_tx) - 1] = '\0';
}
