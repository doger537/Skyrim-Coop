@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0QuestAudit\Start-QuestAudit.ps1" -Restore %*
pause
