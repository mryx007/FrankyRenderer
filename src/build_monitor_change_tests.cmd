@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE test_monitor_change.cpp /Fobuild\obj\test_monitor_change.obj /Febuild\bin\test_monitor_change.exe /link user32.lib
