@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses test_dovi_mapping.cpp PixelLayout.cpp /Fobuild\obj\ /Febuild\bin\test_dovi_mapping.exe /link build\lib\libplacebo.lib strmiids.lib uuid.lib ole32.lib user32.lib
if errorlevel 1 exit /b 1
build\bin\test_dovi_mapping.exe
exit /b %errorlevel%
