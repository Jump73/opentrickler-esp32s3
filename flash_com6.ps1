$ErrorActionPreference = "Stop"

# Ustaw IDF_PATH
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-5.2.3"

# Przejdź do katalogu projektu
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Aktywuj środowisko ESP-IDF
& "C:\Espressif\frameworks\esp-idf-5.2.3\export.ps1"

# Flash i monitor na COM6
Write-Host "`nFlashowanie ESP32-S3 na COM6..."
python "$env:IDF_PATH\tools\idf.py" -p COM6 flash monitor
