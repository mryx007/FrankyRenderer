@echo off
setlocal
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT test_motion_timing.cpp /Fobuild\obj\test_motion_timing.obj /Febuild\bin\test_motion_timing.exe /link user32.lib
