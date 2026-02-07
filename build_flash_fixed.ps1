$ErrorActionPreference = "Stop"

Write-Host "=== Build and Flash LCD FIXED (GPIO42 for SCK) ===" -ForegroundColor Green

# Initialize ESP-IDF environment
. C:\Espressif\frameworks\esp-idf\export.ps1

# Change to project directory
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Build
Write-Host "`nBuilding with CORRECTED pins..." -ForegroundColor Yellow
Write-Host "  LCD_SCK  = GPIO42 (was GPIO1)" -ForegroundColor Cyan
Write-Host "  LCD_MOSI = GPIO2" -ForegroundColor Cyan
Write-Host "  LCD_CS   = GPIO41" -ForegroundColor Cyan
Write-Host "  LCD_A0   = GPIO4" -ForegroundColor Cyan
Write-Host "  LCD_RST  = GPIO38`n" -ForegroundColor Cyan

idf.py build

# Flash and monitor
Write-Host "`nFlashing to COM6 and starting monitor..." -ForegroundColor Yellow
idf.py -p COM6 flash monitor
