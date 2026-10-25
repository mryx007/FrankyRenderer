@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /I deps\msys\ucrt64\include test_image_analysis.cpp /Fobuild\obj\test_image_analysis.obj /Febuild\bin\test_image_analysis.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib user32.lib gdi32.lib
