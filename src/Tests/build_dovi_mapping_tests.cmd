@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist build\obj\test_dovi_mapping mkdir build\obj\test_dovi_mapping
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses Tests\test_dovi_mapping.cpp Renderer\PixelLayout.cpp /Fobuild\obj\test_dovi_mapping\ /Febuild\bin\test_dovi_mapping.exe /link build\lib\libplacebo.lib strmiids.lib uuid.lib ole32.lib user32.lib
if errorlevel 1 exit /b 1
if defined FRANKY_TEST_BUILD_ONLY exit /b 0
build\bin\test_dovi_mapping.exe
exit /b %errorlevel%
