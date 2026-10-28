@echo off
cd /d "%~dp0"
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set T=%%i
if not exist logs mkdir logs
set LOG=logs\palworld-%T%.log
echo Logging to %LOG%
set APS5_LOCAL_FAST_PKF16=0
set APS5_LOCAL_FAULT_DEBUGGER=1
app.exe > "%LOG%" 2>&1
echo.
echo Game exited with code %errorlevel%. Log: %LOG%
pause
