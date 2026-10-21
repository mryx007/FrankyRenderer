@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include test_direct2d_model.cpp Settings.cpp /Fobuild\obj\ /Febuild\bin\test_direct2d_model.exe /link d2d1.lib dwrite.lib comdlg32.lib comctl32.lib build\lib\libplacebo.lib ole32.lib oleaut32.lib user32.lib gdi32.lib advapi32.lib
if errorlevel 1 exit /b 1
build\bin\test_direct2d_model.exe
