@echo off
setlocal
call "%~dp0..\Tools\build_overlay.cmd"
if errorlevel 1 exit /b 1
call "%~dp0..\Tools\vsenv.cmd"
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I deps\msys\ucrt64\include Renderer\Backend.cpp Renderer\ColorManagement.cpp Renderer\PixelLayout.cpp Settings\Settings.cpp Tests\test_color_mapping.cpp /Fobuild\obj\ /Febuild\bin\test_color_mapping.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib user32.lib advapi32.lib
