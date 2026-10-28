@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /I deps\msys\ucrt64\include test_dv_status.cpp /Fobuild\obj\test_dv_status.obj /Febuild\bin\test_dv_status.exe /link build\lib\libplacebo.lib
if errorlevel 1 exit /b 1
build\bin\test_dv_status.exe
exit /b %errorlevel%
