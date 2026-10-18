# Minimal calibration + status dashboard (outline)

**Status:** Implemented in **`src/dfr1154_dashboard.*`** and the **`squirrel_deterrent`** example (`http://<ip>/dash`). This document remains the design summary.

Goal: extend the squirrel deterrent / `edge_ml` HTTP surface with a **small single-page UI** and **JSON endpoints** so you can aim the camera, **tune aiming**, **see live detection status**, and **arm/disarm** spraying without reflashing.

---

## 1. Principles

- **One HTML page** (e.g. `GET /dash` or replace/enhance `GET /`) — no separate server; still ESP32 `WebServer`.
- **Read-heavy status** via **`GET /api/status`** (JSON) polled every 250–500 ms while the dashboard is open.
- **Calibration** via **`POST /api/cal`** with form fields or JSON body (or separate `GET /api/cal?aim_kp=…` for simplicity in classrooms — POST preferred for writes).
- **Persistence:** v1 can use **ESP32 NVS** (small key/value) so reboot keeps pan center, gains, thresholds, and `armed` default. Optional: “factory reset” button → clear NVS keys.
- **Safety:** explicit **`armed`** boolean — when false, run ML and pan (optional) but **never** call `trigger_spray()`. Default **disarmed** after flash until user arms from dashboard (or Serial).

---

## 2. Runtime status (`GET /api/status`)

JSON fields (minimal):

| Field | Meaning |
|--------|--------|
| `armed` | If false, no spray (dry run). |
| `wifi_ok` / `ip` | Connection summary. |
| `sd_ok` | SD mounted. |
| `last_ml_ms` | Time since last inference tick (health). |
| `target_present` | From last `EdgeMlResult`. |
| `confidence` | Last model confidence (0–1). |
| `norm_cx`, `norm_cy` | Last centroid (0–1). |
| `pan_deg` | Commanded pan angle (last write to servo). |
| `spray_cooldown_remaining_ms` | Time until next spray allowed. |
| `in_deadband` | Whether horizontal error is inside center window. |

Optional later: `fps_infer`, `heap_free`, model name string.

---

## 3. Calibration parameters (tunable)

Move from compile-time `#define` to **runtime** (with sensible defaults matching today’s sketch):

| Key | Role |
|-----|------|
| `pan_center` | Servo “straight ahead” trim (default 90). |
| `aim_kp` | Degrees per normalized horizontal error. |
| `center_deadband` | Half-width around 0.5 for `norm_cx` to count as “centered”. |
| `confidence_threshold` | Minimum confidence to track / consider fire. |
| `spray_cooldown_ms` | Between spray events. |
| `ml_interval_ms` | Max inference rate (trade FPS vs CPU). |
| `track_without_arm` | If true, pan follows target even when disarmed; if false, pan only when armed. |

**Apply:** `POST /api/cal` with JSON body `{ "aim_kp": 72, ... }` — partial updates OK.

**Response:** `{ "ok": true }` or `{ "ok": false, "error": "..." }`.

---

## 4. Arm / disarm

- `POST /api/arm` body `{ "armed": true }` or `false`.
- Status line on dashboard: big **ARMED / DISARMED** indicator; spray block clearly disabled when disarmed.

---

## 5. Minimal dashboard UI (sections)

1. **Live stream** — same as today: `<img>` refreshing `/stream?…` every ~150 ms.
2. **Status panel** — poll `/api/status`: target yes/no, confidence bar, cx/cy (text or simple crosshair description), pan angle, cooldown timer.
3. **Calibration form** — number inputs or sliders for parameters above; **Save** → `POST /api/cal`.
4. **Arm toggle** — checkbox or button → `POST /api/arm`.
5. **Capture** — button → `GET /capture` (existing) to verify stills on SD.
6. **Optional:** “**Pan test**” — slider 0–180 + **Send** → immediate `panServo.write` for mechanical trim (does not need NVS until user hits Save).

---

## 6. Firmware structure (when implemented)

- **`squirrel_deterrent.ino`:** global `CalibrationState` struct + `volatile` or mutex-protected **last** `EdgeMlResult` and timestamps updated in `loop()`; spray path checks `state.armed`.
- **New file (optional):** `dfr1154_cal_api.cpp` / `.h` — register `/api/status`, `/api/cal`, `/api/arm`, serve `/dash` HTML from PROGMEM (or extend `dfr1154_http_debug` with a flag `REGISTER_DASHBOARD`).
- **NVS namespace** e.g. `sqdet` with keys `pan_center`, `aim_kp`, … load in `setup()`, save on successful `POST /api/cal`.

---

## 7. Animal-agnostic testing

- **No UI change** required for “squirrel vs any animal” — that is entirely **model + class labels** in Edge Impulse.
- Dashboard only shows **generic** `target_present`, `confidence`, `norm_cx/y`. Optional: add `label` string to `EdgeMlResult` when the model exports class name.

---

## 8. Implementation order (suggested)

1. Add struct + in-RAM only + `/api/status` + arm gate (no NVS).
2. Add `/api/cal` + `/api/arm` + wire loop to use struct.
3. Add minimal HTML `/dash` with stream + status poll + form.
4. Add NVS load/save + factory reset.
5. Polish: pan test slider, validation ranges, heap warnings in status.

---

## 9. Out of scope for “minimal”

- HTTPS, login, multi-user.
- Overlay bounding boxes on MJPEG (heavy on ESP32); use numeric cx/cy + side stream instead.
- OTA from dashboard.
