#pragma once

#include <WebServer.h>

/**
 * Serve one JPEG frame from the camera (for GET /stream).
 * Do not hold another framebuffer while this runs.
 */
void dfr1154_http_serve_stream(WebServer& server);

/**
 * Save one frame to /images/CAP_n.jpg and send JSON {success,filename}.
 */
void dfr1154_http_serve_capture(WebServer& server);

/**
 * Register minimal debug routes on an existing WebServer:
 *   GET /       — small HTML with live preview
 *   GET /stream — single JPEG frame
 *   GET /capture — save JPEG to /images/CAP_n.jpg
 *
 * Requires SD_MMC mounted and /images directory for /capture.
 */
void dfr1154_http_debug_register(WebServer& server);

/** Register only /stream and /capture (use with dashboard that owns GET /). */
void dfr1154_http_debug_register_stream_capture_only(WebServer& server);

void dfr1154_http_debug_reset_capture_counter();
