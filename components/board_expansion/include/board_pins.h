/*
 * board_pins.h - GPIO pin definitions for OpenTrickler ESP32-S3
 *
 * Pin mapping based on original Raspberry Pi Pico W design, adapted for ESP32-S3.
 * This preserves the same PCB layout with ESP32-S3 in Pico form factor.
 */

#ifndef BOARD_PINS_H_
#define BOARD_PINS_H_

#include "driver/gpio.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

// ========== Motor Control Pins ==========

// TMC UART (shared for both motors)
// Single-wire half-duplex: TX and RX on same pin (GPIO15), same as original Pico design.
// TMC2209 has one PDN_UART pin; TX drives the line, RX reads echo + TMC response.
#define MOTOR_UART_NUM      UART_NUM_1
#define MOTOR_UART_TX_PIN   GPIO_NUM_15
#define MOTOR_UART_RX_PIN   GPIO_NUM_15

// Coarse Trickler Motor (Motor 0)
#define COARSE_MOTOR_ADDR   0
#define COARSE_MOTOR_EN_PIN GPIO_NUM_17
#define COARSE_MOTOR_STEP_PIN GPIO_NUM_14
#define COARSE_MOTOR_DIR_PIN GPIO_NUM_13

// Fine Trickler Motor (Motor 1)
#define FINE_MOTOR_ADDR     1
#define FINE_MOTOR_EN_PIN   GPIO_NUM_34
#define FINE_MOTOR_STEP_PIN GPIO_NUM_33
#define FINE_MOTOR_DIR_PIN  GPIO_NUM_18

// ========== Display Pins (SPI) ==========

// Mini12864 LCD (SPI)
#define DISPLAY_SPI_NUM     SPI2_HOST
#define DISPLAY_MOSI_PIN    GPIO_NUM_2   // DISPLAY0_TX
#define DISPLAY_SCK_PIN     GPIO_NUM_40  // DISPLAY0_SCK
#define DISPLAY_CS_PIN      GPIO_NUM_39  // DISPLAY0_CS
#define DISPLAY_DC_PIN      GPIO_NUM_4   // DISPLAY0_A0
#define DISPLAY_RST_PIN     GPIO_NUM_5   // DISPLAY0_RESET
#define DISPLAY_MISO_PIN    GPIO_NUM_38  // DISPLAY0_RX (usually NC)

// ========== Rotary Encoder Pins ==========

#define ENCODER_A_PIN       GPIO_NUM_35  // BUTTON0_ENCODER_PIN1
#define ENCODER_B_PIN       GPIO_NUM_36  // BUTTON0_ENCODER_PIN2
#define ENCODER_BTN_PIN     GPIO_NUM_6   // BUTTON0_ENC
#define ENCODER_RST_PIN     GPIO_NUM_37  // BUTTON0_RST

// ========== Scale Interface (UART) ==========

#define SCALE_UART_NUM      UART_NUM_2   // UART0 is reserved for console
#define SCALE_UART_TX_PIN   GPIO_NUM_11  // SCALE_UART_TX
#define SCALE_UART_RX_PIN   GPIO_NUM_12  // SCALE_UART_RX

// ========== NeoPixel LED ==========

#define NEOPIXEL_PIN        GPIO_NUM_9   // NEOPIXEL_PWM3
#define NEOPIXEL_COUNT      2

// ========== Servo Gate ==========

#define SERVO0_PWM_PIN      GPIO_NUM_8   // SERVO0_PWM (note: conflicts with EEPROM_SCL)
#define SERVO1_PWM_PIN      GPIO_NUM_9   // SERVO1_PWM

// ========== I2C EEPROM ==========

#define EEPROM_I2C_SDA_PIN  GPIO_NUM_7   // EEPROM_SDA
#define EEPROM_I2C_SCL_PIN  GPIO_NUM_8   // EEPROM_SCL

#ifdef __cplusplus
}
#endif

#endif // BOARD_PINS_H_
