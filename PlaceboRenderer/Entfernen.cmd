@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -Action Remove
set "taskResult=%errorlevel%"
if not "%taskResult%"=="0" pause
exit /b %taskResult%
