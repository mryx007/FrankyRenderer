@echo off
setlocal
call "%~dp0build_overlay.cmd"
if errorlevel 1 exit /b 1
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I deps\msys\ucrt64\include Backend.cpp ColorManagement.cpp PixelLayout.cpp Settings.cpp test_color_mapping.cpp /Fobuild\obj\ /Febuild\bin\test_color_mapping.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib user32.lib advapi32.lib
