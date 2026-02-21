# OpenTrickler ESP32-S3 Controller

This repo is an attempt to replace the Pico W/2W in the OpenTrickler controller with an ESP32-S3-Pico board. The port is based on the work done by Ran Bao/Eeamars – link to the original repository:
https://github.com/eamars/OpenTrickler-RP2040-Controller

## Project Status (~85-90% complete)

| Module | Status | Notes |
|--------|--------|-------|
| **Motor Control (MCPWM)** | ✅ Complete | Variable-speed STEP, acceleration ramp, PD control |
| **TMC2209 UART** | ⏳ Working | Both motors run; full register r/w needs more testing |
| **Scale (G&G JJB)** | ✅ Complete | Frame-driven polling (~20ms), queued tare |
| **Scale (other models)** | ⏳ Untested | Universal parser + frame structs for 7 models |
| **Display (LVGL + ST7567)** | ✅ Complete | 12-screen menu, encoder navigation, custom fonts |
| **NeoPixel LED** | ✅ Complete | RMT WS2812B, mini12864 backlight + PWM3 mirror |
| **Charge Mode** | ✅ Complete | PD dispense cycle, over/under detection, charge history |
| **Cleanup Mode** | ✅ Complete | Manual trickler with reverse support |
| **Profile System** | ✅ Complete | 8 profiles with PD params, REST CRUD |
| **WiFi (STA + AP)** | ✅ Complete | Auto-start, NVS config storage |
| **Web UI** | ✅ Complete | Portal, wizard, charge control, inline banners |
| **REST API** | ✅ Mostly | 14 endpoints; servo/button/display_buffer missing |
| **Autotuning** | ✅ Complete | (1+1)-ES evolutionary Kp/Kd autotuning, coarse then fine |
| **Flow Model** | ✅ Active | Self-learning motor-to-weight correlation, quality-weighted EMA |
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


## Charge Mode

Pure PD control (Kp + Kd, no Ki) for two-stage powder dispensing:
1. **Coarse motor** — fast bulk dispensing until `coarse_stop_threshold`
2. **Fine motor** — precise trickle until target weight reached

Precision > speed. Overshoot is always worse than undershoot.

## Autotuning

Endpoint: `GET /rest/autotune_coarse`

(1+1)-ES evolutionary algorithm that optimizes Kp/Kd per motor:
1. Tunes coarse motor first (max N dispenses, adaptive Kp/Kd mutation)
2. Then tunes fine motor (same approach)
3. Optional auto-save of best parameters to profile

Parameters:
- `a1`: target weight for coarse stage (grains)
- `a2`: target time for coarse stage (s)
- `a3`: final target weight for fine stage (grains)
- `a4`: target time for fine stage (s)
- `a5`: max trials per stage (e.g. 15)
- `a6`: weight error tolerance (grains)
- `a7`: time error tolerance (s)
- `a8`: auto-apply best Kp/Kd to profile
- `ee`: save profile to NVS

## Flow Model (self-learning)

The system learns the relationship between motor speed (RPS) and powder flow rate (gn/s)
from every dispense. This happens passively — no user action required.

**What it does now:**
- Records time/speed/weight samples during every dispense (charge mode + autotune)
- After each dispense: analyzes data, updates piecewise-linear flow model per motor
- Extended filtering: spin-up rejection, non-steady rejection, braking filter
- Quality-weighted EMA: noisy dispenses barely affect the model
- Per-bin confidence tracking with `trusted` threshold
- Measures transport delay and inertia (in-flight powder after motor stop)
- Persisted in NVS per profile (survives reboot)

**What's next (Phase 2d):**
- Auto max_speed limit — if repeated overshoots detected, reduce max motor speed
- One-directional safety: only slows down, never increases aggressiveness
- Gated by model confidence (only activates when enough data collected)

**Future:**
- Feedforward speed control (replace P-term with model-based speed prediction)
- Analytical PD tuning (compute optimal Kp/Kd from flow model slope)

See [docs/flow_model_plan.md](docs/flow_model_plan.md) for full technical design.

## Text and Encoding Policy

Project text policy:
- Use English for source comments, log messages, UI labels, and documentation.
- Use UTF-8 encoding and LF line endings.

Repository checks:
```bash
python scripts/check_text_quality.py
```
