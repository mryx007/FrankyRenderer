@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist build\image-position\obj mkdir build\image-position\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I deps\msys\ucrt64\include Renderer\Backend.cpp Renderer\ColorManagement.cpp Renderer\PixelLayout.cpp Settings\Settings.cpp Tests\test_image_position.cpp /Fobuild\image-position\obj\ /Febuild\bin\test_image_position.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib user32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
set "PLACEBO_RENDERER_TEST_ROOT=Software\FrankyRendererShiftImageTests"
set "PLACEBO_RENDERER_TEST_INI=%CD%\build\image-position\Settings.ini"
build\bin\test_image_position.exe
exit /b %errorlevel%
