@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir build\bin
if not exist "build\obj" mkdir build\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /I . /I deps\msys\ucrt64\include Tests\test_diagnostic_report.cpp /Fobuild\obj\test_diagnostic_report.obj /Febuild\bin\test_diagnostic_report.exe /link advapi32.lib user32.lib
if errorlevel 1 exit /b 1
build\bin\test_diagnostic_report.exe
exit /b %errorlevel%
