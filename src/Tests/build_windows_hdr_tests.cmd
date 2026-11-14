@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /I . /I deps\msys\ucrt64\include Tests\test_windows_hdr.cpp /Fobuild\obj\test_windows_hdr.obj /Febuild\bin\test_windows_hdr.exe /link user32.lib advapi32.lib
if errorlevel 1 exit /b 1
build\bin\test_windows_hdr.exe
exit /b %errorlevel%
