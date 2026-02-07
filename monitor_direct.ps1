$ErrorActionPreference = "Stop"

Write-Host "Uruchamianie monitora na COM6 (115200 baud)..."
Write-Host "Aby wyjść: Ctrl+C`n"

# Użyj bezpośrednio Python z pyserial
& "C:\Espressif\python_env\idf5.2_py3.11_env\Scripts\python.exe" -m serial.tools.miniterm COM6 115200 --eol LF --raw
