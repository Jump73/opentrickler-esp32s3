# OpenTrickler ESP32-S3 Controller

This repo is an attempt to replace the Pico W/2W in the OpenTrickler controller with an ESP32-S3-Pico board. The port is based on the work done by Ran Bao/Eeamars – link to the original repository:
https://github.com/eamars/OpenTrickler-RP2040-Controller

## Project Status (~80-85% complete)

| Module | Status | Notes |
|--------|--------|-------|
| **Motor Control (MCPWM)** | ✅ Complete | Variable-speed STEP, acceleration ramp, PID control |
| **TMC2209 UART** | ⏳ Working | Both motors run; full register r/w needs more testing |
| **Scale (G&G JJB)** | ✅ Complete | Frame-driven polling (~20ms), queued tare |
| **Scale (other models)** | ⏳ Untested | Universal parser + frame structs for 7 models |
| **Display (LVGL + ST7567)** | ✅ Complete | 12-screen menu, encoder navigation, custom fonts |
| **NeoPixel LED** | ✅ Complete | RMT WS2812B, mini12864 backlight + PWM3 mirror |
| **Charge Mode** | ✅ Complete | Full PID dispense cycle, over/under detection |
| **Cleanup Mode** | ✅ Complete | Manual trickler with reverse support |
| **Profile System** | ✅ Complete | 8 profiles with PID params, REST CRUD |
| **WiFi (STA + AP)** | ✅ Complete | Auto-start, NVS config storage |
| **Web UI** | ✅ Complete | Portal, wizard, charge control, inline banners |
| **REST API** | ✅ Mostly | 14 endpoints; servo/button/display_buffer missing |
| **Coarse+Fine Autotuning** | ✅ New | Iterative Kp/Kd autotuning: coarse first, then fine, with profile save option |
| **Servo Gate** | ❌ Missing | Component stub only |
| **Sartorius Scale** | ❌ Missing | No frame structure |

See [COMPARISON_REPORT.md](COMPARISON_REPORT.md) for detailed analysis.

## Hardware

- **MCU:** ESP32-S3-Pico (drop-in replacement for Pico W)
- **Display:** Mini 12864 LCD (ST7567 via SPI) + NeoPixel backlight
- **Motors:** 2x TMC2209 stepper drivers (MCPWM STEP generation)
- **Scale:** UART serial (G&G JJB tested, 7 other models supported)
- **Input:** Rotary encoder with button

## Software Stack

- **Framework:** ESP-IDF (FreeRTOS)
- **Graphics:** LVGL v9.2.2
- **LED Driver:** espressif/led_strip v2.5.5 (RMT)
- **Storage:** NVS (replaces external EEPROM)

## Building

Requires ESP-IDF toolchain. Build via ESP-IDF Command Prompt:

```
idf.py build
idf.py -p COMx flash monitor
```

## Web UI

After flashing, the ESP32 creates a WiFi AP:
- **SSID:** OpenTrickler-ESP32
- **Password:** opentrickler
- **URL:** http://192.168.4.1/

HTML sources are in `html/`. After editing, regenerate embedded headers:
```
python scripts/html2header.py -f html/web_portal.html -o main/generated/web_portal.html.h --no-minify
```


## Coarse + Fine Autotuning (REST)

Endpoint: `GET /rest/autotune_coarse`

Proces jest sekwencyjny:
1. strojenie `coarse` (maks. `a5` prób, adaptacyjna zmiana `Kp/Kd` po każdym nasypie),
2. po uzyskaniu sensownych parametrów coarse strojenie `fine` (analogicznie, także do `a5` prób),
3. opcjonalny zapis dobranych parametrów do profilu.

Przykład startu:
`?a0=true&a1=18.5&a2=6.0&a3=20.0&a4=2.5&a5=15&a6=0.03&a7=0.35&a8=true&ee=true`

- Poll status: `GET /rest/autotune_coarse`
- Cancel: `?ca=true`
- Jeśli etap nie osiągnie tolerancji (`a6`, `a7`) w limicie `a5`, autotuning kończy się błędem i nie przechodzi dalej.

Parameters:
- `a1`: docelowa waga dla etapu coarse (grains)
- `a2`: docelowy czas etapu coarse (s)
- `a3`: docelowa waga końcowa dla etapu fine (grains)
- `a4`: docelowy czas etapu fine (s)
- `a5`: maksymalna liczba prób na etap (np. 15)
- `a6`: tolerancja błędu masy (grains)
- `a7`: tolerancja błędu czasu (s)
- `a8`: auto-apply (zastosuj najlepsze `Kp/Kd` do profilu)
- `ee`: zapisz profil do NVS
