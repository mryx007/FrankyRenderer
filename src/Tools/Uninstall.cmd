@echo off
setlocal
rem Unregisters FrankyRenderer and asks whether to delete the settings.
rem "Uninstall.cmd /s" runs without messages and keeps the settings; exit
rem code 0 means removed.
if /i "%~1"=="/s" (set "quiet=1") else (set "quiet=")
cd /d "%~dp0"

fltmc >nul 2>&1 || (call :msg "Please right-click Uninstall.cmd and choose Run as administrator." & exit /b 5)
if not exist "FrankyRenderer.ax" (call :msg "FrankyRenderer.ax is missing from this folder." & exit /b 2)
for %%p in (PotPlayerMini64 PotPlayer64 mpc-be64 mpc-hc64) do (
    tasklist /nh /fi "imagename eq %%p.exe" | find /i "%%p" >nul && (call :msg "Please close %%p first." & exit /b 3)
)

"%SystemRoot%\System32\regsvr32.exe" /s /u "%~dp0FrankyRenderer.ax" || (call :msg "Removing the registration failed." & exit /b 1)

rem Older versions had a separate Dolby Vision filter.
for %%h in (HKLM HKCU) do (
    reg delete "%%h\SOFTWARE\Classes\CLSID\{40F090C8-E82B-4CE9-931B-A34760E6D49E}" /f >nul 2>&1
    reg delete "%%h\SOFTWARE\Classes\CLSID\{083863F1-70DE-11D0-BD40-00A0C911CE86}\Instance\{40F090C8-E82B-4CE9-931B-A34760E6D49E}" /f >nul 2>&1
)

rem Files of the earlier PowerShell installer.
if exist "%~dp0registration-backup" rd /s /q "%~dp0registration-backup"
if exist "%~dp0setup.ps1" del /q "%~dp0setup.ps1"

rem Settings stay unless the user chooses to delete them ("/s" keeps them).
set "settings=Your settings were kept."
if not defined quiet (
    choice /c YN /n /m "Also delete all FrankyRenderer settings? [Y/N] "
    if not errorlevel 2 (
        if exist "%APPDATA%\FrankyRenderer" rd /s /q "%APPDATA%\FrankyRenderer"
        if exist "%LOCALAPPDATA%\FrankyRenderer" rd /s /q "%LOCALAPPDATA%\FrankyRenderer"
        if exist "%LOCALAPPDATA%\PlaceboRenderer" rd /s /q "%LOCALAPPDATA%\PlaceboRenderer"
        reg delete "HKCU\Software\PlaceboRenderer" /f >nul 2>&1
        reg delete "HKCU\Software\PlaceboRenderer.Backup" /f >nul 2>&1
        set "settings=Your settings were deleted."
    )
    echo.
)
call :msg "FrankyRenderer removed. %settings%"
exit /b 0

:msg
if not defined quiet (echo %~1& echo.& pause)
exit /b
