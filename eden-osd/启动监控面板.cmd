@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0eden-osd.ps1" (
    echo [Eden OSD] 缺少 eden-osd.ps1
    pause
    exit /b 1
)
start "" powershell.exe -NoProfile -Sta -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0eden-osd.ps1"
exit /b 0
