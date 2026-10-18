#pragma once

#include <WebServer.h>
#include "esp_camera.h"

/** Dataset folder roots on SD_MMC (created by dfr1154_dataset_init). */
#define DFR1154_DS_INBOX        "/dataset/inbox"
#define DFR1154_DS_SQUIRREL     "/dataset/squirrel"
#define DFR1154_DS_NOT_SQUIRREL "/dataset/not_squirrel"
#define DFR1154_DS_UNCERTAIN    "/dataset/uncertain"

/** Ensure /dataset/* folders exist. Call after SD_MMC.begin(). */
bool dfr1154_dataset_init();

/**
 * Save JPEG frame into /dataset/inbox/.
 * @param fb     camera framebuffer (JPEG)
 * @param source short tag for filename: manual, motion, timer, etc.
 * @param out_path optional buffer for full path written (e.g. 64 chars)
 * @param out_len  size of out_path
 * @return false on failure
 */
bool dfr1154_dataset_capture_fb(camera_fb_t* fb, const char* source,
                                char* out_path, size_t out_len);

/**
 * Move a JPEG (and its .json sidecar, if any) out of /dataset/inbox into the
 * folder for `label` ("squirrel", "not_squirrel", or "uncertain").
 * @param filename name only, e.g. "IMG_123_motion.jpg" (no path).
 */
bool dfr1154_dataset_archive_from_inbox(const char* filename, const char* label);

/**
 * Write (overwrite) the JSON metadata sidecar next to a dataset image.
 * @param jpg_path full path to the .jpg, e.g. "/dataset/inbox/IMG_123_motion.jpg".
 */
bool dfr1154_dataset_write_meta(const char* jpg_path, const String& json_body);

/** Read the JSON sidecar for a dataset image (full .jpg path). Empty string if none. */
String dfr1154_dataset_read_meta(const char* jpg_path);

/**
 * Call often from loop(). When folders cleared via POST /api/dataset/clear are
 * waiting to be emptied, starts a background task that deletes them, so the
 * purge never blocks loop() or HTTP.
 */
void dfr1154_dataset_service();

/** Register GET/POST routes for listing, viewing, tagging, capture, and clear. */
void dfr1154_dataset_register(WebServer& server);
