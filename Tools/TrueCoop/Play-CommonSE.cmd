@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0SharedContent.ps1" -Action Enable -Launch
if errorlevel 1 pause
