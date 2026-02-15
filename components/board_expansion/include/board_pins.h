/*
 * board_pins.h - GPIO pin definitions for OpenTrickler ESP32-S3
 *
 * Pin mapping based on original Raspberry Pi Pico W design, adapted for ESP32-S3.
 * This preserves the same PCB layout with ESP32-S3 in Pico form factor.
 *
 * Canonical pin definitions are in ot_pins.h (used by main.c).
 * This file must stay in sync with ot_pins.h.
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
// TX on GPIO15 (pin 6 = Pico GP4), RX on GPIO16 (pin 7 = Pico GP5).
// PCB: TX → resistor → TMC PDN_UART ← RX (standard TMC2209 single-wire circuit).
#define MOTOR_UART_NUM      UART_NUM_1
#define MOTOR_UART_TX_PIN   GPIO_NUM_15
#define MOTOR_UART_RX_PIN   GPIO_NUM_16

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

// Mini12864 LCD (SPI3)
// Pico GP18 pin24 -> ESP32 GPIO1 (SCK)
// Pico GP19 pin25 -> ESP32 GPIO2 (MOSI)
// Pico GP17 pin22 -> ESP32 GPIO41 (CS)
// Pico GP20 pin26 -> ESP32 GPIO4 (A0/DC)
// Pico GP21 pin27 -> ESP32 GPIO5 (RST)
#define DISPLAY_SPI_NUM     SPI3_HOST
#define DISPLAY_MOSI_PIN    GPIO_NUM_2
#define DISPLAY_SCK_PIN     GPIO_NUM_1
#define DISPLAY_CS_PIN      GPIO_NUM_41
#define DISPLAY_DC_PIN      GPIO_NUM_4
#define DISPLAY_RST_PIN     GPIO_NUM_5

// ========== Rotary Encoder Pins ==========

// Pico GP15 pin20 -> ESP32 GPIO40 (EN1/A)
// Pico GP14 pin19 -> ESP32 GPIO39 (EN2/B)
// Pico GP22 pin29 -> ESP32 GPIO6  (BTN)
// Pico GP12 pin16 -> ESP32 GPIO37 (RST)
#define ENCODER_A_PIN       GPIO_NUM_40
#define ENCODER_B_PIN       GPIO_NUM_39
#define ENCODER_BTN_PIN     GPIO_NUM_6
#define ENCODER_RST_PIN     GPIO_NUM_37

// ========== Scale Interface (UART) ==========

#define SCALE_UART_NUM      UART_NUM_2   // UART0 is reserved for console
#define SCALE_UART_TX_PIN   GPIO_NUM_11  // SCALE_UART_TX
#define SCALE_UART_RX_PIN   GPIO_NUM_12  // SCALE_UART_RX

// ========== NeoPixel LED ==========

#define NEOPIXEL_BACKLIGHT_PIN GPIO_NUM_38  // Mini12864 backlight chain (3 LEDs)
#define NEOPIXEL_PWM3_PIN   GPIO_NUM_9      // External PWM3 LED (mirrors LED1)
#define NEOPIXEL_COUNT      3               // RGB1 + RGB2 + backlight

// ========== Servo Gate ==========

#define SERVO0_PWM_PIN      GPIO_NUM_8   // SERVO0_PWM
#define SERVO1_PWM_PIN      GPIO_NUM_9   // SERVO1_PWM (shares pin with NEOPIXEL_PWM3)

// ========== I2C EEPROM ==========
// Pico GP26 pin31 -> ESP32 GPIO7 (SDA) - per physical pin mapping
// Pico GP27 pin32 -> ESP32 GPIO8 (SCL) - per physical pin mapping
// Note: ot_pins.h maps I2C to GPIO35/GPIO36 (Pico GP10/GP11 pins 14/15).
// Neither is currently used (NVS replaces EEPROM). Keeping both for reference.

#define EEPROM_I2C_SDA_PIN  GPIO_NUM_7
#define EEPROM_I2C_SCL_PIN  GPIO_NUM_8

#ifdef __cplusplus
}
#endif

#endif // BOARD_PINS_H_
