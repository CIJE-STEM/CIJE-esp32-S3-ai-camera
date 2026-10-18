#pragma once

/**
 * Change SQDET_SERVO_BACKEND to match your wiring.
 *
 * UART (default): ACEBOTT drives servos; camera uses GPIO 43/44 for serial only.
 * PCA9685: servos on I2C via Gravity port — cannot use ACEBOTT UART at the same time.
 */

#define SQDET_SERVO_BACKEND_GPIO     0
#define SQDET_SERVO_BACKEND_PCA9685  1
#define SQDET_SERVO_BACKEND_UART     2

#ifndef SQDET_SERVO_BACKEND
#define SQDET_SERVO_BACKEND SQDET_SERVO_BACKEND_UART
#endif

// --- PCA9685 (only when SQDET_SERVO_BACKEND_PCA9685) ---
#define SQDET_PCA9685_SDA       43
#define SQDET_PCA9685_SCL       44
#define SQDET_PCA9685_ADDR      0x40
#define SQDET_PCA9685_CH_PAN    0
#define SQDET_PCA9685_CH_SPRAY  1
#define SQDET_PCA9685_PULSE_MIN 150
#define SQDET_PCA9685_PULSE_MAX 600

// --- Direct GPIO (not available on DFR1154 v1.1 breakout) ---
#define SQDET_GPIO_PAN_PIN      19
#define SQDET_GPIO_SPRAY_PIN    20

// --- Spray lever angles (GPIO / PCA9685 backends) ---
#define SQDET_SPRAY_REST_DEG    90
#define SQDET_SPRAY_SQUEEZE_DEG 125
#define SQDET_SPRAY_SQUEEZE_MS  280

#include <sqdet_servo.h>

static inline SqdetServoConfig sqdet_servo_config_from_defines() {
  SqdetServoConfig cfg = {};
  cfg.backend = (uint8_t)SQDET_SERVO_BACKEND;
  cfg.gpio_pan_pin = SQDET_GPIO_PAN_PIN;
  cfg.gpio_spray_pin = SQDET_GPIO_SPRAY_PIN;
  cfg.pca9685_sda = SQDET_PCA9685_SDA;
  cfg.pca9685_scl = SQDET_PCA9685_SCL;
  cfg.pca9685_addr = SQDET_PCA9685_ADDR;
  cfg.pca9685_ch_pan = SQDET_PCA9685_CH_PAN;
  cfg.pca9685_ch_spray = SQDET_PCA9685_CH_SPRAY;
  cfg.pca9685_pulse_min = SQDET_PCA9685_PULSE_MIN;
  cfg.pca9685_pulse_max = SQDET_PCA9685_PULSE_MAX;
  cfg.spray_rest_deg = SQDET_SPRAY_REST_DEG;
  cfg.spray_squeeze_deg = SQDET_SPRAY_SQUEEZE_DEG;
  cfg.spray_squeeze_ms = SQDET_SPRAY_SQUEEZE_MS;
  return cfg;
}
