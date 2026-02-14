#pragma once

// Mapowanie zrobione POPRAWNIE: Pico GP -> PAD (1..40) -> ESP32 GPIO (Waveshare ESP32-S3-PICO)

// ====== LCD Mini12864 (linie na plytce z gniazda Pico) ======
// PHYSICAL PIN MAPPING Waveshare ESP32-S3-Pico:
// SPI0_SCK  : Pico GP18 pin24 -> ESP32 GPIO4  (verified from docs)
// SPI0_MOSI : Pico GP19 pin25 -> ESP32 GPIO8  (verified from docs)
// SPI0_CS0  : Pico GP17 pin22 -> ESP32 GPIO6  (D22=GPIO6 from docs)
// LCD_A0    : Pico GP22 pin29 -> ESP32 GPIO?  (TBD - need to verify)
// LCD_RST   : Pico GP20 pin26 -> ESP32 GPIO?  (TBD - need to verify)

#define LCD_SCK   4    // Physical pin 24
#define LCD_MOSI  8    // Physical pin 25
#define LCD_CS    6    // Physical pin 22 (D22=GPIO6)
#define LCD_A0    10   // Physical pin 29 (CHANGED: avoid GPIO7 conflict)
#define LCD_RST   -1   // DISABLED for testing // Physical pin 26

// ====== Encoder / buttons ======
// BTN_ENC : Pico GP21 pad27 -> ESP32 GPIO5
// BTN_RST : Pico GP12 pad16 -> ESP32 GPIO37
// BTN_EN1 : Pico GP14 pad19 -> ESP32 GPIO39
// BTN_EN2 : Pico GP15 pad20 -> ESP32 GPIO40

#define ENC_BTN   5
#define BTN_RST   37
#define BTN_EN1   39
#define BTN_EN2   40

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
#define NEOPIXEL      38   // Mini12864 backlight chain (RGB1 + RGB2 + backlight)
#define NEOPIXEL_PWM3  9   // External PWM3 LED output (Pico GP28 -> ESP32 GPIO9)
