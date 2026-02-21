# OpenTrickler ESP32-S3 Controller

This repository ports the OpenTrickler controller from Pico W/2W to ESP32-S3.
Original upstream project:
https://github.com/eamars/OpenTrickler-RP2040-Controller

## Project Status

| Module | Status | Notes |
|--------|--------|-------|
| Motor Control (MCPWM) | Complete | Variable speed STEP generation, acceleration ramp, PD loop |
| TMC2209 UART | Working | Both motors operating, full register coverage still under validation |
| Scale (G&G JJB) | Complete | Frame-driven polling, stable runtime integration |
| Scale (other models) | Partial | Parser/framework present, hardware validation pending |
| Display (LVGL + ST7567) | Complete | Menu system, encoder navigation, mirror page available |
| NeoPixel LED | Complete | WS2812 control for backlight and status LEDs |
| Charge Mode | Complete | Coarse/fine dispense, post-settle classification, history export |
| Cleanup Mode | Complete | Manual trickler control with reverse |
| Profile System | Complete | Profile CRUD + NVS persistence |
| WiFi (STA + AP) | Complete | Auto-start + NVS config |
| Web UI | Complete | Portal + wizard + autotune panel |
| REST API | Active | Includes autotune status, trials, telemetry, and finish-now control |
| Autotuning | Active | Coarse/fine ES tuning, confirmations, speed probes, cancel + finish-now |
| Flow Model | Active | Self-learning speed->flow map, delay/inertia estimation, quality-weighted EMA |
| Servo Gate | Missing | Stub only |
| Sartorius Scale | Missing | Not yet implemented |

See `COMPARISON_REPORT.md` for broader comparison notes.

## Hardware

- MCU: ESP32-S3-Pico
- Display: Mini 12864 LCD (ST7567 SPI) + NeoPixel backlight
- Motors: 2x TMC2209 stepper drivers
- Scale: UART serial (G&G JJB validated)
- Input: rotary encoder + push button

## Software Stack

- Framework: ESP-IDF (FreeRTOS)
- Graphics: LVGL
- LED driver: `espressif/led_strip`
- Storage: NVS

## Build

```bash
idf.py build
idf.py -p COMx flash monitor
```

## Web Access

Default AP mode after flashing:
- SSID: `OpenTrickler-ESP32`
- Password: `opentrickler`
- URL: `http://192.168.4.1/`

HTML sources are in `html/`.
After editing portal HTML, regenerate embedded header:

```bash
python scripts/html2header.py -f html/web_portal.html -o main/generated/web_portal.html.h --no-minify
```

## Charge Mode

Current control is PD-based (Kp + Kd, Ki unused):
1. Coarse motor bulk fill to coarse threshold.
2. Fine motor precision finish to final threshold.
3. Post-settle classification (`OK/UNDER/OVER`) after stability confirmation.

Settled result now uses the final stable scale reading (no controller-side averaging of final weight).

## Autotune

Endpoint:
- `GET /rest/autotune_coarse`

Main behavior:
- Stage 1: coarse tuning
- Stage 2: fine tuning (with coarse prefill)
- Multi-run confirmation before accepting a setup
- Optional faster-setup probing with re-confirmation
- Hard cancel and graceful finish-now are both supported

Runtime controls:
- Cancel now (hard stop, no completion): `ca=true`
- Finish now (graceful):
  - in coarse: move to fine stage
  - in fine: finish as DONE and keep last valid results
  via `fn=true`

Acceptance and speed search:
- Stable candidate requires repeated confirmation runs (`search -> confirm`)
- After stable confirmation, speed probes try faster gains
- If a faster candidate fails re-confirmation, autotune steps back one probe level (not to the beginning)

Core query parameters:
- `a1`: coarse target weight
- `a2`: total cycle target time
- `a3`: fine target weight
- `a5`: max runs per stage
- `a6`: coarse weight tolerance
- `a9`: fine weight tolerance
- `a7`: time tolerance

Additional outputs:
- `/rest/autotune_trials`
- `/rest/autotune_telemetry`

Notes:
- `a8` (`auto_apply`) and `ee` (`save_to_nvs`) are still accepted by REST for compatibility.
- In current Web UI flow, final PID write is done explicitly after autotune completion (`/rest/profile_config?...&ee=true`).

## Flow Model

The flow model learns motor speed to powder flow relation in normal operation.

Implemented:
- Sample recording during charge and autotune runs
- Per-bin filtering and quality-weighted EMA updates
- Delay and inertia estimation
- Per-profile NVS persistence
- Confidence/quality metrics

Important current note:
- During autotune, live flow model freezing/shadow merge is still planned and not finalized yet.

Technical details:
- `docs/flow_model_plan.md`
- `docs/flow_model_autotune_control_recommendations.md`

## Text and Encoding Policy

- Use English for code comments, logs, UI text, and docs.
- Use UTF-8 encoding and LF line endings.

Run repository text check:

```bash
python scripts/check_text_quality.py
```
