$ErrorActionPreference = "Stop"

Write-Host "=== Quick Build Test ===" -ForegroundColor Green

# Initialize ESP-IDF environment
. C:\Espressif\frameworks\esp-idf\export.ps1

# Change to project directory
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Build only
Write-Host "`nBuilding..." -ForegroundColor Yellow
idf.py build

if ($LASTEXITCODE -eq 0) {
    Write-Host "`nBuild SUCCESS!" -ForegroundColor Green
    Write-Host "Now you can run: .\test_webui.ps1" -ForegroundColor Cyan
} else {
    Write-Host "`nBuild FAILED!" -ForegroundColor Red
}
