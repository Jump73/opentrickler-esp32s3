$ErrorActionPreference = "Stop"

Write-Host "=== Build and Flash GPIO Test ===" -ForegroundColor Green

# Initialize ESP-IDF environment
. C:\Espressif\frameworks\esp-idf\export.ps1

# Change to project directory
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Build
Write-Host "`nBuilding project..." -ForegroundColor Yellow
idf.py build

# Flash
Write-Host "`nFlashing to COM6..." -ForegroundColor Yellow
idf.py -p COM6 flash

Write-Host "`nStarting monitor..." -ForegroundColor Yellow
idf.py -p COM6 monitor
