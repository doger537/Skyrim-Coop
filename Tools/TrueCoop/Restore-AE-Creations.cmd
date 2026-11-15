@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0TrueCoop-Recovery\SharedContent.ps1" -GamePath "%~dp0..\.." -Action Restore
set "result=%errorlevel%"
pause
exit /b %result%
