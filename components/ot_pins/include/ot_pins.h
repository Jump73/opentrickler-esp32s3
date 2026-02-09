#pragma once

// Mapowanie: Pico W GP -> Physical Pin (1..40) -> ESP32 GPIO (Waveshare ESP32-S3-PICO)
// Reference: PICO_W-ESP32_S3_PICO_PIN_MAPPING.md (bottom table, lines 79-120)

// ====== LCD Mini12864 (SPI0 on original Pico W) ======
// LCD_SCK  : Pico GP18 pin24 -> ESP32 GPIO1
// LCD_MOSI : Pico GP19 pin25 -> ESP32 GPIO2
// LCD_CS   : Pico GP17 pin22 -> ESP32 GPIO41
// LCD_A0   : Pico GP20 pin26 -> ESP32 GPIO4
// LCD_RST  : Pico GP21 pin27 -> ESP32 GPIO5

#define LCD_SCK   1    // Physical pin 24 (GP18 -> GPIO1)
#define LCD_MOSI  2    // Physical pin 25 (GP19 -> GPIO2)
#define LCD_CS    41   // Physical pin 22 (GP17 -> GPIO41)
#define LCD_A0    4    // Physical pin 26 (GP20 -> GPIO4)
#define LCD_RST   5    // Physical pin 27 (GP21 -> GPIO5)

// ====== Encoder / buttons ======
// BTN_ENC : Pico GP22 pin29 -> ESP32 GPIO6
// BTN_RST : Pico GP12 pin16 -> ESP32 GPIO37
// BTN_EN1 : Pico GP15 pin20 -> ESP32 GPIO40 (ENCODER_PIN1)
// BTN_EN2 : Pico GP14 pin19 -> ESP32 GPIO39 (ENCODER_PIN2)

#define ENC_BTN   6
#define BTN_RST   37
#define BTN_EN1   40
#define BTN_EN2   39

// ====== Motors ======
#define M1_DIR    13
#define M1_STEP   14
#define M1_EN     17

#define M2_DIR    18
#define M2_STEP   33
#define M2_EN     34

// ====== UART ======
#define UART0_TX  11
#define UART0_RX  12

#define UART1_TX  15
#define UART1_RX  16

// ====== I2C EEPROM ======
#define I2C_SDA   35
#define I2C_SCL   36

// ====== NeoPixel ======
#define NEOPIXEL  38
