# Edge ML support library — DFR1154 (ESP32-S3 AI CAM)

This folder is an **Arduino user library** that complements the main **OpenAI / web UI** firmware in `firmware/standalone/`. It provides:

| Piece | Role |
|--------|------|
| `dfr1154_camera_pins.h`, `dfr1154_camera.*` | Same OV2640 + SD pin map and init as `ESP32_AI_Camera_v3_2_5.ino` (JPEG, SVGA, mirror/flip). |
| `dfr1154_http_debug.*` | `WebServer` helpers: `dfr1154_http_serve_stream` / `serve_capture`, full **minimal** register (`/` + stream + capture), or **stream/capture only** for use with the dashboard. |
| `dfr1154_dashboard.*` | **`/dash`** — live stream, pan slider, spray test, AI squirrel review, dataset review/tagging, calibration, arm/disarm; **`/api/*`** JSON. |
| `dfr1154_dataset.*` | SD folders `/dataset/*`, capture, list, tag, serve images, JSON metadata sidecars. |
| `cloud_ml_api.*` | On-device HTTPS calls to **OpenAI (GPT-4o)** or **Anthropic (Claude)** vision — one JPEG in, `{squirrel, confidence, cx, cy, reason}` out. Settings + API keys live in NVS (set from the dashboard, never committed to source). |
| `dfr1154_cloud_review.*` | Orchestrates the "AI squirrel review" workflow: capture → cloud classify → dashboard confirm (or auto-confirm above a threshold) → center pan + spray + archive to `/dataset/<label>` with AI notes. `/api/cloud/*` routes. |
| `dfr1154_acebott_link.*` | UART on GPIO 43/44 ↔ ACEBOTT (`MOTION` in, `PAN`/`SPRAY` out). |
| `sqdet_servo.*` | Pan/spray: **UART (ACEBOTT)**, **PCA9685** (I2C), or **GPIO**. See `servo_backend_config.h`. |
| `edge_ml_api.*` | **Stub** on-device inference hook: `edge_ml_init()`, `edge_ml_run_fb()` → `EdgeMlResult`. Replace with **Edge Impulse** (recommended) once you have enough labeled data, so you don't have to call the cloud AI for every frame. |

**Examples:** `examples/squirrel_deterrent/` (dashboard + dataset + AI review + deterrent), `examples/acebott_sensor_node/` (PIR + servos for UART link).

---

## AI squirrel review (cloud vision, human-in-the-loop)

A photo is captured whenever ACEBOTT reports `MOTION`, when the capture timer elapses, or when you press
**"Capture & evaluate now"** on the dashboard. If cloud AI is enabled (`dfr1154_dashboard.h` → *AI squirrel review*
section), that photo is sent to OpenAI or Anthropic's vision API with a fixed prompt asking for a small JSON verdict:
`{"squirrel": bool, "confidence": 0-1, "cx": 0-1, "cy": 0-1, "reason": "..."}` — `cx`/`cy` are the model's best-effort
normalized position of the squirrel in-frame, used to nudge the pan servo toward it.

- **Human-in-the-loop by default:** with *Auto-confirm* off, every capture waits on the dashboard for you to click
  **Confirm squirrel** / **Not a squirrel** / **Uncertain**. Confirming "squirrel" centers the pan servo on the AI's
  guessed position and triggers the spray servo — but only if the dashboard is **armed** (same safety gate as the
  rest of the sketch).
- **Auto mode:** turn on *Auto-confirm* and set a *confidence threshold*; captures at or above that confidence are
  centered + archived + sprayed immediately, with no dashboard click. Lower-confidence captures still wait for you.
- **Every decision — human or automatic — archives** the JPEG into `/dataset/squirrel`, `/not_squirrel`, or
  `/uncertain`, alongside a `.json` sidecar with the AI's raw verdict, source (`motion`/`timer`/`manual`), and who
  confirmed it. This is exactly the labeled data an Edge Impulse model needs, so the project can eventually stop
  calling the cloud for every frame (see `edge_ml_api.*` above).
- **API keys** are entered on the dashboard and stored in NVS (ESP32 flash), never in source files or git.
- **Cost/safety:** cloud calls only happen when *Enable cloud AI calls* is checked (off by default) — motion/timer
  capture behaves exactly as before until you opt in.

See `examples/squirrel_deterrent/HOWTO.md` for the walkthrough.

---

## Why Edge Impulse (and not only Teachable Machine)

- **Edge Impulse** targets **microcontrollers**: Arduino library export, int8 quantization, and **object detection** (e.g. FOMO) that can yield a **centroid** for aiming.
- **Teachable Machine** is excellent for quick **image classification** in the browser; getting a robust **ESP32** deployment path and **bounding-box-style** outputs usually takes more manual work.

You can still document a **Teachable Machine → TFLite** path later as an advanced lab; the code integration point remains `edge_ml_run_fb()`.

---

## Install and build

1. Copy or symlink `firmware/edge_ml` to Arduino `libraries/edge_ml` (or Add .ZIP Library).
2. Board settings: same DFR1154 hardware as the standalone firmware — full settings table (Board, Flash, Partition, PSRAM, Upload Speed, etc.) in the main [README](../../README.md#for-advanced-users--instructors) "For Advanced Users / Instructors" section.
3. Dependencies: **ESP32 board package** (includes `esp_camera`, `SD_MMC`, `WebServer`, `WiFi`). **PCA9685 mode** needs Adafruit PWM Servo Driver Library; **UART/ACEBOTT mode** (default in squirrel example) does not.

---

## Integrating Edge Impulse (outline)

These steps are intentionally high level; follow Edge Impulse’s current **ESP32** deployment docs for exact file names.

1. In Edge Impulse Studio, build an **Impulse** (e.g. image data → DSP block → classification or FOMO).
2. **Deployment → Arduino library** (or C++ export). Install the generated ZIP as a second library, or merge generated sources under e.g. `src/ei_generated/` (watch repo size and licenses).
3. In `edge_ml_api.cpp`:
   - Call the generated **`setup()` / `begin()`** (names vary by export) from `edge_ml_init()`.
   - In `edge_ml_run_fb()`:
     - If the impulse expects **raw RGB**, decode or subsample the JPEG (JPEG is what `esp_camera` returns by default in this repo). Many tutorials use **Signal from image** with RGB888 or grayscale — match your block.
     - Fill `EdgeMlResult`: set `target_present`, `confidence`, and `norm_cx` / `norm_cy` (0–1, origin top-left).
4. Reduce **frame size** or **JPEG quality** in `dfr1154_camera.cpp` if you need more FPS or RAM headroom.

**Git:** Large weight files are easy to ignore — see repo [`.gitignore`](../../.gitignore) patterns for `ei_generated/` and `*.tflite`.

---

## Camera sharing with HTTP

`esp_camera` allows **one framebuffer user at a time**. Rules:

- Call `server.handleClient()` **before** `esp_camera_fb_get()` in `loop()`, or ensure you never hold a frame across `handleClient()`.
- Do not block for long periods while holding a frame (bad for stream and watchdog).

The squirrel example follows this order for the local-ML loop. The **cloud AI review** path copies the captured
JPEG into PSRAM, returns the frame right away, and runs the HTTPS call (several seconds) in a background FreeRTOS
task. `loop()` applies the verdict in `dfr1154_cloud_review_service()`, so the stream, dashboard buttons, pan and spray
stay responsive while the AI works. A photo taken while a call is running waits in a one-slot queue (newest wins)
and is evaluated next.

---

## Files you may add (not shipped)

| Path | Purpose |
|------|---------|
| `src/ei_generated/` | Vendored Edge Impulse Arduino export (optional). |
| `*.tflite` | If you load a flatbuffer model manually. |

---

## License / curriculum note

The **stub** code is part of the CIJE curriculum repo. Edge Impulse–generated sources follow **their** license; verify before redistributing binaries.
