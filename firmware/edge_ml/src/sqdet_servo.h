#pragma once

#include <Arduino.h>

/** Reserved / unsupported on DFR1154 camera builds (use UART→ACEBOTT). */
#define SQDET_SERVO_BACKEND_GPIO 0

/** Adafruit PCA9685 on camera I2C (requires EDGE_ML_SERVO_ENABLE_PCA9685; conflicts with ACEBOTT UART). */
#define SQDET_SERVO_BACKEND_PCA9685 1

/** Pan/spray on ACEBOTT over UART (GPIO 43/44). Default for squirrel_deterrent. */
#define SQDET_SERVO_BACKEND_UART 2

struct SqdetServoConfig {
  uint8_t backend;

  // GPIO backend (SQDET_SERVO_BACKEND_GPIO)
  int gpio_pan_pin;
  int gpio_spray_pin;

  // PCA9685 backend (SQDET_SERVO_BACKEND_PCA9685)
  int pca9685_sda;
  int pca9685_scl;
  uint8_t pca9685_addr;
  uint8_t pca9685_ch_pan;
  uint8_t pca9685_ch_spray;
  /** PCA9685 pulse tick range for 0–180° (tune per servo if needed). */
  uint16_t pca9685_pulse_min;
  uint16_t pca9685_pulse_max;

  int spray_rest_deg;
  int spray_squeeze_deg;
  uint32_t spray_squeeze_ms;
};

/**
 * Initialize the selected servo backend and move pan/spray to rest positions.
 * @return false on PCA9685 I2C / init failure
 */
bool sqdet_servo_init(const SqdetServoConfig* cfg, int pan_center_deg);

/** Command pan/yaw servo (0–180 degrees). */
void sqdet_servo_set_pan(int degrees);

/** Squeeze spray servo briefly, then return to rest (blocks for spray_squeeze_ms). */
void sqdet_servo_trigger_spray();

/**
 * Compute a pan angle that nudges toward a target's normalized horizontal
 * position in-frame. Shared by the local-ML tracking loop and the cloud-AI
 * review/confirm flow so both "centering" paths use identical math.
 *
 * @param pan_center_deg calibrated center/rest pan angle (0-180)
 * @param aim_kp          gain: degrees of pan per unit of normalized error
 * @param norm_cx         normalized horizontal target position (0..1, 0.5 = center)
 * @return pan angle clamped to 0-180
 */
int sqdet_servo_compute_pan(int pan_center_deg, float aim_kp, float norm_cx);
