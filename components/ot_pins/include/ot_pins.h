#pragma once

// Mapowanie POPRAWNE: Pico GP -> PAD (1..40) -> ESP32 GPIO (Waveshare ESP32-S3-PICO)

// ====== LCD Mini12864 (linie z gniazda Pico na plytce) ======
// PHYSICAL PIN MAPPING from OpenTrickler Controller v2.0 board:
// LCD_SCK  : Pico GP16 pin21 -> ESP32 GPIO42 (EXP1 Pin3)
// LCD_MOSI : Pico GP19 pin25 -> ESP32 GPIO2  (EXP1 Pin6)
// LCD_CS   : Pico GP17 pin22 -> ESP32 GPIO41 (EXP1 Pin4)
// LCD_A0   : Pico GP20 pin26 -> ESP32 GPIO4  (EXP1 Pin5)
// LCD_RST  : Pico GP13 pin17 -> ESP32 GPIO38 (EXP2 Pin7)

#define LCD_SCK   42   // Physical pin 21 (GP16 -> GPIO42) - with special config
#define LCD_MOSI  2    // Physical pin 25 (GP19 -> GPIO2)
#define LCD_CS    41   // Physical pin 22 (GP17 -> GPIO41)
#define LCD_A0    4    // Physical pin 26 (GP20 -> GPIO4)
#define LCD_RST   38   // Physical pin 17 (GP13 -> GPIO38)

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
#define NEOPIXEL  38
