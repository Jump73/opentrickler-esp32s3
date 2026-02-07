# OpenTrickler ESP32-S3 - WiFi Setup Guide

## Pierwsze uruchomienie / First Boot

### Krok 1: ESP32 uruchamia się w trybie AP
Po pierwszym uruchomieniu (lub resecie do ustawień fabrycznych), ESP32 automatycznie tworzy własną sieć WiFi:

- **SSID**: `OpenTrickler-ESP32`
- **Hasło**: `opentrickler`
- **IP ESP32**: `192.168.4.1`

### Krok 2: Połącz się z siecią WiFi
1. Na telefonie lub komputerze wyszukaj sieć WiFi
2. Połącz się z siecią `OpenTrickler-ESP32`
3. Hasło: `opentrickler`
4. Po połączeniu otrzymasz IP (np. `192.168.4.2`)

### Krok 3: Otwórz Setup Wizard
1. Otwórz przeglądarkę
2. Wejdź na: **http://192.168.4.1/**
3. Zobaczysz Setup Wizard

### Krok 4: Skonfiguruj WiFi
W wizardzie wprowadź dane swojej lokalnej sieci WiFi:

- **SSID**: Nazwa twojej sieci WiFi (np. "MyHomeWiFi")
- **Password**: Hasło do twojej sieci
- **Authentication**: Zazwyczaj `WPA2-MIXED` (opcja 3)
- **Enable WiFi**: `YES`

### Krok 5: Zapisz i zrestartuj
1. Kliknij **"Complete & Reboot"**
2. Wizard wyśle konfigurację do ESP32:
   ```
   /rest/wireless_config?w0=MyHomeWiFi&w1=MyPassword&w2=3&w4=true&ee=true
   /rest/system_control?s5=true
   ```
3. ESP32 zapisze konfigurację do NVS (flash) i zrestartuje się

---

## Po restarcie / After Reboot

### ESP32 łączy się z twoją siecią
Po restarcie ESP32:
1. Wczytuje zapisaną konfigurację z NVS
2. Próbuje połączyć się z twoją siecią WiFi (tryb STA)
3. Jeśli się uda:
   - Otrzymuje IP od routera (np. `192.168.1.150`)
   - **Ma dostęp do internetu** → CDN działa!
   - Web Portal jest dostępny ze wszystkich urządzeń w sieci
4. Jeśli się nie uda (błędne hasło, sieć niedostępna):
   - Wraca do trybu AP (`OpenTrickler-ESP32`)
   - Możesz ponownie skonfigurować WiFi przez wizard

---

## Adresy Web UI

### Tryb AP (bez internetu)
```
http://192.168.4.1/          → Setup Wizard (działa offline)
http://192.168.4.1/wizard    → Setup Wizard (działa offline)
```

### Tryb STA (z internetem)
```
http://[IP-FROM-ROUTER]/         → Setup Wizard (lub Web Portal w przyszłości)
http://[IP-FROM-ROUTER]/portal   → Full Web Portal (wymaga internetu dla CSS)
http://[IP-FROM-ROUTER]/wizard   → Setup Wizard
```

---

## REST API Endpoints

### `/rest/wireless_config`
**GET** - Pobierz aktualną konfigurację WiFi
```
GET /rest/wireless_config
Response: {"w0":"MySSID","w2":3,"w3":10000,"w4":true}
```

**POST** - Zapisz nową konfigurację WiFi
```
POST /rest/wireless_config?w0=MySSID&w1=MyPassword&w2=3&w4=true&ee=true

Parametry:
- w0 (string): SSID sieci WiFi
- w1 (string): Hasło WiFi
- w2 (int): Typ autoryzacji (0=OPEN, 1=WPA, 2=WPA2, 3=WPA_WPA2)
- w3 (int): Timeout połączenia w ms (domyślnie 10000)
- w4 (bool): Włącz WiFi STA (true/false)
- ee (bool): Zapisz do NVS (true/false)

Response: {"w0":"MySSID","w2":3,"w3":10000,"w4":true,"saved":true}
```

### `/rest/system_control`
**POST** - Restart ESP32
```
POST /rest/system_control?s5=true

Parametry:
- s5 (bool): Software reset (true = restart)

Response: {"s5":true,"message":"Rebooting..."}
```

---

## Architektura

### Komponenty:

#### 1. `wifi_manager`
- Zarządza WiFi (AP i STA mode)
- Obsługuje NVS (zapis/odczyt konfiguracji)
- Funkcja `wifi_manager_auto_start()` - automatyczne uruchamianie

#### 2. `rest_handlers`
- REST endpoints dla konfiguracji WiFi
- REST endpoints dla kontroli systemu
- Compatible z oryginalnym OpenTrickler API

#### 3. `http_server`
- HTTP server wrapper (ESP-IDF httpd)
- Rejestracja stron HTML i REST endpoints

---

## Workflow Code

### Startup (`web_test.c`):
```c
void app_main(void)
{
    // 1. Initialize WiFi manager
    wifi_manager_init();

    // 2. Auto-start WiFi (STA or AP)
    wifi_manager_auto_start("OpenTrickler-ESP32", "opentrickler");
    // - Próbuje STA z zapisanej konfiguracji
    // - Jeśli fail → AP mode

    // 3. Start HTTP server
    http_server_init();

    // 4. Register pages
    http_server_register_page_handler("/", html_wizard_html);  // AP mode
    http_server_register_page_handler("/portal", html_web_portal_html);  // STA mode

    // 5. Register REST endpoints
    http_server_register_rest_handler("/rest/wireless_config", ...);
    http_server_register_rest_handler("/rest/system_control", ...);
}
```

### WiFi Configuration Save:
```c
// In wizard, user clicks "Complete & Reboot"
// JavaScript sends:
fetch('/rest/wireless_config?w0=MySSID&w1=Pass&w2=3&w4=true&ee=true')
  .then(() => fetch('/rest/system_control?s5=true'))
```

### After Reboot:
```c
wifi_manager_auto_start("OpenTrickler-ESP32", "opentrickler");
  ↓
wifi_manager_load_config(&config);  // Load from NVS
  ↓
if (config.enable && config.ssid[0] != '\0') {
    wifi_manager_start_sta(config.ssid, config.password, config.timeout_ms);
    // → Connects to user's WiFi
} else {
    wifi_manager_start_ap("OpenTrickler-ESP32", "opentrickler");
    // → Creates AP
}
```

---

## Troubleshooting

### Problem: Nie widzę sieci "OpenTrickler-ESP32"
- Sprawdź czy WiFi 2.4GHz jest włączony (ESP32 nie obsługuje 5GHz)
- Zrestartuj ESP32 (przycisk RST)
- Sprawdź logi w monitorze

### Problem: Nie mogę połączyć się z moją siecią WiFi
- Sprawdź czy hasło jest poprawne
- Sprawdź czy sieć jest 2.4GHz
- Sprawdź typ autoryzacji (zazwyczaj WPA2-MIXED)
- ESP32 wróci do AP mode jeśli nie może się połączyć

### Problem: Web Portal nie ładuje CSS
- W trybie AP (bez internetu) używaj `/wizard` (inline CSS)
- W trybie STA (z internetem) możesz używać `/portal` (CDN)

---

## Następne kroki

Po skonfigurowaniu WiFi i połączeniu z siecią lokalną:
- [ ] Dodać automatyczne przełączanie `/` z wizard na portal w trybie STA
- [ ] Zaimplementować resztę REST endpoints z oryginalnego OpenTrickler
- [ ] Dodać backend logic (motors, scale, charge_mode, etc.)
