@echo off
setlocal
rem Registers FrankyRenderer. "Install.cmd /s" runs without messages, for
rem player packages; exit code 0 means installed.
if /i "%~1"=="/s" (set "quiet=1") else (set "quiet=")
cd /d "%~dp0"

fltmc >nul 2>&1 || (call :msg "Please right-click Install.cmd and choose Run as administrator." & exit /b 5)
if not exist "FrankyRenderer.ax" (call :msg "FrankyRenderer.ax is missing. Extract the whole ZIP first." & exit /b 2)
for %%p in (PotPlayerMini64 PotPlayer64 mpc-be64 mpc-hc64) do (
    tasklist /nh /fi "imagename eq %%p.exe" | find /i "%%p" >nul && (call :msg "Please close %%p first." & exit /b 3)
)

rem Older versions had a separate Dolby Vision filter.
for %%h in (HKLM HKCU) do (
    reg delete "%%h\SOFTWARE\Classes\CLSID\{40F090C8-E82B-4CE9-931B-A34760E6D49E}" /f >nul 2>&1
    reg delete "%%h\SOFTWARE\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{40F090C8-E82B-4CE9-931B-A34760E6D49E}" /f >nul 2>&1
)

"%SystemRoot%\System32\regsvr32.exe" /s "%~dp0FrankyRenderer.ax" || (call :msg "Registration failed. Are all files extracted? Is a virus scanner blocking them?" & exit /b 1)

rem Files of the earlier PowerShell installer.
if exist "%~dp0registration-backup" rd /s /q "%~dp0registration-backup"
if exist "%~dp0setup.ps1" del /q "%~dp0setup.ps1"
call :msg "FrankyRenderer installed. Select FrankyRenderer as the video renderer."
exit /b 0

:msg
if not defined quiet (echo %~1& echo.& pause)
exit /b
