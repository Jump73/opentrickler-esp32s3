# OpenTrickler ESP32-S3 Port - Comprehensive Comparison Report

**Report Date:** 2026-02-08 (Updated)
**Original Project Location:** `C:\Users\kdzia\OpenTrickler_org-EAMARS`
**ESP32-S3 Port Location:** `C:\Users\kdzia\ESPRESS\opentrickler-esp32s3`

---

## Project Goal

**Primary Objective:** Replace the "brain" of the OpenTrickler controller by swapping the Raspberry Pi Pico W with an ESP32-S3 module (in Pico form factor), while maintaining **all original functionality** of the powder dispenser controller.

### Hardware Strategy
- **Drop-in Replacement:** ESP32-S3 in Pico-compatible footprint replaces the original Pico W
- **Preserve PCB Design:** All existing circuitry, motor drivers, display, encoder, and scale interface remain unchanged
- **Pin Compatibility:** ESP32-S3 GPIO mapping adapted to match original Pico W pinout where possible

### Software Migration Guidelines

The port follows these specific implementation requirements:

1. **Display Graphics Library**
   - ❌ Remove: `u8g2` (original monochrome graphics library)
   - ✅ Replace with: **LVGL** (Light and Versatile Graphics Library)
   - Rationale: More modern, feature-rich, better ESP32 support

2. **Motor Control**
   - ❌ Remove: PIO-based stepper control (RP2040 specific)
   - ✅ Replace with: **MCPWM** or **LEDC PWM** (ESP32 hardware peripherals)
   - Rationale: ESP32-S3 lacks PIO, uses native PWM controllers

3. **LED Control (NeoPixel/WS2812)**
   - ❌ Remove: PIO-based WS2812 driver (RP2040 specific)
   - ✅ Replace with: **led_strip component** ([espressif/led_strip](https://components.espressif.com/components/espressif/led_strip))
   - Rationale: ESP32 RMT peripheral better suited for WS2812 timing

4. **HTTP REST Server**
   - ❌ Remove: Custom lwIP-based REST implementation
   - ✅ Replace with: **ESP-IDF RESTful Server** ([example](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server/restful_server))
   - Rationale: Use ESP-IDF native HTTP server with modern REST patterns

5. **WiFi Provisioning**
   - ➕ Add: **ESP Provisioning Tool** integration
   - Rationale: Simplify WiFi setup with BLE/SoftAP provisioning via mobile app

### Code Standards
- ✅ All code comments and documentation must be in **English**
- ✅ Follow ESP-IDF coding style guidelines
- ✅ Use ESP-IDF component structure

---

## Executive Summary

This report compares the original OpenTrickler project (Raspberry Pi Pico W) with the ESP32-S3 port, documenting what functionality exists, what's complete, what's partially implemented, and what's missing.

**Estimated Completion:** ~55-60% of original functionality ported. Motors spin with MCPWM + acceleration ramp, GNG JJB scale reads weight, charge mode state machine runs, encoder + display SPI work. **Blocking issue:** TMC2209 UART communication (0 bytes RX) prevents current/microstep configuration. See Section 11 for detailed debug log.

---

## Implementation Guidelines - Status Check

### Migration Requirements Compliance

| Guideline | Status | Implementation Notes |
|-----------|--------|---------------------|
| **Replace u8g2 with LVGL** | ❌ **Not Started** | SPI driver works, LVGL not added yet |
| **Replace PIO motors with PWM** | ✅ **Complete** | MCPWM variable-speed + acceleration ramp working |
| **Replace PIO LEDs with led_strip** | ❌ **Not Started** | Config exists, no driver integrated |
| **Use ESP-IDF RESTful Server** | ⏳ **Custom Implementation** | Custom REST handler system (works, but not ESP-IDF example) |
| **Add ESP Provisioning Tool** | ❌ **Not Started** | Using custom wizard, no BLE/SoftAP provisioning |
| **All comments in English** | ✅ **Compliant** | All code comments are in English |

### Detailed Status

#### 1. Display Library (u8g2 → LVGL)
- **Current State:** Low-level ST7567 SPI driver only
- **Original:** Uses u8g2 for graphics
- **Target:** Replace with LVGL
- **Priority:** HIGH - Blocks all UI functionality
- **Action Required:**
  - Add LVGL to components
  - Create ST7567 display driver for LVGL
  - Port menu system to LVGL

#### 2. Motor Control (PIO → PWM)
- **Current State:** MCPWM variable-speed STEP generation + software acceleration ramp
- **Original:** PIO state machines for precise timing
- **Target:** MCPWM with software timing ← **DONE**
- **Priority:** ✅ Core motor control working
- **Remaining:**
  - Fix TMC UART for current/microstep config (see Section 11)
  - Add PID-based dynamic speed control

#### 3. LED Control (PIO → led_strip)
- **Current State:** Configuration storage only
- **Original:** PIO-based WS2812 driver
- **Target:** espressif/led_strip component
- **Priority:** MEDIUM - Visual feedback
- **Action Required:**
  - Add led_strip component dependency
  - Implement RMT-based WS2812 output
  - Integrate with charge mode states

#### 4. REST Server (Custom → ESP-IDF Example)
- **Current State:** Custom REST handler system
- **Original:** lwIP-based custom implementation
- **Target:** ESP-IDF RESTful Server example pattern
- **Priority:** LOW - Current solution works
- **Action Required:**
  - **OPTIONAL:** Refactor to match ESP-IDF example architecture
  - Current implementation is functional and maintainable

#### 5. WiFi Provisioning (Wizard → ESP Provisioning)
- **Current State:** Web-based wizard
- **Original:** Web-based wizard
- **Target:** BLE/SoftAP provisioning via mobile app
- **Priority:** LOW - Nice to have
- **Action Required:**
  - Add ESP provisioning component
  - Integrate with mobile app (iOS/Android)
  - Keep wizard as fallback

---

## 1. Core Functionality Modules

### 1.1 Configuration & Storage

| Component | Original | ESP32-S3 Port | Status |
|-----------|----------|---------------|---------|
| **EEPROM Storage** | ✅ CAT24C256 I²C EEPROM | ✅ NVS (ESP32 Flash) | ✅ **Complete** - Architecture changed |
| **Configuration Management** | ✅ eeprom.h with save handlers | ✅ NVS per-component | ✅ **Complete** - Different implementation |
| **Board ID/Unique ID** | ✅ EEPROM-based unique ID | ✅ MAC/Chip ID based | ✅ **Complete** |
| **Config Versioning** | ✅ Per-module revision tracking | ✅ Per-module version field | ✅ **Complete** |

**Notes:**
- Original uses external I²C EEPROM (CAT24C256, 32KB) with memory-mapped regions
- ESP32-S3 uses built-in NVS (Non-Volatile Storage) with namespace-based partitioning
- Both support versioned configurations for backward compatibility

---

## 2. Hardware Drivers

### 2.1 Motor Control (TMC Stepper Drivers)

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **Motor Driver Library** | ✅ Trinamic TMC library (SPI/UART) | ⏳ TMC2209 library ported, UART HAL written | ⚠️ **UART RX blocked** |
| **Step Generation** | ✅ PIO-based (Pico SDK) | ✅ MCPWM-based (variable speed) | ✅ **Complete** |
| **Direction Control** | ✅ GPIO control | ✅ GPIO control | ✅ **Complete** |
| **Enable Control** | ✅ GPIO control (active-low) | ✅ GPIO control (inverted_enable=true) | ✅ **Complete** |
| **UART Communication** | ✅ TMC UART driver (single-wire) | ⏳ HAL written, open-drain approach | ⚠️ **RX 0 bytes - See Section 11** |
| **Current/Microstep Config** | ✅ TMC register control | ⏳ Microsteps=16 (hardware MS pins), no UART config | ⏳ **Partial** |
| **Acceleration Control** | ✅ PID-based velocity ramping | ✅ Software acceleration ramp | ✅ **Complete** |
| **Motor Config (NVS)** | ✅ Full config saved to EEPROM | ✅ Full config saved to NVS (CONFIG_VERSION=3) | ✅ **Complete** |
| **MCPWM Group Separation** | N/A (PIO) | ✅ Coarse=MCPWM_GROUP1, Fine=MCPWM_GROUP0 | ✅ **Complete** |

**Implementation Files:**
- **Original:** `src/motors.h`, `src/motors.c`, TMC library integration via PIO
- **ESP32-S3:** `components/motors/motors.c` (config + MCPWM + TMC init), `components/tmc_drivers/tmc_uart_hal_esp32.c` (UART HAL)

**What Works:**
- ✅ MCPWM-based STEP pulse generation with variable speed (set_speed_rps API)
- ✅ Software acceleration ramp: COARSE_START_RPS=0.3 → COARSE_SPEED_RPS=0.4 at 0.02 rps/tick
- ✅ GPIO direction, enable pins (active-low for TMC2209)
- ✅ MCPWM group separation (coarse/fine on different groups to avoid resource conflicts)
- ✅ Configuration storage (CONFIG_VERSION=3, microsteps=16, current_ma=800)
- ✅ TMC2209 library compiled and linked
- ✅ TMC UART HAL: CRC calculation, read/write datagram formatting

**What's Blocked (TMC UART):**
- ⚠️ UART RX receives 0 bytes even when TX sends on same GPIO16 pin
- ⚠️ GPIO16 raw loopback works (pin is functional), UART TX sends OK, but UART RX gets nothing
- ⚠️ Root cause: likely ESP32 GPIO matrix `oen_sel` conflict between UART peripheral and GPIO controller
- ⚠️ See **Section 11** for full debug history and next steps

---


### 2.2 Scale Drivers (Serial Communication)


| Scale Driver | Original | ESP32-S3 Port | Status |
|--------------|----------|---------------|---------|

| **GNG JJB** | ✅ Implemented | ✅ Implemented | ✅ **Complete** |
| **AND FXi** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **Steinberg SBS** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **GNG JJB** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **USSolid JFDBS** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **JM Science** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **Creedmoor** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **Radwag PS-R2** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **Sartorius** | ✅ Implemented | ⏳ Enum defined, no driver | ❌ **Missing** |
| **Generic Driver** | ✅ Implemented | ⏳ Basic simulator only | ⏳ **Partial - Sim only** |

**Implementation Files:**
- **Original:** `src/scale.h`, individual scale driver files (`and_scale.c`, etc.)
- **ESP32-S3:** `components/scale/`, `components/scale_generic/`

**What Works:**
- ✅ Scale configuration storage (driver type, baudrate)
- ✅ GNG JJB scale driver: UART communication (8N1), weight parsing, tare/calibration commands, update task
- ✅ Scale UART fix: changed from 7N1 to 8N1 frame format (original Pico code had wrong setting)
- ✅ Verbose scale logging reduced (was flooding console at ~10 lines/sec)
- ✅ Generic scale simulator (for testing)
- ✅ Basic scale API structure (get_weight, tare, calibrate)

**What's Missing:**
- ❌ Other scale drivers (AND FXi, Steinberg SBS, USSolid JFDBS, JM Science, Creedmoor, Radwag PS-R2, Sartorius)
- ❌ Protocol parsers for the above models

---

### 2.3 Display (Mini 12864 LCD)

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **Display Library** | ✅ u8g2 (universal graphics) | ⏳ Custom ST7567 driver | 🔧 **Partial** |
| **SPI Communication** | ✅ u8g2 SPI backend | ✅ ESP-IDF SPI driver | ✅ **Complete** |
| **Display Init** | ✅ mini_12864_module.cpp | ⏳ display_st7567.c (basic test) | 🔧 **Partial** |
| **Graphics Rendering** | ✅ u8g2 draw functions | ❌ No graphics lib integrated | ❌ **Missing** |
| **Menu System (MUI)** | ✅ Full MUI integration | ❌ Not ported | ❌ **Missing** |
| **Display Rotation** | ✅ Configurable (0/90/180/270°) | ❌ Not implemented | ❌ **Missing** |
| **Backlight (NeoPixel)** | ✅ RGB backlight control | ⏳ Stub only | ⏳ **Partial** |

**Implementation Files:**
- **Original:** `src/mini_12864_module.cpp`, `src/display.h`, `src/menu.cpp`
- **ESP32-S3:** `components/display_st7567/` (low-level only)

**What Works:**
- ✅ SPI bus initialization
- ✅ Basic ST7567 command writing
- ✅ Test pattern generation

**What's Missing:**
- ❌ u8g2 library integration
- ❌ Text/graphics rendering
- ❌ Menu system (MUI)
- ❌ Display buffer mirroring for web UI
- ❌ Real-time weight/status display

---

### 2.4 Input (Rotary Encoder + Buttons)

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **Rotary Encoder** | ✅ Dual GPIO quadrature | ✅ Implemented | ✅ **Complete** |
| **Encoder Button** | ✅ Integrated with encoder | ✅ Implemented | ✅ **Complete** |
| **Reset Button** | ✅ Separate GPIO | ❌ Not implemented | ❌ **Missing** |
| **Event Queue** | ✅ FreeRTOS queue | ✅ Polling-based | 🔧 **Different approach** |
| **Debouncing** | ✅ Hardware/software debounce | ⏳ Basic debounce | ⏳ **Partial** |
| **REST Control Override** | ✅ REST can trigger events | ❌ Not implemented | ❌ **Missing** |

**Implementation Files:**
- **Original:** `src/mini_12864_module.cpp` (button_wait_for_input)
- **ESP32-S3:** `components/input_encoder/`

**What Works:**
- ✅ Encoder rotation detection (CW/CCW)
- ✅ Button press/release/click detection
- ✅ Position tracking

**What's Missing:**
- ❌ Integration with menu system
- ❌ REST-triggered button events
- ❌ Reset button support

---

### 2.5 LED Control (NeoPixel RGB)

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **NeoPixel Library** | ✅ PIO-based WS2812 driver | ⏳ Config only, no driver | ⏳ **Partial** |
| **Display Backlight** | ✅ 3 LEDs in chain (Mini 12864) | ⏳ Config defined | ❌ **Missing** |
| **External LED (PWM3)** | ✅ Mirrors LED1 | ❌ Not implemented | ❌ **Missing** |
| **Charge Mode Colors** | ✅ Dynamic color based on state | ⏳ Config stored | ❌ **Missing** |
| **LED Configuration** | ✅ Chain count, RGBW, color order | ✅ Full config in NVS | ✅ **Complete** |

**Implementation Files:**
- **Original:** `src/neopixel_led.h`, PIO state machine
- **ESP32-S3:** `components/neopixel_led/`

**What Works:**
- ✅ LED configuration storage (colors, chain count, RGBW/RGB)
- ✅ Color definitions and macros
- ✅ URL-encoded hex color parsing

**What's Missing:**
- ❌ RMT/SPI-based NeoPixel driver (ESP32 equivalent)
- ❌ Actual LED color output
- ❌ State-based color control

---

### 2.6 Servo Gate Control

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **PWM Servo Driver** | ✅ Hardware PWM on 2 channels | ❌ Not implemented | ❌ **Missing** |
| **Gate State Machine** | ✅ OPEN/CLOSE/DISABLED states | ❌ Not implemented | ❌ **Missing** |
| **Servo Speed Control** | ✅ Slow open/close ramping | ❌ Not implemented | ❌ **Missing** |
| **Dual Servo Support** | ✅ 2 independent servos | ❌ Not implemented | ❌ **Missing** |
| **Configuration Storage** | ✅ Duty cycle, speed settings | ❌ Component exists but empty | ❌ **Missing** |

**Implementation Files:**
- **Original:** `src/servo_gate.h`
- **ESP32-S3:** `components/servo_gate/` (empty directory with include stub)

**Status:** Complete component missing - directory structure created but no implementation.

---

## 3. Application Logic

### 3.1 Charge Mode (Auto Dispense)

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **State Machine** | ✅ 5 states (EXIT, WAIT_ZERO, COMPLETE, etc.) | ✅ Full state machine running | ✅ **Complete** |
| **PID Control** | ✅ Dual PID (coarse/fine) | ⏳ Threshold-based (no PID yet) | ⏳ **Partial** |
| **Weight Monitoring** | ✅ Real-time scale reading | ✅ Reads from GNG JJB scale | ✅ **Complete** |
| **Motor Control** | ✅ Automatic speed adjustment | ✅ Coarse/fine with acceleration ramp | ✅ **Complete** |
| **Precharge Mode** | ✅ Fast initial dispense | ⏳ Config stored, not implemented | ❌ **Missing** |
| **Threshold Detection** | ✅ Coarse/fine stop thresholds | ✅ Uses charge_mode_config thresholds | ✅ **Complete** |
| **LED Feedback** | ✅ Color indicates state | ❌ Not implemented | ❌ **Missing** |
| **Display Rendering** | ✅ Real-time weight/timer display | ❌ Not implemented | ❌ **Missing** |
| **Configuration** | ✅ Full config in EEPROM | ✅ Full config in NVS | ✅ **Complete** |
| **Stability Detection** | ✅ Part of state machine | ✅ Configurable stable counts & tolerances | ✅ **Complete** |
| **Cup Remove/Return** | ✅ Detect empty cup removal | ✅ Threshold-based detection | ✅ **Complete** |

**Implementation Files:**
- **Original:** `src/charge_mode.cpp`, `src/charge_mode.h`
- **ESP32-S3:** `components/charge_mode/charge_mode.c`, `components/charge_mode/include/charge_mode.h`

**What Works:**
- ✅ Full state machine: WAIT_FOR_ZERO → DISPENSING (coarse) → DISPENSING (fine) → SETTLING → COMPLETE → CUP_REMOVED → WAIT_FOR_ZERO
- ✅ Coarse motor runs at 0.4 rps with acceleration ramp (start 0.3 rps, +0.02 rps/tick)
- ✅ Fine motor runs at 0.2 rps with acceleration ramp (start 0.05 rps)
- ✅ Scale weight integration (reads GNG JJB in real time)
- ✅ Stability detection: ZERO_STABLE_COUNT=20, SETTLE_STABLE_COUNT=10
- ✅ Cup removal detection (threshold: -0.3g)
- ✅ Configuration storage (thresholds, colors, precharge settings)
- ✅ FreeRTOS task with 50ms tick period

**What's Missing:**
- ❌ PID controller (currently fixed speed, not dynamic)
- ❌ Precharge mode (fast initial dispense)
- ❌ Display updates
- ❌ LED status updates

---

### 3.2 Cleanup Mode (Manual Trickler)

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **Manual Speed Control** | ✅ User-adjustable trickler | ⏳ State machine defined | ⏳ **Partial** |
| **Motor Control** | ✅ Direct motor speed setting | ❌ Not implemented | ❌ **Missing** |
| **Display UI** | ✅ Speed display and control | ❌ Not implemented | ❌ **Missing** |
| **State Management** | ✅ ENTER/EXIT states | ✅ States defined | ⏳ **Partial** |

**Implementation Files:**
- **Original:** `src/cleanup_mode.cpp`, `src/cleanup_mode.h`
- **ESP32-S3:** `components/cleanup_mode/`

**Status:** Basic structure exists but no actual motor control or UI.

---

### 3.3 Profile Management (PID Presets)

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **Profile Storage** | ✅ 8 profiles in EEPROM | ✅ 8 profiles in NVS | ✅ **Complete** |
| **PID Parameters** | ✅ Separate coarse/fine PID | ✅ Stored in NVS | ✅ **Complete** |
| **Flow Speed Limits** | ✅ Min/max speed per motor | ✅ Stored in NVS | ✅ **Complete** |
| **Profile Selection** | ✅ Active profile tracking | ✅ Current index tracked | ✅ **Complete** |
| **Profile Names** | ✅ 16-char custom names | ✅ 16-char names | ✅ **Complete** |
| **Menu UI** | ✅ Profile selection menu | ❌ Not implemented | ❌ **Missing** |

**Implementation Files:**
- **Original:** `src/profile.h`
- **ESP32-S3:** `components/profile/`

**Status:** Data structures and storage are complete, but menu/UI integration missing.

---

### 3.4 Menu System

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **MUI Framework** | ✅ Full MUI (Menu UI) system | ❌ Not ported | ❌ **Missing** |
| **Form Navigation** | ✅ Multi-level menus | ❌ Not implemented | ❌ **Missing** |
| **Field Editing** | ✅ Value editing with encoder | ❌ Not implemented | ❌ **Missing** |
| **State Transitions** | ✅ Menu → App mode transitions | ❌ Not implemented | ❌ **Missing** |
| **FreeRTOS Task** | ✅ Dedicated menu_task | ❌ Not implemented | ❌ **Missing** |

**Implementation Files:**
- **Original:** `src/menu.cpp`, `src/menu.h` (MUI integration)
- **ESP32-S3:** None

**Status:** Completely missing - no menu system ported.

---

## 4. Communication & Connectivity

### 4.1 WiFi Management

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **WiFi Library** | ✅ Pico W (CYW43) | ✅ ESP-IDF WiFi | ✅ **Complete** |
| **Station Mode (STA)** | ✅ Connect to existing network | ✅ Implemented | ✅ **Complete** |
| **Access Point Mode (AP)** | ✅ Create own network | ✅ Implemented | ✅ **Complete** |
| **Auto-start Logic** | ✅ STA first, fallback to AP | ✅ Implemented | ✅ **Complete** |
| **Configuration Storage** | ✅ SSID, password, auth, timeout | ✅ Full config in NVS | ✅ **Complete** |
| **Authentication Types** | ✅ OPEN, WPA, WPA2 | ✅ All types supported | ✅ **Complete** |

**Implementation Files:**
- **Original:** `src/wireless.h`
- **ESP32-S3:** `components/wifi_manager/`

**Status:** Fully ported and working.

---

### 4.2 HTTP Server & REST API

| Feature | Original (Pico W) | ESP32-S3 Port | Status |
|---------|-------------------|---------------|---------|
| **HTTP Server** | ✅ lwIP httpd | ✅ esp_http_server | ✅ **Complete** - Different library |
| **REST Handler System** | ✅ Custom REST routing | ✅ Custom handler registration | ✅ **Complete** |
| **Query Parameter Parsing** | ✅ Automatic parsing | ✅ Automatic parsing | ✅ **Complete** |
| **JSON Responses** | ✅ Manual JSON formatting | ✅ Manual JSON formatting | ✅ **Complete** |

### 4.3 REST Endpoints

| Endpoint | Original | ESP32-S3 | Status |
|----------|----------|----------|---------|
| **/** (Root) | ✅ Web portal/wizard | ✅ Smart routing (AP/STA) | ✅ **Complete** |
| **/wizard** | ✅ Setup wizard page | ✅ Implemented | ✅ **Complete** |
| **/mobile** | ✅ Mobile-friendly portal | ✅ Implemented | ✅ **Complete** |
| **/display_mirror** | ✅ Live display view | ✅ Page served (no backend) | ⏳ **Partial** |
| **/rest/wireless_config** | ✅ WiFi settings | ✅ Fully implemented | ✅ **Complete** |
| **/rest/system_control** | ✅ Reboot, save, erase NVS | ✅ Fully implemented | ✅ **Complete** |
| **/rest/scale_config** | ✅ Scale driver settings | ✅ Config only (no driver) | ⏳ **Partial** |
| **/rest/scale_action** | ✅ Tare, calibrate commands | ✅ Stub only | ⏳ **Partial** |
| **/rest/coarse_motor_config** | ✅ Motor settings | ✅ Config only | ⏳ **Partial** |
| **/rest/fine_motor_config** | ✅ Motor settings | ✅ Config only | ⏳ **Partial** |
| **/rest/charge_mode_config** | ✅ Charge settings | ✅ Config only | ⏳ **Partial** |
| **/rest/charge_mode_state** | ✅ Start/stop charging | ✅ Stub only | ⏳ **Partial** |
| **/rest/cleanup_mode_state** | ✅ Manual trickler | ✅ Stub only | ⏳ **Partial** |
| **/rest/profile_config** | ✅ Edit profiles | ✅ Fully implemented | ✅ **Complete** |
| **/rest/profile_summary** | ✅ List profiles | ✅ Fully implemented | ✅ **Complete** |
| **/rest/neopixel_led_config** | ✅ LED settings | ✅ Config only (no driver) | ⏳ **Partial** |
| **/rest/servo_gate_config** | ✅ Servo settings | ❌ Not implemented | ❌ **Missing** |
| **/rest/servo_gate_state** | ✅ Open/close gate | ❌ Not implemented | ❌ **Missing** |
| **/rest/button_control** | ✅ Simulate button press | ❌ Not implemented | ❌ **Missing** |
| **/rest/mini_12864_config** | ✅ Display rotation | ❌ Not implemented | ❌ **Missing** |
| **/display_buffer** | ✅ Get display bitmap | ❌ Not implemented | ❌ **Missing** |

**Implementation Files:**
- **Original:** `src/rest_endpoints.c` (registration), individual modules
- **ESP32-S3:** `components/rest_handlers/`, `components/http_server/`, `main/web_test.c`

---

### 4.4 Web UI Pages

| Page | Original | ESP32-S3 | Status |
|------|----------|----------|---------|
| **Web Portal** | ✅ Full SPA with controls | ✅ HTML embedded | ✅ **Complete** |
| **Setup Wizard** | ✅ WiFi + initial setup | ✅ HTML embedded | ✅ **Complete** |
| **Display Mirror** | ✅ Live LCD view in browser | ✅ HTML only (no backend) | ⏳ **Partial** |

**Implementation Files:**
- **Original:** HTML in `resources/`, embedded via build script
- **ESP32-S3:** `html/` → `main/generated/*.html.h`

**Status:** HTML pages are ported and served, but backend integration incomplete.

---

## 5. System Architecture Comparison

### 5.1 Hardware Platform

| Aspect | Original (Pico W) | ESP32-S3 Port |
|--------|-------------------|---------------|
| **MCU** | RP2040 (Dual Cortex-M0+, 133 MHz) | ESP32-S3 (Dual Xtensa LX7, 240 MHz) |
| **RAM** | 264 KB SRAM | 512 KB SRAM |
| **Flash** | 2 MB external (Pico W) | 8 MB external flash (typical) |
| **WiFi** | CYW43439 (802.11n) | Built-in WiFi 4 (802.11n) |
| **PIO** | 2× PIO blocks (8 state machines) | None (uses RMT, MCPWM instead) |
| **UART** | 2× UART | 3× UART |
| **SPI** | 2× SPI | 4× SPI (SPI2, SPI3 + HSPI, VSPI) |
| **I²C** | 2× I²C | 2× I²C |
| **PWM** | 8× PWM slices (16 channels) | LEDC (8 channels) + MCPWM (6 channels) |

### 5.2 Software Stack

| Layer | Original | ESP32-S3 Port |
|-------|----------|---------------|
| **RTOS** | FreeRTOS (bundled with Pico SDK) | FreeRTOS (ESP-IDF) |
| **Build System** | CMake + Pico SDK | CMake + ESP-IDF |
| **HTTP Stack** | lwIP + httpd | ESP-IDF HTTP server |
| **Storage** | External EEPROM (I²C) | NVS (internal flash) |
| **Graphics** | u8g2 (universal library) | Custom ST7567 driver (low-level) |

---

## 6. Missing Components - Prioritized List

### 6.1 Critical (Blocks Core Functionality)

1. ⚠️ **TMC UART RX Signal Routing** ← **CURRENT BLOCKER**
   - Location: `components/tmc_drivers/tmc_uart_hal_esp32.c`
   - Problem: UART TX sends on GPIO16, but UART RX gets 0 bytes on same pin
   - Impact: Cannot configure TMC2209 current/microsteps/stealthChop via UART
   - See: **Section 11** for full debug history and next steps

2. ❌ **Display Graphics Integration (LVGL)**
   - Location: `components/display_st7567/`
   - Need: Add LVGL library, create ST7567 display driver for LVGL
   - Impact: Cannot show UI, weight, or status on LCD

3. ❌ **Scale Serial Drivers (other models)**
   - Location: `components/scale/`
   - Need: UART protocol parsers for AND FXi, Steinberg SBS, USSolid JFDBS, JM Science, Creedmoor, Radwag PS-R2, Sartorius
   - Impact: Only GNG JJB scale works

### 6.2 High Priority (Essential Features)

4. ❌ **Menu System (MUI → LVGL)**
   - Location: New component needed
   - Need: LVGL-based menu system (replaces u8g2 MUI)
   - Impact: Cannot navigate settings or modes via LCD+encoder

5. ❌ **NeoPixel LED Driver**
   - Location: `components/neopixel_led/`
   - Need: RMT-based WS2812 driver (espressif/led_strip component)
   - Impact: No visual status feedback

6. ❌ **Servo Gate Control**
   - Location: `components/servo_gate/`
   - Need: LEDC PWM servo driver
   - Impact: Cannot automate powder funnel gate

7. ⏳ **Charge Mode PID Control**
   - Location: `components/charge_mode/`
   - Need: Dynamic speed adjustment based on weight delta (replaces fixed speeds)
   - Impact: Less precise dispensing than original

### 6.3 Medium Priority (Enhanced Functionality)

8. ⏳ **Display Buffer Mirroring**
   - Location: `components/display_st7567/` + REST handler
   - Need: Export framebuffer as bitmap
   - Impact: Cannot view display remotely

9. ❌ **Button REST Override**
   - Location: `components/input_encoder/` + REST
   - Need: REST API to simulate button presses
   - Impact: Cannot control from web without buttons

10. ❌ **Cleanup Mode Motor Control**
    - Location: `components/cleanup_mode/`
    - Need: Link to motor control
    - Impact: Manual trickler mode non-functional

### 6.4 Low Priority (Nice to Have)

11. ❌ **Display Rotation Configuration**
    - Depends on LVGL integration

12. ❌ **ESP Provisioning Tool**
    - BLE/SoftAP provisioning via mobile app (current web wizard works)

---

## 7. What's Actually Working Right Now

### ✅ Fully Functional Components

1. **WiFi Management** - Can connect to WiFi (STA) or create AP
2. **HTTP Server** - Serves web pages and REST API
3. **NVS Configuration Storage** - All config modules save/load correctly (CONFIG_VERSION tracked)
4. **Profile System** - Full CRUD via REST API
5. **System Control** - Reboot, save all settings, erase NVS
6. **Web UI Pages** - Portal, wizard, and display mirror HTML served
7. **REST API Endpoints** - All endpoints registered and parse parameters
8. **Motor STEP Generation** - MCPWM variable-speed STEP pulses with acceleration ramp
9. **Motor Direction/Enable** - GPIO control with active-low enable (TMC2209)
10. **GNG JJB Scale** - UART 8N1 communication, weight reading, tare/calibration
11. **Charge Mode State Machine** - Full dispense cycle: zero-wait → coarse → fine → settle → complete → cup-remove
12. **Input Encoder** - Rotation + button press detection working

### ⏳ Partially Working Components

1. **TMC2209 UART** - HAL written with open-drain approach, TX sends OK, **RX gets 0 bytes** (blocking issue)
2. **Display** - SPI communication works, no graphics library (LVGL not yet added)
3. **Charge Mode PID** - Currently uses fixed speeds + thresholds, no dynamic PID control
4. **Cleanup Mode** - Configuration stored, basic state machine, no motor integration

### ❌ Non-Functional Components

1. **TMC UART RX** - Cannot read TMC registers (blocks current/microstep configuration)
2. **Menu System** - Completely missing (needs LVGL first)
3. **NeoPixel LEDs** - No WS2812/RMT driver
4. **Servo Gate** - Empty component
5. **Display Rendering** - No graphics output (needs LVGL)
6. **Other Scale Drivers** - Only GNG JJB works; AND FXi, Steinberg, etc. not ported

---

## 8. Hardware Integration Requirements

### 🔧 GPIO Pin Mapping Status

All pins are defined in `components/board_expansion/include/board_pins.h`.
Reference mapping: `PIN_MAPPING_TABLE.txt` and `PICO_W-ESP32_S3_PICO_PIN_MAPPING.md`.

| Function | ESP32-S3 GPIO | Physical Pin | Status |
|----------|--------------|--------------|---------|
| **Display MOSI** | GPIO2 | Pin 25 (GP19) | ✅ Tested |
| **Display SCK** | GPIO40 | Pin 20 (GP15) | ✅ Tested |
| **Display CS** | GPIO39 | Pin 19 (GP14) | ✅ Tested |
| **Display DC/A0** | GPIO4 | Pin 26 (GP20) | ✅ Tested |
| **Display RST** | GPIO5 | Pin 27 (GP21) | ✅ Tested |
| **Display MISO** | GPIO38 | Pin 17 (GP13) | ✅ Assigned (NC) |
| **Encoder A** | GPIO35 | Pin 14 (GP10) | ✅ Tested |
| **Encoder B** | GPIO36 | Pin 15 (GP11) | ✅ Tested |
| **Encoder BTN** | GPIO6 | Pin 29 (GP22) | ✅ Tested |
| **Encoder RST** | GPIO37 | Pin 16 (GP12) | ✅ Assigned |
| **Coarse STEP** | GPIO14 | Pin 5 (GP03) | ✅ Working (MCPWM) |
| **Coarse DIR** | GPIO13 | Pin 4 (GP02) | ✅ Working |
| **Coarse EN** | GPIO17 | Pin 9 (GP06) | ✅ Working |
| **Fine STEP** | GPIO33 | Pin 11 (GP08) | ✅ Working (MCPWM) |
| **Fine DIR** | GPIO18 | Pin 10 (GP07) | ✅ Working |
| **Fine EN** | GPIO34 | Pin 12 (GP09) | ✅ Working |
| **TMC UART TX/RX** | GPIO16 | Pin 7 (GP05) | ⚠️ **TX OK, RX 0 bytes** |
| **~~TMC UART (old)~~** | ~~GPIO15~~ | ~~Pin 6~~ | ❌ **3V3_Out - NOT usable GPIO!** |
| **Scale UART TX** | GPIO11 | Pin 1 (GP00) | ✅ Working (8N1) |
| **Scale UART RX** | GPIO12 | Pin 2 (GP01) | ✅ Working |
| **NeoPixel** | GPIO9 | Pin 34 (GP28) | ⏳ Assigned, not tested |
| **Servo0 PWM** | GPIO8 | Pin 32 (GP27) | ⏳ Assigned |
| **Servo1 PWM** | GPIO9 | Pin 34 (GP28) | ⏳ Assigned (conflicts NeoPixel) |
| **EEPROM SDA** | GPIO7 | Pin 31 (GP26) | ⏳ Assigned |
| **EEPROM SCL** | GPIO8 | Pin 32 (GP27) | ⏳ Assigned |

**Critical Notes:**
- GPIO15 (Pin 6) is **3V3_Out** on ESP32-S3-PICO-V3-02 - NOT a usable GPIO! All early TMC UART attempts on this pin gave 0 bytes.
- UART allocation: UART0=console, UART1=TMC motors, UART2=scale
- MCPWM allocation: GROUP0=fine motor, GROUP1=coarse motor (separated to avoid resource conflicts)

---

## 9. Recommended Next Steps

### Immediate: Fix TMC UART RX (Blocking Issue)
1. **Try `uart_set_loop_back(UART1, true)`** - ESP-IDF API for internal loopback testing
2. **Try UART2 instead of UART1** - rule out UART peripheral conflict
3. **Force `oen_sel=1`** via direct register write to `GPIO_FUNCn_OUT_SEL_CFG_REG`
4. **Try push-pull instead of open-drain** - test if issue is OD-specific
5. **Check `func_out_sel_cfg`** register to verify UART TX output signal is correctly routed

### Phase 1: Display + LVGL
1. Add LVGL to components
2. Create ST7567 display driver for LVGL
3. Port basic weight/status display
4. Integrate encoder with LVGL input driver

### Phase 2: Remaining Drivers
5. NeoPixel LED driver (RMT/led_strip)
6. Servo gate control (LEDC PWM)
7. Additional scale drivers (AND FXi, Steinberg, etc.)

### Phase 3: UI & Polish
8. LVGL-based menu system
9. Charge mode PID control
10. Cleanup mode motor integration
11. Display buffer mirroring for web UI

---

## 10. File Structure Summary

### Original Project Structure
```
OpenTrickler_org-EAMARS/
├── src/                    # All C++ source files
│   ├── app.cpp            # Main entry point
│   ├── menu.cpp           # MUI menu system
│   ├── charge_mode.cpp    # Auto dispense logic
│   ├── motors.h           # TMC motor control
│   ├── scale.h            # Scale drivers
│   └── ...
├── library/               # External dependencies
│   ├── u8g2/              # Graphics library
│   ├── Trinamic-library/  # TMC drivers
│   └── pico-sdk/
└── targets/               # Hardware configs
    └── raspberrypi_pico_w_config.h
```

### ESP32-S3 Port Structure
```
opentrickler-esp32s3/
├── components/            # ESP-IDF components
│   ├── charge_mode/       # ✅ Full state machine + motor/scale integration
│   ├── cleanup_mode/      # ⏳ Config only
│   ├── motors/            # ✅ MCPWM + acceleration + TMC init
│   ├── tmc_drivers/       # ⏳ TMC2209 lib + UART HAL (RX blocked)
│   ├── scale/             # ✅ GNG JJB driver working
│   ├── scale_generic/     # ⏳ Simulator only
│   ├── board_expansion/   # ✅ Pin definitions (board_pins.h)
│   ├── display_st7567/    # ⏳ Low-level driver
│   ├── input_encoder/     # ✅ Complete
│   ├── neopixel_led/      # ⏳ Config only
│   ├── profile/           # ✅ Complete
│   ├── wifi_manager/      # ✅ Complete
│   ├── http_server/       # ✅ Complete
│   ├── rest_handlers/     # ✅ Complete
│   ├── system_control/    # ✅ Complete
│   └── servo_gate/        # ❌ Empty
├── main/
│   ├── web_test.c         # ✅ Working web UI test
│   └── generated/         # ✅ Embedded HTML pages
└── html/                  # Source HTML files
```

---

## Conclusion

The ESP32-S3 port has made significant progress across all layers:

**Infrastructure layer** - ✅ Complete:
- ✅ Configuration storage (NVS replacing EEPROM)
- ✅ WiFi and HTTP server
- ✅ REST API framework
- ✅ Web UI pages

**Hardware integration layer** - ⏳ Mostly working:
- ✅ Motor STEP generation (MCPWM variable-speed + acceleration ramp)
- ✅ Motor GPIO control (direction, enable with active-low)
- ✅ Scale communication (GNG JJB UART 8N1)
- ✅ Display SPI communication
- ✅ Encoder input
- ⚠️ TMC UART RX blocked (see Section 11)
- ❌ NeoPixel LED output
- ❌ Display graphics (LVGL)

**Application logic layer** - ⏳ Core working:
- ✅ Charge mode state machine runs full dispense cycle
- ✅ Scale + motor integration in charge mode
- ⏳ PID control not yet implemented (fixed speeds)
- ❌ Menu system not ported (needs LVGL)

**Estimated Completion:** ~55-60% of original functionality ported. The project can physically dispense powder (motors spin, scale reads weight, charge mode state machine controls the cycle). The main blocker is TMC UART RX for driver configuration, and LVGL for the display UI.

---

## Notes

- URL-encoded hex colors (`%23` for `#`) are properly handled in charge_mode and neopixel_led
- WiFi smart root handler correctly shows wizard in AP mode and portal in STA mode
- All REST endpoints register and parse parameters correctly
- NVS storage is working reliably for all components
- Build system: Cannot build from CLI (MSys/Mingw not supported). User builds manually via ESP-IDF Command Prompt.
- Scale UART was fixed from 7N1 to 8N1 (original Pico code had wrong frame format)
- CONFIG_VERSION=3 in motors.c forces NVS reset of stale motor configs on update

---

## 11. TMC2209 UART Debug Log (Current Blocker)

### Problem Statement
TMC2209 uses single-wire UART (PDN_UART pin). The ESP32-S3 port needs to TX and RX on the **same GPIO pin** (GPIO16). Currently, UART TX sends successfully, but UART RX receives **0 bytes** - not even the echo of the TX data.

### Pin History
| Attempt | Pin | Result | Root Cause |
|---------|-----|--------|------------|
| 1. Two-pin: TX=GPIO15, RX=GPIO16 | GPIO15+16 | 0 bytes | GPIO15 = 3V3_Out on ESP32-S3-PICO (NOT a GPIO!) |
| 2. Single-wire: GPIO15 | GPIO15 | 0 bytes | Same: GPIO15 is power pin |
| 3. Single-wire: GPIO16 | GPIO16 | TX OK, RX 0 bytes | UART signal routing issue |

### Diagnostic Results on GPIO16
```
I (1654) TMC_UART: === TMC UART DIAGNOSTIC START (GPIO16) ===
I (1654) TMC_UART: GPIO16 loopback: write=1 read=1, write=0 read=0 OK    ← Pin works!
I (1664) TMC_UART: UART1 configured: TX+RX on GPIO16, baud=250000, open-drain
I (1674) TMC_UART: UART TX test: write_ret=1, wait_tx_done=OK              ← TX works!
I (1734) TMC_UART: UART RX test: buffered=0, read_ret=0, rx_byte=0x00 NO ECHO  ← RX fails!
I (1734) TMC_UART: === TMC UART DIAGNOSTIC END ===
```

### What We Know
1. **GPIO16 is physically functional** - raw GPIO loopback (write HIGH, read HIGH; write LOW, read LOW) works
2. **UART1 TX peripheral works** - `uart_write_bytes()` returns 1, `uart_wait_tx_done()` returns OK
3. **UART1 RX gets nothing** - `uart_get_buffered_data_len()` = 0 after TX completes on same pin
4. **The pin IS connected to TMC2209** - it's on the physical bus

### Current Implementation Approach
```c
// 1. Configure UART with TX only (avoid RX killing TX output)
uart_set_pin(UART1, GPIO16, UART_PIN_NO_CHANGE, NO_CHANGE, NO_CHANGE);

// 2. Install UART driver
uart_driver_install(UART1, 256, 256, 0, NULL, 0);

// 3. Manually route pin input to UART1 RX signal
esp_rom_gpio_connect_in_signal(GPIO16, U1RXD_IN_IDX, false);

// 4. Set open-drain mode (allows TMC to pull bus LOW for response)
gpio_set_direction(GPIO16, GPIO_MODE_INPUT_OUTPUT_OD);
gpio_pullup_en(GPIO16);
```

### Root Cause Analysis
The likely issue is **`oen_sel` conflict** in the ESP32 GPIO matrix:
- `uart_set_pin()` for TX calls `esp_rom_gpio_connect_out_signal()` which sets `oen_sel=0` (UART peripheral controls output enable)
- `gpio_set_direction()` uses `GPIO_ENABLE` register (GPIO controller)
- When `oen_sel=0`, the GPIO_ENABLE register is **ignored** - the UART peripheral's OEN signal controls whether the pin drives
- This may prevent the pin from being readable as input even in INPUT_OUTPUT_OD mode

### Approaches NOT Yet Tried
1. **`uart_set_loop_back(UART1, true)`** - ESP-IDF internal loopback API
2. **UART2 instead of UART1** - rule out UART1-specific issue
3. **Force `oen_sel=1`** via direct register write: `GPIO.func_out_sel_cfg[16].oen_sel = 1`
4. **Push-pull instead of open-drain** - test if issue is OD-specific
5. **Let `uart_set_pin()` handle both TX and RX**, then fix with `gpio_set_direction(INPUT_OUTPUT)` afterward
6. **Use `gpio_ll` or `gpio_hal` functions** for lower-level control
7. **Check UART1 clock gating** - ensure UART1 RX clock is enabled

### Reference: Original Pico Implementation
```c
// Original uses separate TX (GP4) and RX (GP5) pins, both connected to TMC PDN_UART
// RX is toggled on/off via UART hardware register:
static void _enable_uart_rx(uart_inst_t *uart, bool state) {
    hw_write_masked(&uart_get_hw(uart)->cr, state ? UART_UARTCR_RXE_BITS : 0, UART_UARTCR_RXE_BITS);
}
// Before sending: disable RX. After sending: enable RX, wait for TMC response.
```

### Files Involved
- `components/tmc_drivers/tmc_uart_hal_esp32.c` - UART HAL (contains diagnostic code)
- `components/board_expansion/include/board_pins.h` - Pin definitions
- `PIN_MAPPING_TABLE.txt` - Hardware pin mapping reference
- `PICO_W-ESP32_S3_PICO_PIN_MAPPING.md` - Detailed pin-to-pin mapping

**Last Updated:** 2026-02-08
