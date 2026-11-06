@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir build\bin
if not exist "build\obj" mkdir build\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I . Tests\test_translation.cpp /Fobuild\obj\test_translation.obj /Febuild\bin\test_translation.exe /link advapi32.lib user32.lib dwmapi.lib
if errorlevel 1 exit /b 1
build\bin\test_translation.exe
