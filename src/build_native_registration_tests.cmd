@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE test_native_registration.cpp /Fobuild\obj\test_native_registration.obj /Febuild\bin\test_native_registration.exe /link ole32.lib advapi32.lib strmiids.lib
if errorlevel 1 exit /b 1
if "%~1"=="" (
 build\bin\test_native_registration.exe "%~dp0build\bin\FrankyRenderer.ax"
) else (
 build\bin\test_native_registration.exe "%~f1"
)
exit /b %errorlevel%
