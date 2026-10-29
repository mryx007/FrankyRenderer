@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /I deps\msys\ucrt64\include Tests\test_dv_motion.cpp /Fobuild\obj\test_dv_motion.obj /Febuild\bin\test_dv_motion.exe /link build\obj\ColorManagement.obj build\obj\PixelLayout.obj build\lib\libplacebo.lib d3dcompiler.lib d2d1.lib dwrite.lib d3d11.lib dxgi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib winmm.lib uuid.lib
exit /b %errorlevel%
