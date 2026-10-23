@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT test_registration.cpp /Fobuild\obj\test_registration.obj /Febuild\bin\test_registration.exe /link ole32.lib advapi32.lib
if errorlevel 1 exit /b 1
build\bin\test_registration.exe
exit /b %errorlevel%
