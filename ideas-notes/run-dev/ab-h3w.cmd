@echo off
cd /d "%~dp0"
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set T=%%i
if not exist logs mkdir logs
copy /y ab-h3w\libSceAgcDriver-H3w.prx libs\libSceAgcDriver.prx >nul || (echo Could not install the H3w driver. & pause & exit /b 1)
set LOG=logs\ab-h3w-%T%.log
echo Logging to %LOG%
set APS5_FAULT_DEBUGGER=1
set APS5_PROFILE_DRAW=1
set APS5_SAMPLE_WORKER=logs\worker-samples-h3w-%T%.txt
app.exe > "%LOG%" 2>&1
echo.
echo Game exited with code %errorlevel%. Log: %LOG%
pause
