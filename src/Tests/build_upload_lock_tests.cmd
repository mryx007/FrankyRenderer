@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist build\upload-lock\obj mkdir build\upload-lock\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I deps\msys\ucrt64\include Renderer\Backend.cpp Renderer\ColorManagement.cpp Renderer\PixelLayout.cpp Settings\Settings.cpp Tests\test_upload_lock.cpp /Fobuild\upload-lock\obj\ /Febuild\bin\test_upload_lock.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib user32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
set "PLACEBO_RENDERER_TEST_ROOT=Software\FrankyRendererUploadLockTests"
set "PLACEBO_RENDERER_TEST_INI=%CD%\build\upload-lock\Settings.ini"
build\bin\test_upload_lock.exe
exit /b %errorlevel%
