# Tabela funkcji i pinów Pico W w projekcie OpenTrickler-RP2040-Controller

# Tabela funkcji i pinów ESP32-S3-PICO-V3-02 (wg mapowania fizycznych pinów)

| Funkcja                | Pin fizyczny | GPIO ESP32-S3-PICO-V3-02 |
|------------------------|--------------|--------------------------|
| SCALE_UART_TX          |  1           | GP11                     |
| SCALE_UART_RX          |  2           | GP12                     |
| COARSE_MOTOR_DIR       |  4           | GP13                     |
| COARSE_MOTOR_STEP      |  5           | GP14                     |
| MOTOR_UART_TX          |  6           | GP16  <!-- Half-duplex: TX/RX on GP16. To revert, restore TX=GP15, RX=GP16 --> |
| MOTOR_UART_RX          |  7           | GP16  <!-- Half-duplex: TX/RX on GP16. To revert, restore TX=GP15, RX=GP16 --> |
| COARSE_MOTOR_EN        |  9           | GP17                     |
| FINE_MOTOR_DIR         | 10           | GP18                     |
| FINE_MOTOR_STEP        | 11           | GP33                     |
| FINE_MOTOR_EN          | 12           | GP34                     |
| BUTTON0_RST            | 16           | GP37                     |
| BUTTON0_ENCODER_PIN2   | 15           | GP36                     |
| BUTTON0_ENCODER_PIN1   | 14           | GP35                     |
| DISPLAY0_RX            | 17           | **GP38**                 |
| DISPLAY0_CS            | 18           | GP39                     |
| DISPLAY0_SCK           | 19           | GP40                     |
| DISPLAY0_TX            | 21           | GP2                      |
| DISPLAY0_A0            | 22           | GP4                      |
| DISPLAY0_RESET         | 23           | GP5                      |
| BUTTON0_ENC            | 29           | GP6                      |
| EEPROM_SDA             | 31           | GP7                      |
| SERVO0_PWM             | 32           | GP8                      |
| SERVO1_PWM             | 34           | GP9                      |
| NEOPIXEL_PWM3          | 34           | GP9                      |
| NEOPIXEL               | 19           | GP14                     |
| EEPROM_SCL             | 32           | GP8                      |
| WATCHDOG_LED           | 25           | GP2                      |

**Uwaga:**
- Pin fizyczny = numer pinu na podstawce Pico (1–40, patrząc od góry, USB u góry po lewej).
- GPIO ESP32-S3-PICO-V3-02 = numer GPIO na ESP32-S3-PICO-V3-02 zgodnie z Twoim mapowaniem.
- Niektóre funkcje mogą dzielić ten sam pin fizyczny lub GPIO (np. NEOPIXEL_PWM3 i SERVO1_PWM na GP9).
- Niektóre funkcje mogą wymagać innego przypisania lub nie mieć bezpośredniego odpowiednika (np. WATCHDOG_LED).

| Funkcja                | Pin fizyczny | GPIO Pico W |
|------------------------|--------------|-------------|
| SCALE_UART_TX          |  1           | GP0         |
| SCALE_UART_RX          |  2           | GP1         |
| COARSE_MOTOR_DIR       |  4           | GP2         |
| COARSE_MOTOR_STEP      |  5           | GP3         |
| MOTOR_UART_TX          |  6           | GP4         |
| MOTOR_UART_RX          |  7           | GP5         |
| COARSE_MOTOR_EN        |  9           | GP6         |
| FINE_MOTOR_DIR         | 10           | GP7         |
| FINE_MOTOR_STEP        | 11           | GP8         |
| FINE_MOTOR_EN          | 12           | GP9         |
| BUTTON0_RST            | 16           | GP12        |
| BUTTON0_ENCODER_PIN2   | 15           | GP14        |
| BUTTON0_ENCODER_PIN1   | 14           | GP15        |
| DISPLAY0_RX            | 17           | GP16        |
| DISPLAY0_CS            | 18           | GP17        |
| DISPLAY0_SCK           | 19           | GP18        |
| DISPLAY0_TX            | 21           | GP19        |
| DISPLAY0_A0            | 22           | GP20        |
| DISPLAY0_RESET         | 23           | GP21        |
| BUTTON0_ENC            | 29           | GP22        |
| EEPROM_SDA             | 31           | GP26        |
| SERVO0_PWM             | 32           | GP27        |
| SERVO1_PWM             | 34           | GP28        |
| NEOPIXEL_PWM3          | 34           | GP28        |
| NEOPIXEL               | 19           | GP13        |
| EEPROM_SCL             | 32           | GP27        |
| WATCHDOG_LED           | 25           | CYW43_WL_GPIO_LED_PIN |

**Uwaga:**
- Pin fizyczny = numer pinu na podstawce Pico (1–40, patrząc od góry, USB u góry po lewej).
- GPIO Pico W = numer GPIO używany w kodzie.
- Niektóre funkcje mogą dzielić ten sam pin fizyczny lub GPIO (np. NEOPIXEL_PWM3 i SERVO1_PWM na GP28).
# Pin Mapping: Raspberry Pi Pico W ↔ ESP32-S3-PICO-V3-02

Porównanie fizycznych wyprowadzeń (pinów) obu układów w kontekście zamiany w podstawce Open Tricklera.

| Pin fiz | Pico W      | ESP32-S3-PICO |
| ------: | ----------- | ------------- |
|       1 | GP0         | GP11          |
|       2 | GP1         | GP12          |
|       3 | GND         | GND           |
|       4 | GP2         | GP13          |
|       5 | GP3         | GP14          |
|       6 | GP4         | GP15          |
|       7 | GP5         | GP16          |
|       8 | GND         | GND           |
|       9 | GP6         | GP17          |
|      10 | GP7         | GP18          |
|      11 | GP8         | GP33          |
|      12 | GP9         | GP34          |
|      13 | GND         | GND           |
|      14 | GP10        | GP35          |
|      15 | GP11        | GP36          |
|      16 | GP12        | GP37          |
|      17 | GP13        | **GP38**      |
|      18 | GND         | GND           |
|      19 | GP14        | GP39          |
|      20 | GP15        | GP40          |
|      21 | GP16        | GP42          |
|      22 | GP17        | GP41          |
|      23 | GND         | GND           |
|      24 | GP18        | GP1           |
|      25 | GP19        | GP2           |
|      26 | GP20        | GP4           |
|      27 | GP21        | GP5           |
|      28 | GND         | GND           |
|      29 | GP22        | GP6           |
|      30 | RUN         | RUN           |
|      31 | GP26 / ADC0 | GP7           |
|      32 | GP27 / ADC1 | GP8           |
|      33 | AGND        | GND           |
|      34 | GP28 / ADC2 | GP9           |
|      35 | ADC_VREF    | GP10          |
|      36 | 3V3(OUT)    | 3V3(OUT)      |
|      37 | 3V3_EN      | 3V3_EN        |
|      38 | GND         | GND           |
|      39 | VSYS        | VSYS          |
|      40 | VBUS        | VBUS          |

**Uwaga:**
- ESP32-S3-PICO-V3-02 ma mniej wyprowadzonych pinów niż Pico W (brak wszystkich funkcji, nie wszystkie GPIO są dostępne na zewnątrz).
- Sprawdź dokumentację sprzętową obu układów przed podłączeniem!
- Pico W: [datasheet](https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf)
- ESP32-S3-PICO-V3-02: [datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-pico-v3-02_datasheet_en.pdf)
