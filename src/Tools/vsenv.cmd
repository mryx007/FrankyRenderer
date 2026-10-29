@echo off
set "INCLUDE=%~dp0..;%INCLUDE%"
if defined VCToolsInstallDir exit /b 0
set "frankyVsWhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%frankyVsWhere%" (
 echo Install Visual Studio Build Tools with Desktop development with C++.
 exit /b 1
)
for /f "usebackq delims=" %%I in (`"%frankyVsWhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "frankyVsRoot=%%I"
if not defined frankyVsRoot exit /b 1
call "%frankyVsRoot%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
