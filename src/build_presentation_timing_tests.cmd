@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT test_presentation_timing.cpp /Fobuild\obj\test_presentation_timing.obj /Febuild\bin\test_presentation_timing.exe /link user32.lib
if errorlevel 1 exit /b 1
build\bin\test_presentation_timing.exe
