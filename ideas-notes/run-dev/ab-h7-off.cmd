@echo off
cd /d "%~dp0"
for /f %%i in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set T=%%i
if not exist logs mkdir logs
copy /y ab-h7\libSceAgcDriver-H7.prx libs\libSceAgcDriver.prx >nul || (echo Could not install the H7 driver. & pause & exit /b 1)
copy /y ab-h7\libc-H7.prx libs\libc.prx >nul || (echo Could not install the H7 libc. & pause & exit /b 1)
set LOG=logs\ab-h7off-%T%.log
echo Logging to %LOG%
set APS5_FAULT_DEBUGGER=1
set APS5_PROFILE_DRAW=1
set APS5_LOCAL_DRAW_MIRROR=1
set APS5_NO_CLEAN_SET=1
set APS5_SAMPLE_WORKER=logs\worker-samples-h7off-%T%
set APS5_SAMPLE_WORKER_CPU_ONLY=1
app.exe > "%LOG%" 2>&1
echo.
echo Game exited with code %errorlevel%. Log: %LOG%
pause
