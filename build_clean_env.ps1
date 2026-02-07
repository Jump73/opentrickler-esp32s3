$ErrorActionPreference = "Stop"

Write-Host "=== Cleaning environment and building ===" -ForegroundColor Green

# Remove all MSYSTEM-related environment variables
$envVarsToRemove = @('MSYSTEM', 'MSYSTEM_CARCH', 'MSYSTEM_CHOST', 'MSYSTEM_PREFIX', 'MINGW_PREFIX', 'MSYS')
foreach ($var in $envVarsToRemove) {
    if (Test-Path "Env:$var") {
        Remove-Item "Env:$var" -ErrorAction SilentlyContinue
        Write-Host "Removed $var" -ForegroundColor Yellow
    }
}

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
