# Pin Mapping: Raspberry Pi Pico W ↔ ESP32-S3-PICO-V3-02

## ESP32-S3-PICO-V3-02 Function Pin Table (per ot_pins.h / board_pins.h)

| Function               | Pico GPIO | Phys Pin | ESP32-S3 GPIO | Status          |
|------------------------|-----------|----------|---------------|-----------------|
| SCALE_UART_TX          | GP0       |  1       | GPIO11        | OK (in use)     |
| SCALE_UART_RX          | GP1       |  2       | GPIO12        | OK (in use)     |
| COARSE_MOTOR_DIR       | GP2       |  4       | GPIO13        | OK (in use)     |
| COARSE_MOTOR_STEP      | GP3       |  5       | GPIO14        | OK (in use)     |
| MOTOR_UART_TX          | GP4       |  6       | GPIO15        | OK (in use)     |
| MOTOR_UART_RX          | GP5       |  7       | GPIO16        | OK (in use)     |
| COARSE_MOTOR_EN        | GP6       |  9       | GPIO17        | OK (in use)     |
| FINE_MOTOR_DIR         | GP7       | 10       | GPIO18        | OK (in use)     |
| FINE_MOTOR_STEP        | GP8       | 11       | GPIO33        | OK (in use)     |
| FINE_MOTOR_EN          | GP9       | 12       | GPIO34        | OK (in use)     |
| ENCODER_A (BTN_EN1)    | GP15      | 20       | GPIO40        | OK (in use)     |
| ENCODER_B (BTN_EN2)    | GP14      | 19       | GPIO39        | OK (in use)     |
| ENCODER_BTN            | GP22      | 29       | GPIO6         | OK (in use)     |
| ENCODER_RST            | GP12      | 16       | GPIO37        | OK (assigned)   |
| LCD_SCK                | GP18      | 24       | GPIO1         | OK (in use)     |
| LCD_MOSI               | GP19      | 25       | GPIO2         | OK (in use)     |
| LCD_CS                 | GP17      | 22       | GPIO41        | OK (in use)     |
| LCD_A0 (DC)            | GP20      | 26       | GPIO4         | OK (in use)     |
| LCD_RST                | GP21      | 27       | GPIO5         | OK (in use)     |
| NEOPIXEL (backlight)   | GP13      | 17       | GPIO38        | OK (in use)     |
| NEOPIXEL_PWM3          | GP28      | 34       | GPIO9         | OK (in use)     |
| EEPROM_SDA             | GP26      | 31       | GPIO7         | Unused (NVS)    |
| EEPROM_SCL             | GP27      | 32       | GPIO8         | Unused (NVS)    |
| SERVO0_PWM             | GP27      | 32       | GPIO8         | Not implemented |
| SERVO1_PWM             | GP28      | 34       | GPIO9         | Conflicts with NEOPIXEL_PWM3 |

**Notes:**
- Phys Pin = pin number on the Pico socket (1-40, top view, USB at top left).
- ESP32-S3 GPIO = GPIO number as defined in `ot_pins.h` and `board_pins.h`.
- EEPROM is not used (replaced by NVS flash).
- SERVO1_PWM and NEOPIXEL_PWM3 share GPIO9 - currently used as NeoPixel.

---

## Original Pico W Function Pin Table

| Function               | Phys Pin | Pico W GPIO |
|------------------------|----------|-------------|
| SCALE_UART_TX          |  1       | GP0         |
| SCALE_UART_RX          |  2       | GP1         |
| COARSE_MOTOR_DIR       |  4       | GP2         |
| COARSE_MOTOR_STEP      |  5       | GP3         |
| MOTOR_UART_TX          |  6       | GP4         |
| MOTOR_UART_RX          |  7       | GP5         |
| COARSE_MOTOR_EN        |  9       | GP6         |
| FINE_MOTOR_DIR         | 10       | GP7         |
| FINE_MOTOR_STEP        | 11       | GP8         |
| FINE_MOTOR_EN          | 12       | GP9         |
| ENCODER_A (PIN1)       | 14       | GP10        |
| ENCODER_B (PIN2)       | 15       | GP11        |
| ENCODER_RST            | 16       | GP12        |
| NEOPIXEL (backlight)   | 17       | GP13        |
| ENCODER_A (EN1)        | 19       | GP14        |
| ENCODER_B (EN2)        | 20       | GP15        |
| DISPLAY_RX             | 17       | GP16        |
| DISPLAY_CS             | 18       | GP17        |
| DISPLAY_SCK            | 19       | GP18        |
| DISPLAY_TX             | 21       | GP19        |
| DISPLAY_A0             | 22       | GP20        |
| DISPLAY_RESET          | 23       | GP21        |
| ENCODER_BTN            | 29       | GP22        |
| EEPROM_SDA             | 31       | GP26        |
| EEPROM_SCL / SERVO0    | 32       | GP27        |
| NEOPIXEL_PWM3 / SERVO1 | 34      | GP28        |
| WATCHDOG_LED           | -        | CYW43_WL_GPIO_LED_PIN |

---

## Pin-to-Pin Comparison: Pico W ↔ ESP32-S3-PICO-V3-02

Physical pin comparison for drop-in replacement on the OpenTrickler PCB.

| Phys Pin | Pico W      | ESP32-S3-PICO | Project Function            |
| -------: | ----------- | ------------- | --------------------------- |
|        1 | GP0         | GPIO11        | Scale UART TX               |
|        2 | GP1         | GPIO12        | Scale UART RX               |
|        3 | GND         | GND           |                             |
|        4 | GP2         | GPIO13        | Coarse motor DIR            |
|        5 | GP3         | GPIO14        | Coarse motor STEP           |
|        6 | GP4         | GPIO15        | TMC UART TX                 |
|        7 | GP5         | GPIO16        | TMC UART RX                 |
|        8 | GND         | GND           |                             |
|        9 | GP6         | GPIO17        | Coarse motor EN             |
|       10 | GP7         | GPIO18        | Fine motor DIR              |
|       11 | GP8         | GPIO33        | Fine motor STEP             |
|       12 | GP9         | GPIO34        | Fine motor EN               |
|       13 | GND         | GND           |                             |
|       14 | GP10        | GPIO35        | (I2C SDA in ot_pins.h)      |
|       15 | GP11        | GPIO36        | (I2C SCL in ot_pins.h)      |
|       16 | GP12        | GPIO37        | Encoder RST button          |
|       17 | GP13        | GPIO38        | NeoPixel backlight          |
|       18 | GND         | GND           |                             |
|       19 | GP14        | GPIO39        | Encoder B (BTN_EN2)         |
|       20 | GP15        | GPIO40        | Encoder A (BTN_EN1)         |
|       21 | GP16        | GPIO42        | (not used)                  |
|       22 | GP17        | GPIO41        | LCD CS                      |
|       23 | GND         | GND           |                             |
|       24 | GP18        | GPIO1         | LCD SCK                     |
|       25 | GP19        | GPIO2         | LCD MOSI                    |
|       26 | GP20        | GPIO4         | LCD A0 (DC)                 |
|       27 | GP21        | GPIO5         | LCD RST                     |
|       28 | GND         | GND           |                             |
|       29 | GP22        | GPIO6         | Encoder BTN                 |
|       30 | RUN         | RUN           |                             |
|       31 | GP26 / ADC0 | GPIO7         | EEPROM SDA (unused)         |
|       32 | GP27 / ADC1 | GPIO8         | EEPROM SCL / Servo0 (unused)|
|       33 | AGND        | GND           |                             |
|       34 | GP28 / ADC2 | GPIO9         | NeoPixel PWM3 / Servo1      |
|       35 | ADC_VREF    | GPIO10        | (not used)                  |
|       36 | 3V3(OUT)    | 3V3(OUT)      |                             |
|       37 | 3V3_EN      | 3V3_EN        |                             |
|       38 | GND         | GND           |                             |
|       39 | VSYS        | VSYS          |                             |
|       40 | VBUS        | VBUS          |                             |

**Notes:**
- ESP32-S3-PICO-V3-02 has fewer exposed pins than Pico W (not all GPIOs are externally available).
- Check hardware documentation for both boards before connecting!
- Pico W: [datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)
- ESP32-S3-PICO-V3-02: [datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-pico-v3-02_datasheet_en.pdf)
