/*
 * board_pins.h - GPIO pin definitions for OpenTrickler ESP32-S3
 *
 * Pin mapping based on original Raspberry Pi Pico W design, adapted for ESP32-S3.
 * This preserves the same PCB layout with ESP32-S3 in Pico form factor.
 */

#ifndef BOARD_PINS_H_
#define BOARD_PINS_H_

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ========== Motor Control Pins ==========

// TMC UART (shared for both motors)
#define MOTOR_UART_NUM      UART_NUM_1
#define MOTOR_UART_TX_PIN   GPIO_NUM_4
#define MOTOR_UART_RX_PIN   GPIO_NUM_5

// Coarse Trickler Motor (Motor 0)
#define COARSE_MOTOR_ADDR   0
#define COARSE_MOTOR_EN_PIN GPIO_NUM_6
#define COARSE_MOTOR_STEP_PIN GPIO_NUM_3
#define COARSE_MOTOR_DIR_PIN GPIO_NUM_2

// Fine Trickler Motor (Motor 1)
#define FINE_MOTOR_ADDR     1
#define FINE_MOTOR_EN_PIN   GPIO_NUM_9
#define FINE_MOTOR_STEP_PIN GPIO_NUM_8
#define FINE_MOTOR_DIR_PIN  GPIO_NUM_7

// ========== Display Pins (ST7567 SPI) ==========

// ST7567 128x64 LCD (SPI)
#define DISPLAY_SPI_NUM     SPI2_HOST
#define DISPLAY_MOSI_PIN    GPIO_NUM_11
#define DISPLAY_SCK_PIN     GPIO_NUM_12
#define DISPLAY_CS_PIN      GPIO_NUM_13
#define DISPLAY_DC_PIN      GPIO_NUM_14
#define DISPLAY_RST_PIN     GPIO_NUM_15

// ========== Rotary Encoder Pins ==========

#define ENCODER_A_PIN       GPIO_NUM_16
#define ENCODER_B_PIN       GPIO_NUM_17
#define ENCODER_BTN_PIN     GPIO_NUM_18

// ========== Scale Interface (UART) ==========

#define SCALE_UART_NUM      UART_NUM_2
#define SCALE_UART_RX_PIN   GPIO_NUM_19
#define SCALE_UART_TX_PIN   GPIO_NUM_20

// ========== NeoPixel LED ==========

#define NEOPIXEL_PIN        GPIO_NUM_21
#define NEOPIXEL_COUNT      2

// ========== Servo Gate ==========

#define SERVO_PWM_PIN       GPIO_NUM_22

// ========== Other I/O ==========

// Additional GPIO available for future expansion
// GPIO 1, 10, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42

#ifdef __cplusplus
}
#endif

#endif // BOARD_PINS_H_
