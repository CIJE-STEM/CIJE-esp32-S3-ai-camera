#pragma once

#include <Arduino.h>

/** Callback when ACEBOTT sends a MOTION line over UART. */
typedef void (*AcebottMotionCallback)(void);

struct AcebottLinkStatus {
  bool begun;
  /** True if any line was received in the last 5 seconds. */
  bool heard_recent;
  uint32_t last_rx_ms;
  uint32_t last_tx_ms;
  uint32_t last_ack_ms;
  uint32_t rx_ago_ms;
  uint32_t ack_ago_ms;
  uint32_t rx_count;
  uint32_t tx_count;
  uint32_t ack_count;
  char last_rx[32];
  char last_tx[32];
};

/**
 * UART link on DFR1154 Gravity connector: TX=GPIO43, RX=GPIO44 @ 115200.
 * ACEBOTT TX → camera 44, ACEBOTT RX → camera 43, common GND.
 */
void dfr1154_acebott_link_init();

/** Call often from loop(); parses incoming lines (MOTION / PONG / ACK / HELLO). */
void dfr1154_acebott_link_poll(AcebottMotionCallback on_motion);

/** Drain RX for up to `wait_ms` (used after PING / SPRAY to catch an ACK). */
void dfr1154_acebott_link_pump(uint32_t wait_ms);

/** Send pan angle to ACEBOTT servo node: "PAN <deg>". */
void dfr1154_acebott_link_send_pan(int degrees);

/** Send spray command: "SPRAY". */
void dfr1154_acebott_link_send_spray();

/** Send "PING" and expect "PONG". */
void dfr1154_acebott_link_send_ping();

void dfr1154_acebott_link_get_status(AcebottLinkStatus* out);
