# Squirrel deterrent example — how to

This sketch uses the **`edge_ml` Arduino library** in [`../../`](../..) (camera, dataset folders, dashboard, ML API stub, ACEBOTT UART). It is a **reference** for learning and prototyping, not a turnkey product.

## 1. Install the library

- **Option A:** Copy or symlink `firmware/edge_ml` into your Arduino sketchbook `libraries` folder as `edge_ml`.
- **Option B:** Arduino IDE → *Sketch → Include Library → Add .ZIP Library…* (zip the `edge_ml` folder).

Restart the IDE, then open **File → Examples → edge_ml → squirrel_deterrent**.

**ACEBOTT companion:** open [`../acebott_sensor_node/acebott_sensor_node.ino`](../acebott_sensor_node/acebott_sensor_node.ino) on your ACEBOTT ESP32 (separate USB flash).

**Library for ACEBOTT sketch:** Arduino Library Manager → **Adafruit PWM Servo Driver Library** (PCA9685 on the ACEBOTT).

Default **`servo_backend_config.h`** on the camera uses **UART → ACEBOTT** (camera does not talk to PCA9685 directly).

## 2. WiFi secrets (dashboard on your LAN)

```bash
cd firmware/edge_ml/examples/squirrel_deterrent
cp wifi_secrets.h.example wifi_secrets.h
```

Edit `wifi_secrets.h` with your LAN SSID and password.  
`wifi_secrets.h` is gitignored.

Set `#define ENABLE_HTTP_DEBUG 0` in the `.ino` if you do not need the web UI (dataset capture and labeling then require another path).

## 3. Board settings (match main curriculum firmware)

- **Board:** ESP32S3 Dev Module  
- **USB CDC On Boot:** Enabled  
- **Flash size:** 16 MB  
- **Partition scheme:** 16M Flash (3MB APP / 9.9MB FATFS)  
- **PSRAM:** OPI PSRAM  

## 4. SD card preparation (4 GB is fine)

The DFR1154 has a **built-in microSD slot** on the back of the board. You do **not** need an external card reader module.

| Step | Action |
|------|--------|
| 1 | Format the card as **FAT32** on your computer (Mac: Disk Utility → MS-DOS FAT; Windows: Format → FAT32). |
| 2 | Leave the card **empty** — no folders or files required. |
| 3 | Insert into the camera board before power-on. |
| 4 | Flash `squirrel_deterrent`; Serial Monitor should print **`SD_MMC OK — /dataset/* ready`**. |

On first boot the firmware creates:

```
/dataset/inbox/          ← new captures (timer, PIR/motion, manual)
/dataset/squirrel/       ← you tag positives here (dashboard)
/dataset/not_squirrel/   ← negatives
/dataset/uncertain/      ← review later
/images/                 ← legacy manual capture path
/triggers/               ← spray event snapshots (when armed + ML hits)
```

To export for Edge Impulse: pull the card → USB reader on PC → upload **`squirrel`** and **`not_squirrel`** folders.

## 5. Wiring: camera UART + ACEBOTT PCA9685

DFR1154 **v1.1** only exposes **GPIO 43** and **44** on Gravity — used for **UART to the ACEBOTT only**. The camera never talks to the PCA9685 directly.

The **PCA9685 sits on the ACEBOTT** (local I2C). Command flow:

```
Dashboard pan/spray  →  camera (WiFi)  →  UART PAN/SPRAY  →  ACEBOTT  →  I2C PCA9685  →  servos
PIR motion           →  ACEBOTT         →  UART MOTION     →  camera captures to SD
```

### UART (camera ↔ ACEBOTT)

| Camera Gravity | ACEBOTT |
|----------------|---------|
| **43 (T / TX)** | **RX** (`LINK_RX_PIN`, default 16) |
| **44 (R / RX)** | **TX** (`LINK_TX_PIN`, default 17) |
| **- (GND)** | **GND** |

### PCA9685 (on ACEBOTT — edit pins in `acebott_sensor_node.ino`)

SDA and SCL only let the ACEBOTT *talk* to the chip. Servos will not move unless **V+** is powered and **GND** is common.

| PCA9685 | ACEBOTT (QA007 ESP32 Max defaults) |
|---------|---------------------|
| SDA | GPIO **21** (pin labeled SDA) |
| SCL | GPIO **22** (pin labeled SCL) |
| VCC | **3.3 V** (chip logic — this is why I2C can ACK with no servo motion) |
| GND | common GND with ACEBOTT + camera + servo pack |
| V+ / terminal block | external **5–6 V** for the servos — **required** |
| OE | GND (or leave open if the board already pulls it down) |
| Channel **0** | pan servo |
| Channel **1** | spray servo |

Each 3-pin channel header, from the **board edge inward**: **GND | PWM (signal) | V+**.  
Servo wires are usually brown/black = GND, orange/yellow = signal, red = V+. Plugging the connector in backwards is a common “ACK but no motion” failure.

**PIR:** `PIR_PIN` default GPIO **25** on ACEBOTT.

Power the ACEBOTT from its own USB or supply. Tie **GND** common: camera, ACEBOTT, PCA9685, and external servo supply.

On ACEBOTT boot the sketch now **sweeps both servos** (0° → 90° → 180° → 90°). If that does not move them, the problem is power/headers/OE — not UART. You can also type `SWEEP` in the ACEBOTT Serial Monitor.

### Untethered / battery power

A simple, classroom-safe setup for a deployment that isn't near a computer:

| What | Power source |
|------|---------------|
| Camera board (ESP32-S3) | USB-C from a **rechargeable USB power bank** (5V) |
| ACEBOTT | USB from the **same power bank's second port** (or its own port/bank) |
| Servos (PCA9685 `V+`) | A **separate** 4x AA (or NiMH) pack, ~4.8–6V — do **not** share this with the camera/ACEBOTT logic rails |

Notes:
- The AA pack's `-` must still be wired into the same common **GND** as the camera, ACEBOTT, and PCA9685 — otherwise the PWM signal to the servos has no valid reference and they'll behave erratically. Only the `+`/power rails are kept separate; ground is always shared.
- Keeping servo power on its own battery (instead of sharing the power bank) protects the camera/WiFi from brownouts caused by servo current spikes — this is the same reasoning as the [top-level README's "Powering the board"](../../../../README.md#powering-the-board) section.
- Pick a power bank rated for **simultaneous dual-port output** (check its spec sheet — some cap total combined current lower than the sum of each port) and, ideally, one **without an aggressive low-current auto-shutoff**, since an idling ESP32 can draw less current than some power banks expect before they assume nothing's plugged in and cut off.
- Rough runtime: a 10,000 mAh power bank at ~5V/~85% conversion efficiency is roughly 6,000–6,500 mAh of usable 5V capacity. Split across the camera (~250–400 mA average with WiFi/streaming) and ACEBOTT (~100–200 mA idling), expect somewhere around 10–15 hours before a recharge — plan for a daily top-up, or size up to 20,000 mAh+ for multi-day runs.
- If deployed outdoors, house the power bank and AA pack in a weatherproof enclosure — USB-C and AA holders are not weather-sealed on their own.

Protocol @ 115200 baud:

- camera → ACEBOTT: `PING`, `PAN 90`, `SPRAY`
- ACEBOTT → camera: `HELLO` (every 3 s), `PONG`, `ACK PAN 90`, `ACK SPRAY`, `MOTION`

The dashboard badge **ACEBOTT UART: LINK UP** means the camera heard a reply in the last 5 seconds. **Ping ACEBOTT** / **Test spray** report whether an ACK came back. **Reflash both boards** after this change.

## 6. Dashboard (`http://<ip>/dash`)

Serial Monitor prints the IP after WiFi connects.

| Feature | What it does |
|---------|----------------|
| **Live stream** | Starts automatically; polls `/stream` every 250 ms |
| **Pan slider** | Yaw servo while watching the feed (UART → ACEBOTT) |
| **Test spray** | Pulses water servo once (works when disarmed); waits for ACEBOTT `ACK SPRAY` |
| **Ping ACEBOTT** | Sends `PING`, expects `PONG` — confirms the UART wiring before blaming servos |
| **Capture to dataset** | Saves JPEG to `/dataset/inbox/` |
| **Label dataset** | Browse inbox (or any folder), tag **Squirrel** / **Not squirrel** / **Uncertain** — files move on SD |
| **Timer capture** | Calibration → *Timer capture (ms)* — default 300000 (5 min); `0` = off |
| **Motion burst** | Saves N frames when ACEBOTT sends `MOTION` (default 3) |
| **Arm / Disarm** | ML spray path blocked when disarmed |
| **AI squirrel review** | Cloud AI (OpenAI/Anthropic) verdict on a captured photo, with confirm/reject buttons — see below |

Sorted dataset folders are ready for Edge Impulse upload at any time (label manually, via AI review, or both).

## 7. AI squirrel review (cloud AI + human-in-the-loop)

This is the **"is that a squirrel?"** workflow: a photo is captured on motion, on the timer, or on demand, sent to
OpenAI or Anthropic's vision API, and shown on the dashboard for you to confirm before (or instead of) spraying.

1. On `/dash`, open **AI squirrel review**.
2. Pick a **Provider** (OpenAI or Anthropic), paste an **API key** (get one from platform.openai.com or
   console.anthropic.com), and click **Save AI settings**. Keys are stored in NVS on the device — never in source
   files or git.
3. Check **Enable cloud AI calls** (off by default — no HTTPS calls happen, no cost, until you opt in) and Save again.
4. Click **Capture & evaluate now** to test. Within a few seconds you'll see the photo, the AI's verdict
   (squirrel / not squirrel), a confidence percent, and its short reasoning.
5. Use the live stream + pan slider (above, in "Live view") to manually aim if you want to look around first, then:
   - **Confirm squirrel — center & spray**: nudges the pan servo toward the AI's guessed position and, if
     **armed**, triggers the spray servo. If disarmed, it centers only.
   - **Not a squirrel** / **Uncertain**: no servo action.
   - Every choice archives the photo (with the AI's notes) into `/dataset/squirrel`, `/not_squirrel`, or
     `/uncertain` — the same folders the manual "Label dataset" section uses, and what you'll later upload to
     Edge Impulse to stop needing the cloud call at all.
6. Optional — **Auto-confirm above threshold**: once you trust the AI on your setup, check this box and set a
   **Confidence threshold** (e.g. `0.75`). Captures at or above that confidence skip the dashboard click entirely
   (center + archive + spray, if armed); anything less confident still waits for you.

Motion (`ACEBOTT` → `MOTION`) and the timer capture both trigger one AI-reviewed photo each; the motion **burst**
(raw training frames, see above) still runs independently and is not cloud-evaluated.

## 8. Spray angles and PCA9685 tuning

Tune in `acebott_sensor_node.ino`: `SPRAY_REST_DEG`, `SPRAY_SQUEEZE_DEG`, `SPRAY_SQUEEZE_MS`, and `PCA9685_PULSE_MIN` / `MAX` if a servo does not reach full travel.

**Do not** set `SQDET_SERVO_BACKEND_PCA9685` on the camera unless the PCA9685 is wired to the camera’s Gravity port (43/44 I2C) — that conflicts with UART to the ACEBOTT. With the ACEBOTT + PCA9685 layout, keep **`SQDET_SERVO_BACKEND_UART`** on the camera.

## 9. Edge ML (Edge Impulse)

Default **`edge_ml_api.cpp` is a stub** — no detections until you integrate a trained model.

1. Collect and tag images via the dashboard (or timer + PIR bursts).
2. Upload **`/dataset/squirrel`** and **`/dataset/not_squirrel`** to Edge Impulse.
3. Train FOMO (for pan centroids) or classification.
4. Export Arduino library and wire into `edge_ml_run_fb()`.

See [`../../README.md`](../../README.md).

## 10. If the dashboard loads but there is no image / servos do not move

**No live image**

- Reflash the camera after pulling these library fixes (`/stream` now sends a real JPEG, same as the main curriculum firmware).
- Serial should print `dfr1154_camera_init: PSRAM OK` then a warmup size and `dfr1154_camera_init: OK`. If you see `no PSRAM` or `failed: 0x…`, set **PSRAM: OPI PSRAM** and **USB CDC On Boot: Enabled**.
- `cam_hal: FB-OVF` means a framebuffer overflowed (camera produced a frame nobody read). Reflash this library revision — camera starts after WiFi, uses one framebuffer, and `/stream` releases the frame before the HTTP send.
- The live view starts on its own. A red line under the image means `/stream` is not returning a JPEG.

**Servos do not move on Test spray, pan slider, or Confirm squirrel**

- Those commands go **camera UART → ACEBOTT → PCA9685**. The camera never drives the servos itself.
- Camera Serial should print `[acebott] TX PAN …` or `[acebott] TX SPRAY` when you use the dashboard.
- ACEBOTT Serial should print `UART line: [PAN 90]` / `UART line: [SPRAY]`. If the camera TX line appears but ACEBOTT does not, swap TX/RX or check common GND.
- ACEBOTT must print `PCA9685 OK` at boot, then a **self-test sweep** of ch0 and ch1. `ERROR: pca.begin() failed` / I2C scan empty means SDA/SCL/VCC (GPIO 21/22) are wrong.
- **UART `ACK SPRAY` with no motion** means I2C reached the chip but the servos have no power or a bad header: put **5–6 V on PCA9685 V+**, common GND, OE to GND, servo GND-PWM-V+ (not reversed). Type `SWEEP` in the ACEBOTT Serial Monitor.
- **Motion alone does not move servos** — it only captures. Servos move from the pan slider, **Test spray**, or **Confirm squirrel** (spray only if **armed**).
- On-device Edge ML is still a stub, so the automatic “detect → aim → spray” loop will not fire until an Edge Impulse model is wired in. Use cloud AI review (or the manual buttons) until then.

## 11. Safety

- Water spray can surprise people and pets; use **cooldowns**, aim away from paths, and comply with local rules.
- This example has **no HTTPS or login**; use only on a **trusted LAN**.
- The AI squirrel review dashboard section stores your OpenAI/Anthropic API key in device NVS over that same
  unencrypted LAN connection — fine for a trusted home network, not for anything public-facing.
- Cloud vision calls use `WiFiClientSecure::setInsecure()` (TLS encryption, no certificate validation) for the
  same memory-constraint reasons documented in `firmware/standalone/.../ssl_validation.h`.
