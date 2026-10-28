@echo off
cd /d "%~dp0"
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set T=%%i
if not exist logs mkdir logs
set LOG=logs\palworld-profile-%T%.log
echo Logging to %LOG%
set APS5_PROFILE_DRAW=1
set APS5_PROFILE_GPU=1
set APS5_SAMPLE_THREADS=logs\samples-%T%.txt
app.exe > "%LOG%" 2>&1
echo.
echo Game exited with code %errorlevel%. Log: %LOG%
pause
