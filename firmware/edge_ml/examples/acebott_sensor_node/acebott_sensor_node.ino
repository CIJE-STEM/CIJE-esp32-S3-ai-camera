/**
 * ACEBOTT ESP32 sensor + PCA9685 servo node for DFR1154 AI camera.
 *
 * The camera has only GPIO 43/44 free — it sends PAN/SPRAY over UART.
 * This board runs PIR and talks I2C to a PCA9685 for pan + spray servos.
 *
 * --- UART to camera (Gravity on DFR1154 v1.1) ---
 *   ACEBOTT RX  <-  camera GPIO 43 (T / TX)
 *   ACEBOTT TX  ->  camera GPIO 44 (R / RX)
 *   GND common
 *
 * --- PCA9685 (on ACEBOTT I2C — NOT on the camera) ---
 *   SDA  -> ACEBOTT GPIO 21 (labeled SDA)
 *   SCL  -> ACEBOTT GPIO 22 (labeled SCL)
 *   VCC  -> ACEBOTT 3.3 V (chip logic only — I2C can work with just this)
 *   GND  -> common with ACEBOTT + camera + servo supply
 *   V+   -> external 5–6 V  *** required or servos will not move ***
 *   OE   -> GND (or leave unconnected if the board already pulls it down)
 *
 *   Each channel header (from the board edge inward): GND | PWM | V+
 *   Pan   -> channel 0
 *   Spray -> channel 1
 *
 * Install: Adafruit PWM Servo Driver Library (Library Manager).
 *
 * Protocol @ 115200 baud (newline-terminated):
 *   ACEBOTT -> camera: HELLO, PONG, ACK PAN <deg>, ACK SPRAY, MOTION
 *   camera -> ACEBOTT: PING, PAN <0-180>, SPRAY
 *
 * USB Serial extras (type in ACEBOTT Serial Monitor, newline):
 *   SWEEP   — move both servos 0→90→180→90 so you can see wiring/power
 *   SPRAY   — same pulse as the camera command
 *   PAN 90  — set pan angle
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// UART link to DFR1154 camera — use UART2 (Serial2); GPIO 16/17 are its usual pins.
// Wire SIGNAL pins only (GVS "S"), not the 5V "V" pins.
#define LINK_RX_PIN 16
#define LINK_TX_PIN 17

// PIR motion sensor (active HIGH)
#define PIR_PIN 25

// PCA9685 on ACEBOTT local I2C (QA007 ESP32 Max defaults: SDA=21 SCL=22)
#define PCA9685_SDA       21
#define PCA9685_SCL       22
#define PCA9685_ADDR      0x40
#define PCA9685_CH_PAN    0
#define PCA9685_CH_SPRAY  1
// Optional: wire PCA9685 OE to this GPIO and we will hold it LOW (outputs enabled).
// -1 = not connected (rely on the board's OE pull-down / jumper to GND).
#define PCA9685_OE_PIN    -1
// Pulse width in microseconds. 500–2500 is a wide 0–180° range for most analog servos.
#define PCA9685_US_MIN    500
#define PCA9685_US_MAX    2500
// Visible 0/90/180 sweep at boot so you can tell power + headers are right.
#define PCA9685_BOOT_WIGGLE 1

#define SPRAY_REST_DEG    90
#define SPRAY_SQUEEZE_DEG 125
#define SPRAY_SQUEEZE_MS  400

#define PIR_COOLDOWN_MS 4000

static Adafruit_PWMServoDriver pca(PCA9685_ADDR, Wire);
static bool pca_ok;

static char line_buf[32];
static size_t line_len;
static char usb_buf[32];
static size_t usb_len;
static uint32_t last_pir_ms;
static uint32_t last_hello_ms;
static int pan_deg = 90;

static void link_reply(const char* line) {
  Serial2.print(line);
  Serial2.print("\r\n");
  Serial.printf("UART TX: %s\n", line);
}

static uint16_t deg_to_us(int degrees) {
  degrees = constrain(degrees, 0, 180);
  return (uint16_t)map(degrees, 0, 180, PCA9685_US_MIN, PCA9685_US_MAX);
}

static void pca_write_deg(int channel, int degrees) {
  degrees = constrain(degrees, 0, 180);
  uint16_t us = deg_to_us(degrees);
  pca.writeMicroseconds((uint8_t)channel, us);
  Serial.printf("  PWM ch%d -> %d deg (%u us)\n", channel, degrees, (unsigned)us);
}

static void set_pan(int degrees) {
  pan_deg = constrain(degrees, 0, 180);
  if (pca_ok) {
    pca_write_deg(PCA9685_CH_PAN, pan_deg);
  }
}

static void trigger_spray() {
  if (!pca_ok) return;
  Serial.printf("SPRAY pulse ch%d: %d -> %d -> %d\n",
                PCA9685_CH_SPRAY, SPRAY_REST_DEG, SPRAY_SQUEEZE_DEG, SPRAY_REST_DEG);
  pca_write_deg(PCA9685_CH_SPRAY, SPRAY_SQUEEZE_DEG);
  delay(SPRAY_SQUEEZE_MS);
  pca_write_deg(PCA9685_CH_SPRAY, SPRAY_REST_DEG);
}

static void sweep_channel(int channel, const char* name) {
  Serial.printf("Sweep %s (ch%d): 0 -> 90 -> 180 -> 90\n", name, channel);
  pca_write_deg(channel, 0);
  delay(500);
  pca_write_deg(channel, 90);
  delay(500);
  pca_write_deg(channel, 180);
  delay(500);
  pca_write_deg(channel, 90);
  delay(300);
}

static void sweep_both() {
  if (!pca_ok) {
    Serial.println("SWEEP skipped — PCA9685 not ready");
    return;
  }
  sweep_channel(PCA9685_CH_PAN, "pan");
  sweep_channel(PCA9685_CH_SPRAY, "spray");
  pca_write_deg(PCA9685_CH_SPRAY, SPRAY_REST_DEG);
}

static void i2c_scan() {
  Serial.printf("I2C scan on SDA=%d SCL=%d:\n", PCA9685_SDA, PCA9685_SCL);
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X%s\n", addr,
                    addr == PCA9685_ADDR ? "  <- PCA9685 expected" : "");
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  no devices — SDA/SCL/VCC/GND to the PCA9685 are wrong, or OE/VCC unpowered");
  }
}

static bool pca_init() {
#if PCA9685_OE_PIN >= 0
  pinMode(PCA9685_OE_PIN, OUTPUT);
  digitalWrite(PCA9685_OE_PIN, LOW);  // OE is active-LOW: LOW = PWM outputs enabled
#endif

  Wire.begin(PCA9685_SDA, PCA9685_SCL);
  Wire.setClock(100000);
  delay(20);
  i2c_scan();

  if (!pca.begin()) {
    Serial.printf("ERROR: pca.begin() failed at 0x%02X — chip not responding\n", PCA9685_ADDR);
    return false;
  }

  pca.setOscillatorFrequency(25000000);
  pca.setPWMFreq(50);
  delay(20);

  uint8_t prescale = pca.readPrescale();
  Serial.printf("PCA9685 OK — addr 0x%02X  prescale=%u (expect ~121 at 50 Hz)\n",
                PCA9685_ADDR, (unsigned)prescale);
  Serial.printf("  I2C SDA=%d SCL=%d | pan=ch%d spray=ch%d | pulse %u-%u us\n",
                PCA9685_SDA, PCA9685_SCL, PCA9685_CH_PAN, PCA9685_CH_SPRAY,
                (unsigned)PCA9685_US_MIN, (unsigned)PCA9685_US_MAX);
  Serial.println("  Servos need PCA9685 V+ at 5-6 V and common GND. SDA/SCL alone will ACK but not move.");
  return true;
}

static void handle_command(const char* line, bool from_camera) {
  if (strcmp(line, "PING") == 0) {
    if (from_camera) link_reply("PONG");
    else Serial.println("PONG");
    return;
  }
  if (strcmp(line, "SWEEP") == 0) {
    sweep_both();
    return;
  }
  if (strncmp(line, "PAN ", 4) == 0) {
    set_pan(atoi(line + 4));
    Serial.printf("PAN -> %d (PCA9685 ch %d)%s\n", pan_deg, PCA9685_CH_PAN,
                  pca_ok ? "" : " [PCA9685 missing]");
    if (from_camera) {
      char ack[24];
      snprintf(ack, sizeof(ack), "ACK PAN %d", pan_deg);
      link_reply(ack);
    }
    return;
  }
  if (strcmp(line, "SPRAY") == 0) {
    Serial.printf("SPRAY%s\n", pca_ok ? " (PCA9685)" : " [PCA9685 missing — ACK only]");
    if (from_camera) {
      link_reply("ACK SPRAY");
    }
    if (pca_ok) {
      trigger_spray();
    }
    return;
  }
}

static void poll_stream(Stream& s, char* buf, size_t& len, size_t cap, bool from_camera) {
  while (s.available()) {
    char c = (char)s.read();
    if (c == '\0') continue;
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        buf[len] = '\0';
        if (from_camera) {
          Serial.printf("UART line: [%s]\n", buf);
        } else {
          Serial.printf("USB cmd: [%s]\n", buf);
        }
        handle_command(buf, from_camera);
        len = 0;
      }
      continue;
    }
    if (c < 32 || c > 126) continue;
    if (len + 1 < cap) {
      buf[len++] = c;
    }
  }
}

static void poll_pir() {
  if (digitalRead(PIR_PIN) != HIGH) return;
  uint32_t now = millis();
  if (now - last_pir_ms < PIR_COOLDOWN_MS) return;
  last_pir_ms = now;
  Serial2.println("MOTION");
  Serial.println("PIR -> MOTION");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  // Pull-down so an unplugged PIR reads LOW. A floating pin reads random
  // HIGHs, which sends a stream of false MOTION events to the camera.
  pinMode(PIR_PIN, INPUT_PULLDOWN);

  Serial.println("\n=== ACEBOTT sensor + PCA9685 node ===");

  // RX=16 <- camera TX(43), TX=17 -> camera RX(44)
  Serial2.begin(115200, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);

  pca_ok = pca_init();
  if (pca_ok) {
#if PCA9685_BOOT_WIGGLE
    sweep_both();
#else
    set_pan(pan_deg);
    pca_write_deg(PCA9685_CH_SPRAY, SPRAY_REST_DEG);
#endif
  }

  Serial.println("ACEBOTT sensor node ready");
  Serial.printf("Camera UART: RX=%d TX=%d | PIR=%d\n", LINK_RX_PIN, LINK_TX_PIN, PIR_PIN);
  Serial.println("USB Serial: type SWEEP / SPRAY / PAN 90 then Enter");
  link_reply("HELLO");
  last_hello_ms = millis();
}

void loop() {
  poll_stream(Serial2, line_buf, line_len, sizeof(line_buf), true);
  poll_stream(Serial, usb_buf, usb_len, sizeof(usb_buf), false);
  poll_pir();
  uint32_t now = millis();
  if (now - last_hello_ms >= 3000) {
    last_hello_ms = now;
    link_reply("HELLO");
  }
}
