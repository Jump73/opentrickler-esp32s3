$ErrorActionPreference = "Stop"

Write-Host "=== OpenTrickler ESP32-S3 Web UI Test ===" -ForegroundColor Green
Write-Host ""

# Initialize ESP-IDF environment
Write-Host "Initializing ESP-IDF environment..." -ForegroundColor Yellow
. C:\Espressif\frameworks\esp-idf\export.ps1

# Change to project directory
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Build (no clean - for faster builds)
Write-Host "`nBuilding Web UI test..." -ForegroundColor Yellow
idf.py build

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nBuild FAILED! Check errors above." -ForegroundColor Red
    exit 1
}

# Flash using esptool directly
Write-Host "`nFlashing to COM6..." -ForegroundColor Yellow
python -m esptool --chip esp32s3 --port COM6 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0x10000 build\opentrickler_esp32s3.bin

if ($LASTEXITCODE -ne 0) {
    Write-Host "`nFlash FAILED! Check if ESP32 is connected to COM6." -ForegroundColor Red
    exit 1
}

Write-Host "`n=== TEST INSTRUCTIONS ===" -ForegroundColor Yellow
Write-Host ""
Write-Host "After flashing, the ESP32 will create a WiFi Access Point:" -ForegroundColor Cyan
Write-Host "  SSID: OpenTrickler-ESP32" -ForegroundColor White
Write-Host "  Password: opentrickler" -ForegroundColor White
Write-Host ""
Write-Host "To test:" -ForegroundColor Cyan
Write-Host "  1. Connect your phone/computer to the WiFi network" -ForegroundColor White
Write-Host "  2. Open browser and go to: http://192.168.4.1/" -ForegroundColor White
Write-Host "  3. You should see the OpenTrickler web portal!" -ForegroundColor White
Write-Host ""
Write-Host "Available pages:" -ForegroundColor Cyan
Write-Host "  http://192.168.4.1/         - Main web portal" -ForegroundColor White
Write-Host "  http://192.168.4.1/wizard   - Setup wizard" -ForegroundColor White
Write-Host "  http://192.168.4.1/mobile   - Mobile view" -ForegroundColor White
Write-Host "  http://192.168.4.1/display_mirror - Display mirror" -ForegroundColor White
Write-Host ""
Write-Host "Starting monitor..." -ForegroundColor Yellow
Write-Host ""

idf.py -p COM6 monitor
