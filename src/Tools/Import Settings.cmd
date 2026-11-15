@echo off
setlocal
rem Makes Settings.ini from this folder the renderer's settings
rem (%APPDATA%\FrankyRenderer\Settings.ini). "Import Settings.cmd /s" runs
rem without questions or messages, for player packages; exit code 0 means
rem imported.
if /i "%~1"=="/s" (set "quiet=1") else (set "quiet=")
set "source=%~dp0Settings.ini"
set "folder=%APPDATA%\FrankyRenderer"

if not exist "%source%" (call :msg "No Settings.ini found in this folder." & exit /b 2)
findstr /b /c:"Storage=1" "%source%" >nul || (call :msg "This Settings.ini is not a FrankyRenderer settings file. Create one with Backup Settings.cmd or Export Settings in the menu." & exit /b 4)
for %%p in (PotPlayerMini64 PotPlayer64 mpc-be64 mpc-hc64) do (
    tasklist /nh /fi "imagename eq %%p.exe" | find /i "%%p" >nul && (call :msg "Please close %%p first." & exit /b 3)
)

if not defined quiet (
    choice /c YN /n /m "Replace your FrankyRenderer settings with this Settings.ini? [Y/N] "
    if errorlevel 2 goto cancel
    echo.
)
if not exist "%folder%" mkdir "%folder%" >nul 2>&1
copy /y "%source%" "%folder%\Settings.ini" >nul 2>&1 || (call :msg "The settings could not be written." & exit /b 1)
call :msg "Settings imported."
exit /b 0

:cancel
exit /b 6

:msg
if not defined quiet (echo %~1& echo.& pause)
exit /b
