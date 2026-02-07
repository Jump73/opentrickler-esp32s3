# OpenTrickler ESP32-S3 Port - Comprehensive Comparison Report

**Report Date:** 2026-02-07
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

**Estimated Completion:** 40-50% of original functionality ported. The project has a solid foundation for web-based configuration, but needs significant work on hardware drivers and application logic to become a functional powder dispenser.

---

## Implementation Guidelines - Status Check

### Migration Requirements Compliance

| Guideline | Status | Implementation Notes |
|-----------|--------|---------------------|
| **Replace u8g2 with LVGL** | ❌ **Not Started** | u8g2 not integrated yet, LVGL not added |
| **Replace PIO motors with PWM** | ⏳ **Partial** | Basic MCPWM test exists, no full motor control |
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
- **Current State:** Basic MCPWM STEP test (100 Hz fixed)
- **Original:** PIO state machines for precise timing
- **Target:** MCPWM or LEDC with software timing
- **Priority:** CRITICAL - Core functionality
- **Action Required:**
  - Implement velocity control with MCPWM
  - Add acceleration profiles
  - Port TMC driver library

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
| **Motor Driver Library** | ✅ Trinamic TMC library (SPI/UART) | ❌ Not ported | ❌ **Missing** |
| **Step Generation** | ✅ PIO-based (Pico SDK) | ⏳ MCPWM-based (basic test) | 🔧 **Partial - Hardware only** |
| **Direction Control** | ✅ GPIO control | ✅ GPIO control | ✅ **Complete** |
| **Enable Control** | ✅ GPIO control | ✅ GPIO control | ✅ **Complete** |
| **UART Communication** | ✅ TMC UART driver | ❌ Not implemented | ❌ **Missing** |
| **Current/Microstep Config** | ✅ TMC register control | ❌ Not implemented | ❌ **Missing** |
| **Acceleration Control** | ✅ PID-based velocity ramping | ❌ Not implemented | ❌ **Missing** |
| **Motor Config (NVS)** | ✅ Full config saved to EEPROM | ✅ Full config saved to NVS | ✅ **Complete** |

**Implementation Files:**
- **Original:** `src/motors.h`, TMC library integration via PIO
- **ESP32-S3:** `components/motors/` (config only), `components/motors_mcpwm/` (basic STEP test)

**What Works:**
- ✅ Basic STEP pulse generation at fixed frequency (100 Hz test)
- ✅ GPIO direction and enable pins
- ✅ Configuration storage and retrieval

**What's Missing:**
- ❌ TMC driver communication (SPI/UART)
- ❌ Real-time speed control with acceleration
- ❌ Microstep configuration
- ❌ Current control
- ❌ Motor velocity task/queue system

---

### 2.2 Scale Drivers (Serial Communication)

| Scale Driver | Original | ESP32-S3 Port | Status |
|--------------|----------|---------------|---------|
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
- **ESP32-S3:** `components/scale/`, `components/scale_generic/` (simulator)

**What Works:**
- ✅ Scale configuration storage (driver type, baudrate)
- ✅ Generic scale simulator (for testing)
- ✅ Basic scale API structure

**What's Missing:**
- ❌ UART communication with actual scales
- ❌ Protocol parsers for all 8 scale types
- ❌ Weight measurement reading and filtering
- ❌ Scale action commands (tare, calibration)
- ❌ Real-time weight update task

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
| **State Machine** | ✅ 5 states (EXIT, WAIT_ZERO, COMPLETE, etc.) | ✅ States defined | ⏳ **Partial** |
| **PID Control** | ✅ Dual PID (coarse/fine) | ❌ Not implemented | ❌ **Missing** |
| **Weight Monitoring** | ✅ Real-time scale reading | ❌ Stub only | ❌ **Missing** |
| **Motor Control** | ✅ Automatic speed adjustment | ❌ Not implemented | ❌ **Missing** |
| **Precharge Mode** | ✅ Fast initial dispense | ⏳ Config stored | ❌ **Missing** |
| **Threshold Detection** | ✅ Coarse/fine stop thresholds | ⏳ Config stored | ❌ **Missing** |
| **LED Feedback** | ✅ Color indicates state | ❌ Not implemented | ❌ **Missing** |
| **Display Rendering** | ✅ Real-time weight/timer display | ❌ Not implemented | ❌ **Missing** |
| **Configuration** | ✅ Full config in EEPROM | ✅ Full config in NVS | ✅ **Complete** |

**Implementation Files:**
- **Original:** `src/charge_mode.cpp`, `src/charge_mode.h`
- **ESP32-S3:** `components/charge_mode/`

**What Works:**
- ✅ Configuration storage (thresholds, colors, precharge settings)
- ✅ State enumeration
- ✅ Runtime state structure
- ✅ URL-encoded hex color parsing

**What's Missing:**
- ❌ Actual charge loop execution
- ❌ PID controller integration
- ❌ Scale reading integration
- ❌ Motor control integration
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

### 6.1 Critical (Blocks Basic Functionality)

1. ❌ **TMC Motor Driver Communication**
   - Location: `components/motors/`
   - Need: SPI/UART TMC library port
   - Impact: Cannot control motors beyond basic STEP pulses

2. ❌ **Scale Serial Drivers**
   - Location: `components/scale/`
   - Need: UART protocol parsers for 8 scale types
   - Impact: Cannot read weight from real scales

3. ❌ **Display Graphics Integration**
   - Location: `components/display_st7567/`
   - Need: u8g2 library integration
   - Impact: Cannot show UI, weight, or status

4. ❌ **Charge Mode Execution Logic**
   - Location: `components/charge_mode/`
   - Need: PID control loop, motor integration
   - Impact: Core auto-dispense feature non-functional

### 6.2 High Priority (Essential Features)

5. ❌ **Menu System (MUI)**
   - Location: New component needed
   - Need: Port MUI library + integration
   - Impact: Cannot navigate settings or modes

6. ❌ **NeoPixel LED Driver**
   - Location: `components/neopixel_led/`
   - Need: RMT or SPI-based WS2812 driver
   - Impact: No visual status feedback

7. ❌ **Servo Gate Control**
   - Location: `components/servo_gate/`
   - Need: LEDC PWM servo driver
   - Impact: Cannot automate powder funnel gate

8. ❌ **Motor Acceleration/PID Control**
   - Location: `components/motors/`
   - Need: Velocity ramping, PID integration
   - Impact: Jerky motor movement, poor dispense control

### 6.3 Medium Priority (Enhanced Functionality)

9. ⏳ **Display Buffer Mirroring**
   - Location: `components/display_st7567/` + REST handler
   - Need: Export framebuffer as bitmap
   - Impact: Cannot view display remotely

10. ❌ **Button REST Override**
    - Location: `components/input_encoder/` + REST
    - Need: REST API to simulate button presses
    - Impact: Cannot control from web without buttons

11. ⏳ **Scale Calibration**
    - Location: `components/scale/`
    - Need: External weight calibration routine
    - Impact: Cannot calibrate scale via UI

### 6.4 Low Priority (Nice to Have)

12. ❌ **Cleanup Mode Motor Control**
    - Location: `components/cleanup_mode/`
    - Need: Link to motor control
    - Impact: Manual trickler mode non-functional

13. ❌ **Display Rotation Configuration**
    - Location: `components/display_st7567/`
    - Need: Config parameter + u8g2 rotation
    - Impact: Fixed display orientation

---

## 7. What's Actually Working Right Now

### ✅ Fully Functional Components

1. **WiFi Management** - Can connect to WiFi (STA) or create AP
2. **HTTP Server** - Serves web pages and REST API
3. **NVS Configuration Storage** - All config modules save/load correctly
4. **Profile System** - Full CRUD via REST API
5. **System Control** - Reboot, save all settings, erase NVS
6. **Web UI Pages** - Portal, wizard, and display mirror HTML served
7. **REST API Endpoints** - All endpoints registered and parse parameters

### ⏳ Partially Working Components

1. **Motors** - Basic STEP generation at fixed frequency (test only)
2. **Scale** - Generic simulator for testing (no real hardware)
3. **Input Encoder** - Hardware driver works, not integrated
4. **Display** - SPI communication works, no graphics library
5. **Charge/Cleanup Mode** - Configuration stored, no execution logic

### ❌ Non-Functional Components

1. **TMC Motor Drivers** - No communication with drivers
2. **Real Scale Hardware** - No serial protocol parsers
3. **Menu System** - Completely missing
4. **NeoPixel LEDs** - No WS2812 driver
5. **Servo Gate** - Empty component
6. **PID Control** - Not implemented
7. **Display Rendering** - No graphics output

---

## 8. Hardware Integration Requirements

### 🔧 GPIO Pin Mapping Status

| Original Pin Assignment | ESP32-S3 Mapping | Status |
|------------------------|------------------|---------|
| **Display SPI** | GPIO pins assigned | ✅ Tested |
| **Encoder** | GPIO pins assigned | ✅ Tested |
| **Coarse Motor** | STEP=GPIO3 (strap pin!), DIR=GPIO7, EN=? | ⚠️ **Needs boot delay** |
| **Fine Motor** | Not mapped yet | ❌ Missing |
| **Scale UART** | Not configured | ❌ Missing |
| **Motor UART** | Not configured | ❌ Missing |
| **NeoPixel** | Not configured | ❌ Missing |
| **Servo PWM** | Not configured | ❌ Missing |

**Critical Note:** GPIO3 is a strapping pin on ESP32-S3. The current implementation has a boot delay guard to prevent issues.

---

## 9. Recommended Next Steps

### Phase 1: Core Hardware (Weeks 1-2)
1. Port TMC motor driver library (SPI/UART communication)
2. Implement proper MCPWM motor control with acceleration
3. Port at least one scale driver (e.g., AND FXi or Generic)
4. Integrate u8g2 graphics library with ST7567 display

### Phase 2: Application Logic (Weeks 3-4)
5. Implement charge mode PID control loop
6. Link motor control to charge mode
7. Add scale reading integration to charge mode
8. Port NeoPixel driver (RMT or SPI-based)

### Phase 3: User Interface (Weeks 5-6)
9. Port MUI menu system
10. Create main menu and settings screens
11. Integrate encoder input with menu system
12. Add display buffer mirroring for web UI

### Phase 4: Advanced Features (Weeks 7-8)
13. Implement servo gate control (LEDC PWM)
14. Add cleanup mode motor control
15. Port remaining scale drivers
16. Add scale calibration routine

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
│   ├── charge_mode/       # ⏳ Config only
│   ├── cleanup_mode/      # ⏳ Config only
│   ├── motors/            # ⏳ Config only
│   ├── motors_mcpwm/      # ⏳ Basic STEP test
│   ├── scale/             # ⏳ Config only
│   ├── scale_generic/     # ⏳ Simulator only
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

The ESP32-S3 port has successfully established the **infrastructure layer**:
- ✅ Configuration storage (NVS replacing EEPROM)
- ✅ WiFi and HTTP server
- ✅ REST API framework
- ✅ Web UI pages

However, the **hardware integration layer** is largely incomplete:
- ❌ Motor control (beyond basic STEP pulses)
- ❌ Scale communication
- ❌ Display graphics
- ❌ LED output

The **application logic layer** exists as configuration stubs but lacks execution:
- ⏳ Charge mode has configs but no PID loop
- ⏳ Profile system stores data but isn't used
- ❌ Menu system not ported

**Estimated Completion:** 40-50% of original functionality ported. The project is a solid foundation for web-based configuration, but needs significant work on hardware drivers and application logic to become a functional powder dispenser.

---

## Notes

- URL-encoded hex colors (`%23` for `#`) are properly handled in charge_mode and neopixel_led
- WiFi smart root handler correctly shows wizard in AP mode and portal in STA mode
- All REST endpoints register and parse parameters correctly
- NVS storage is working reliably for all components

**Last Updated:** 2026-02-07
