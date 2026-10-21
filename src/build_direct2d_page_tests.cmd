@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include test_direct2d_page.cpp /Fobuild\obj\test_direct2d_page.obj /Febuild\bin\test_direct2d_page.exe /link ole32.lib oleaut32.lib user32.lib gdi32.lib comctl32.lib uuid.lib
if errorlevel 1 exit /b 1
build\bin\test_direct2d_page.exe
