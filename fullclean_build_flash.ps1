$ErrorActionPreference = "Stop"

Write-Host "=== ESP32-S3 Full Clean, Build & Flash ===" -ForegroundColor Green

# Initialize ESP-IDF environment
Write-Host "`nInitializing ESP-IDF environment..." -ForegroundColor Yellow
. C:\Espressif\frameworks\esp-idf\export.ps1

# Change to project directory
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Full clean
Write-Host "`nRunning fullclean..." -ForegroundColor Yellow
idf.py fullclean

# Build
Write-Host "`nBuilding..." -ForegroundColor Yellow
idf.py build

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build FAILED!" -ForegroundColor Red
    exit 1
}

Write-Host "`nBuild OK!" -ForegroundColor Green

# Flash and monitor
Write-Host "`nFlashing to COM6..." -ForegroundColor Yellow
idf.py -p COM6 flash monitor
