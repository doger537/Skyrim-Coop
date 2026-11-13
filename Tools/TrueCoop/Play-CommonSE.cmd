@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0SharedContent.ps1" -GamePath "%~dp0..\.." -Action Enable -Launch
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%
