@echo off
setlocal
:: ============================================================
::  Vocal Doubler — One-Click Setup
::  Double-click this file. It will ask for admin rights,
::  install anything missing, then build the VST3 plugin.
:: ============================================================

:: Check if already running as Administrator
net session >nul 2>&1
if %errorlevel% equ 0 goto :run

:: Not admin — relaunch elevated, directly into PowerShell so
:: ExecutionPolicy Bypass is honoured even when LocalMachine=AllSigned
echo Requesting administrator privileges...
powershell -Command ^
  "Start-Process powershell ^
    -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File \'%~dp0setup.ps1\'' ^
    -Verb RunAs"
exit /b

:run
:: Already admin — run directly
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1"
if %errorlevel% neq 0 (
    echo.
    echo Setup failed. See the messages above.
    pause
)
