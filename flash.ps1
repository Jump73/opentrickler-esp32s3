$ErrorActionPreference = "Stop"

# Ustaw IDF_PATH
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-5.2.3"

# Przejdź do katalogu projektu
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Aktywuj środowisko ESP-IDF
& "C:\Espressif\frameworks\esp-idf-5.2.3\export.ps1"

# Znajdź porty COM
Write-Host "`nDostępne porty COM:"
[System.IO.Ports.SerialPort]::getportnames() | ForEach-Object { Write-Host "  $_" }

# Zapytaj użytkownika o port
$port = Read-Host "`nPodaj port COM (np. COM3)"

# Flash i monitor
Write-Host "`nFlashowanie ESP32-S3 na port $port..."
python "$env:IDF_PATH\tools\idf.py" -p $port flash monitor
