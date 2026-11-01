@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT Tests\test_render_probe.cpp /Fobuild\obj\test_render_probe.obj /Febuild\bin\test_render_probe.exe
if errorlevel 1 exit /b 1
build\bin\test_render_probe.exe
