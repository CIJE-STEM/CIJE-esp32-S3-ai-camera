#include "sqdet_servo.h"
#include "dfr1154_acebott_link.h"

/**
 * Camera builds: UART → ACEBOTT only.
 *
 * Do not add Adafruit_PWMServoDriver or Arduino Servo includes here.
 * Arduino's library discovery ignores #if and will pull those packages into
 * every build; the stock Servo package breaks ESP32 core 3.x.
 *
 * PCA9685 lives on the ACEBOTT (see examples/acebott_sensor_node).
 */

static SqdetServoConfig s_cfg;
static bool s_ready;

static void write_pan(int degrees) {
  degrees = constrain(degrees, 0, 180);
  if (s_cfg.backend == SQDET_SERVO_BACKEND_UART) {
    dfr1154_acebott_link_send_pan(degrees);
  }
}

static void write_spray(int degrees) {
  (void)degrees;
  if (s_cfg.backend == SQDET_SERVO_BACKEND_UART) {
    dfr1154_acebott_link_send_spray();
  }
}

bool sqdet_servo_init(const SqdetServoConfig* cfg, int pan_center_deg) {
  s_ready = false;

  if (!cfg) {
    return false;
  }
  s_cfg = *cfg;

  if (s_cfg.backend == SQDET_SERVO_BACKEND_UART) {
    write_pan(pan_center_deg);
    s_ready = true;
    Serial.println("[sqdet_servo] UART backend — servos on ACEBOTT (PAN/SPRAY commands)");
    return true;
  }

  if (s_cfg.backend == SQDET_SERVO_BACKEND_GPIO) {
    Serial.println("[sqdet_servo] GPIO backend not supported on camera — use UART→ACEBOTT");
    return false;
  }

  if (s_cfg.backend == SQDET_SERVO_BACKEND_PCA9685) {
    Serial.println("[sqdet_servo] On-camera PCA9685 not in this build — use UART→ACEBOTT");
    return false;
  }

  Serial.printf("[sqdet_servo] unknown backend %u\n", (unsigned)s_cfg.backend);
  return false;
}

void sqdet_servo_set_pan(int degrees) {
  if (!s_ready) {
    return;
  }
  write_pan(degrees);
}

void sqdet_servo_trigger_spray() {
  if (!s_ready) {
    return;
  }
  if (s_cfg.backend == SQDET_SERVO_BACKEND_UART) {
    dfr1154_acebott_link_send_spray();
  }
}

int sqdet_servo_compute_pan(int pan_center_deg, float aim_kp, float norm_cx) {
  float err = norm_cx - 0.5f;
  int pan = (int)(pan_center_deg + err * aim_kp);
  return constrain(pan, 0, 180);
}
