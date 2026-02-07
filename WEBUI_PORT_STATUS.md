# OpenTrickler ESP32-S3 Web UI Port - Status

## ✅ Co zostało zrobione / What was done

### 1. Skopiowane z oryginalnego projektu
- **HTML files**: `web_portal.html`, `wizard.html`, `display_mirror.html` + CSS/JS
- **html2header.py**: Skrypt do konwersji HTML → C headers
- Wszystkie pliki HTML skonwertowane do C headers w `main/generated/`

### 2. Nowe komponenty ESP32-S3

#### `components/wifi_manager/` - WiFi dla ESP32-S3
- **Zastępuje**: Pico W CYW43 WiFi driver
- **Funkcje**:
  - `wifi_manager_init()` - Inicjalizacja WiFi
  - `wifi_manager_start_ap()` - Tryb Access Point (jak w oryginale)
  - `wifi_manager_start_sta()` - Tryb Station (klient WiFi)
  - `wifi_manager_get_ip()` - Pobierz IP address
  - `wifi_manager_is_connected()` - Status połączenia

#### `components/http_server/` - HTTP Server dla ESP32-S3
- **Zastępuje**: Pico SDK lwIP HTTP server
- **Używa**: ESP-IDF HTTP server (esp_http_server)
- **Funkcje**:
  - `http_server_init()` - Start serwera HTTP
  - `http_server_register_page_handler()` - Rejestruje stronę HTML
  - `http_server_register_rest_handler()` - Rejestruje endpoint REST API
- **Kompatybilność**: API podobne do oryginalnego `http_rest.c`

### 3. Test application: `main/web_test.c`
- Startuje WiFi AP: **SSID: "OpenTrickler-ESP32"**, **hasło: "opentrickler"**
- Uruchamia HTTP server na **192.168.4.1**
- Serwuje strony:
  - `/` → web_portal.html (główny portal)
  - `/wizard` → wizard.html (kreator konfiguracji)
  - `/mobile` → web_portal.html (widok mobilny)
  - `/display_mirror` → display_mirror.html
- Przykładowy REST endpoint: `/rest/test`

## 🔧 Jak przetestować / How to test

### Build i flash:
```powershell
.\test_webui.ps1
```

### Test procedure:
1. **ESP32 startuje i tworzy WiFi AP**:
   - SSID: `OpenTrickler-ESP32`
   - Password: `opentrickler`
   - IP: `192.168.4.1`

2. **Podłącz się do WiFi** z telefonu lub komputera

3. **Otwórz przeglądarkę** i wejdź na:
   - `http://192.168.4.1/` - główny portal
   - `http://192.168.4.1/wizard` - kreator
   - `http://192.168.4.1/rest/test` - test REST API

4. **Powinieneś zobaczyć** oryginalny web UI OpenTrickler!

## 📋 Co dalej / Next steps

### Do zaimplementowania (z oryginalnego kodu):
- [ ] **REST Endpoints** - przenieść wszystkie endpointy z `rest_endpoints.c`:
  - `/rest/scale_action`
  - `/rest/scale_config`
  - `/rest/charge_mode_config`
  - `/rest/charge_mode_state`
  - `/rest/motor_config`
  - itd... (wszystkie ~15 endpointów)

- [ ] **Backend logic** - przenieść moduły:
  - `motors.c` - sterowanie motorami (już mamy `motors_mcpwm`)
  - `scale.c` - obsługa wagi przez UART (już mamy `scale_generic`)
  - `charge_mode.c` - tryb dozowania
  - `profile.c` - profile ustawień
  - `eeprom.c` - zapis konfiguracji
  - `neopixel_led.c` - kontrola RGB LED
  - `servo_gate.c` - servo motor

- [ ] **Display integration** - opcjonalnie:
  - Można pominąć LCD (skoro web UI działa)
  - Lub naprawić mapowanie GPIO dla LCD później

## 🏗️ Architektura / Architecture

```
Original Pico W          →    ESP32-S3 Port
==========================================
Pico SDK                 →    ESP-IDF
CYW43 WiFi driver        →    ESP32 WiFi (components/wifi_manager)
Pico lwIP HTTP           →    ESP HTTP Server (components/http_server)
FreeRTOS (Pico SDK)      →    FreeRTOS (ESP-IDF)
U8G2 LCD (ST7567)        →    U8G2 LCD (components/display_st7567)
RP2040 GPIO              →    ESP32-S3 GPIO
RP2040 PWM               →    ESP32 MCPWM (components/motors_mcpwm)
RP2040 UART              →    ESP32 UART (components/scale_generic)
```

## 📁 Struktura plików / File structure

```
opentrickler-esp32s3/
├── components/
│   ├── wifi_manager/          ← NEW: WiFi dla ESP32-S3
│   ├── http_server/           ← NEW: HTTP server wrapper
│   ├── motors_mcpwm/          ← Existing: Motor control
│   ├── scale_generic/         ← Existing: Scale UART
│   ├── display_st7567/        ← Existing: LCD driver
│   └── ...
├── html/                      ← NEW: Oryginalne pliki HTML
│   ├── web_portal.html
│   ├── wizard.html
│   └── display_mirror.html
├── scripts/
│   └── html2header.py         ← NEW: Konwerter HTML→C
├── main/
│   ├── generated/             ← NEW: Wygenerowane C headers
│   │   ├── web_portal.html.h
│   │   ├── wizard.html.h
│   │   └── display_mirror.html.h
│   ├── web_test.c             ← NEW: Test application
│   └── CMakeLists.txt
└── test_webui.ps1             ← NEW: Build & flash script
```

## ⚠️ Uwagi / Notes

1. **LCD pominięty**: Na razie skupiamy się na Web UI, LCD można dodać później
2. **REST endpoints**: Działają, ale trzeba przenieść całą logikę backendu
3. **HTML files**: Identyczne jak w oryginale, więc UI będzie wyglądał tak samo
4. **WiFi AP mode**: Domyślnie ESP32 działa jako Access Point (jak w oryginale)
5. **IP Address**: Domyślnie `192.168.4.1` (standardowe dla ESP32 AP mode)

## 🎯 Priorytet

**TERAZ**: Przetestuj podstawowy Web UI - czy działa!
```powershell
.\test_webui.ps1
```

**POTEM**: Dodamy resztę REST endpoints i backend logic z oryginalnego kodu.
