$ErrorActionPreference = "Stop"

# Ustaw IDF_PATH
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-5.2.3"

# Przejdź do katalogu projektu
Set-Location "C:\Users\kdzia\ESPRESS\opentrickler-esp32s3"

# Aktywuj środowisko ESP-IDF
& "C:\Espressif\frameworks\esp-idf-5.2.3\export.ps1"

# Buduj projekt
python "$env:IDF_PATH\tools\idf.py" build

Write-Host "Build completed!"
