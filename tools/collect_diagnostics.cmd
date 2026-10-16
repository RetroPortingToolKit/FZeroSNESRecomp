@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect diagnostics.ps1"
if errorlevel 1 pause
