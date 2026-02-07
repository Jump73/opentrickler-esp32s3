# OpenTrickler ESP32-S3 Web UI - Debug Guide

## Problem: Nie widzę sieci WiFi "OpenTrickler-ESP32"

### Krok 1: Build i Flash
```powershell
.\test_webui.ps1
```

### Krok 2: Sprawdź logi w monitorze

W monitorze powinieneś zobaczyć:
```
========================================
=== OpenTrickler ESP32-S3 Web UI Test ===
========================================

Step 1: Initializing WiFi...
I (xxx) WiFi_Manager: Initializing WiFi manager...
I (xxx) WiFi_Manager: NVS initialized
I (xxx) WiFi_Manager: TCP/IP stack initialized
I (xxx) WiFi_Manager: Event loop created
I (xxx) WiFi_Manager: WiFi driver initialized
I (xxx) WiFi_Manager: WiFi manager initialized successfully

Step 2: Starting WiFi AP...
I (xxx) WiFi_Manager: Starting WiFi AP mode...
I (xxx) WiFi_Manager:   SSID: OpenTrickler-ESP32
I (xxx) WiFi_Manager:   Password: opentrickler
I (xxx) WiFi_Manager: AP netif created
I (xxx) WiFi_Manager: Event handlers registered
I (xxx) WiFi_Manager: Setting WiFi mode to AP...
I (xxx) WiFi_Manager: Configuring WiFi AP...
I (xxx) WiFi_Manager: Starting WiFi...
I (xxx) WiFi_Manager: ========================================
I (xxx) WiFi_Manager: WiFi AP started successfully!
I (xxx) WiFi_Manager:   SSID: OpenTrickler-ESP32
I (xxx) WiFi_Manager:   Password: opentrickler
I (xxx) WiFi_Manager:   IP Address: 192.168.4.1
I (xxx) WiFi_Manager: ========================================

Step 4: Starting HTTP server...
I (xxx) HTTP_Server: HTTP server started on port 80

Step 5: Registering web pages...
I (xxx) HTTP_Server: Registered page handler: /
I (xxx) HTTP_Server: Registered page handler: /wizard
I (xxx) HTTP_Server: Registered page handler: /mobile
I (xxx) HTTP_Server: Registered page handler: /display_mirror

Step 6: Registering REST endpoints...
I (xxx) HTTP_Server: Registered REST handler: /rest/test

============================================
Web UI ready!
...
============================================
```

### Krok 3: Diagnoza problemów

#### A) Jeśli ESP32 się restartuje (bootloop):
```
E (xxx) task_wdt: Task watchdog got triggered
```
**Rozwiązanie**: Problem z pamięcią lub inicjalizacją. Sprawdź logi dla błędów.

#### B) Jeśli WiFi init fails:
```
E (xxx) WiFi_Manager: WiFi init failed: ESP_ERR_...
```
**Możliwe przyczyny**:
- Brak partycji NVS
- Problem z konfiguracją sdkconfig
- Za mała pamięć flash

**Rozwiązanie**: Uruchom `idf.py menuconfig` i sprawdź:
- Component config → WiFi → Enable
- Partition Table → partycja NVS musi istnieć

#### C) Jeśli WiFi się inicjuje ale nie ma sieci:
```
I (xxx) WiFi_Manager: WiFi manager initialized successfully
I (xxx) WiFi_Manager: Starting WiFi AP mode...
[brak dalszych komunikatów]
```
**Rozwiązanie**: Problem z WiFi driver. Może być konflikt z innymi komponentami.

#### D) Jeśli wszystko OK w logach ale nadal nie widać sieci:

1. **Sprawdź na telefonie/komputerze**:
   - Odśwież listę sieci WiFi
   - Sprawdź czy WiFi 2.4GHz jest włączony (ESP32 nie obsługuje 5GHz)
   - Sprawdź czy nie masz ukrytych sieci
   - Spróbuj z innym urządzeniem

2. **Zrestartuj ESP32**:
   - Naciśnij przycisk RST/EN
   - Lub: wyjmij i włóż USB

3. **Sprawdź kanał WiFi**:
   - ESP32 domyślnie używa kanału 1
   - Może być konflikt z innymi sieciami
   - Można zmienić w `wifi_config.ap.channel`

### Krok 4: Testuj krok po kroku

Jeśli nadal nie działa, przetestuj tylko WiFi bez HTTP:

1. Wyłącz HTTP server w `web_test.c`:
```c
// ESP_ERROR_CHECK(http_server_init());  // zakomentuj
```

2. Przebuduj i wgraj
3. Sprawdź czy teraz widzisz sieć WiFi
4. Jeśli tak → problem w HTTP server
5. Jeśli nie → problem w WiFi

### Krok 5: Minimalny test WiFi

Stwórz minimalny test - tylko WiFi AP bez niczego innego:

```c
void app_main(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "TEST-ESP32",
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    printf("WiFi AP started: TEST-ESP32 / 12345678\n");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

Jeśli to działa → problem w komponencie wifi_manager
Jeśli to nie działa → problem z ESP-IDF lub hardware

## Kolejne kroki po naprawieniu WiFi

1. ✅ WiFi AP działa - widzisz sieć
2. ⬜ Połącz się z siecią WiFi
3. ⬜ Sprawdź IP: http://192.168.4.1/
4. ⬜ Powinieneś zobaczyć Web UI

## Przydatne komendy

```powershell
# Rebuild from scratch
idf.py fullclean
idf.py build

# Flash and monitor in one command
idf.py -p COM6 flash monitor

# Just monitor (no flash)
idf.py -p COM6 monitor

# Exit monitor: Ctrl+]

# Check WiFi configuration
idf.py menuconfig
# → Component config → Wi-Fi
```

## Sprawdź też

- Antena WiFi na ESP32-S3-Pico - czy jest podłączona?
- Zasilanie - czy ESP32 ma wystarczające zasilanie (WiFi pobiera dużo prądu)?
- USB kabel - czy jest dobry (niektóre kable są tylko do ładowania)?
