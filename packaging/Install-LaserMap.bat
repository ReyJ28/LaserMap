@echo off
REM Double-click to install LaserMap (runs the PowerShell installer without changing any system policy).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-LaserMap.ps1"
pause
