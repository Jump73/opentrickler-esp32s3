$ErrorActionPreference = "Stop"

Write-Host "=== OpenTrickler ESP32-S3 Build & Flash ===" -ForegroundColor Green

# Przejdź do katalogu projektu
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Build
Write-Host "`nBuilding..." -ForegroundColor Yellow
idf.py build

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build FAILED!" -ForegroundColor Red
    exit 1
}

Write-Host "`nBuild OK!" -ForegroundColor Green

# Flash i Monitor
Write-Host "`nFlashing to COM6..." -ForegroundColor Yellow
idf.py -p COM6 flash monitor
