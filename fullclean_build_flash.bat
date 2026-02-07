@echo off
echo === ESP32-S3 Full Clean, Build and Flash ===

REM Initialize ESP-IDF environment
call C:\Espressif\frameworks\esp-idf\export.bat

REM Navigate to project
cd /d C:\Users\kdzia\ESPRESS\opentrickler-esp32s3

REM Full clean
echo.
echo === Full Clean ===
idf.py fullclean

REM Build
echo.
echo === Build ===
idf.py build

if %ERRORLEVEL% NEQ 0 (
    echo Build FAILED!
    exit /b 1
)

REM Flash and monitor
echo.
echo === Flash and Monitor ===
idf.py -p COM6 flash monitor
