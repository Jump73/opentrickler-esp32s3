Set WshShell = CreateObject("WScript.Shell")
WshShell.Run "powershell.exe -ExecutionPolicy Bypass -File ""C:\Users\kdzia\ESPRESS\opentrickler-esp32s3\build_clean_env.ps1""", 1, True
