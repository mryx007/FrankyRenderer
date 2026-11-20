@echo off
setlocal
rem Saves the renderer's settings (%APPDATA%\FrankyRenderer\Settings.ini) as
rem Settings.ini in this folder, where Import Settings.cmd finds them.
rem "Backup Settings.cmd /s" runs without questions or messages; exit code 0
rem means saved.
if /i "%~1"=="/s" (set "quiet=1") else (set "quiet=")
set "source=%APPDATA%\FrankyRenderer\Settings.ini"
set "target=%~dp0Settings.ini"

if not exist "%source%" (call :msg "There are no saved settings yet. Change any setting in the menu first." & exit /b 2)
if exist "%target%" if not defined quiet (
    choice /c YN /n /m "Settings.ini already exists in this folder. Overwrite it? [Y/N] "
    if errorlevel 2 goto cancel
    echo.
)
copy /y "%source%" "%target%" >nul 2>&1 || (call :msg "Cannot write to this folder. Please right-click Backup Settings.cmd and choose Run as administrator." & exit /b 5)
call :msg "Settings saved as Settings.ini in this folder."
exit /b 0

:cancel
exit /b 6

:msg
if not defined quiet (echo %~1& echo.& pause)
exit /b
