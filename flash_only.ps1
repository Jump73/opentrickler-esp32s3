$ErrorActionPreference = "Stop"

Write-Host "=== Flash ESP32-S3 (bez monitora) ===" -ForegroundColor Green

# Initialize ESP-IDF environment
. C:\Espressif\frameworks\esp-idf\export.ps1

# Change to project directory
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Flash only (no monitor)
Write-Host "`nFlashing to COM6..." -ForegroundColor Yellow
idf.py -p COM6 flash

Write-Host "`nFlash complete! Now run monitor separately if needed." -ForegroundColor Green
