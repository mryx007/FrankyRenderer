@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir build\bin
if not exist "build\obj" mkdir build\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /DPLACEBO_TEST_PRESENT_PIXELS /I deps\msys\ucrt64\include Renderer\Backend.cpp Renderer\ColorManagement.cpp Renderer\PixelLayout.cpp Renderer\NguScaler.cpp Settings\Settings.cpp Tests\test_ngu_backend.cpp /Fobuild\obj\ /Febuild\bin\test_ngu_backend.exe /link build\lib\libplacebo.lib d3dcompiler.lib d3d11.lib dxgi.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
exit /b %errorlevel%
