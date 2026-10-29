@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist src\build\dv-source-probe mkdir src\build\dv-source-probe
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I src\deps\lavffmpeg\include /I src\deps\msys\ucrt64\include /I src\DirectShow\external\BaseClasses src\test_dv_source_probe.cpp src\DolbyVision\Processor.cpp src\PixelLayout.cpp /Fosrc\build\dv-source-probe\ /Fesrc\build\bin\test_dv_source_probe.exe /link src\build\lib\BaseClasses.lib src\build\lib\libplacebo.lib src\deps\lavffmpeg\lib\avformat-lav-63.lib src\deps\lavffmpeg\lib\avcodec-lav-63.lib src\deps\lavffmpeg\lib\avutil-lav-61.lib src\deps\lavffmpeg\lib\libdovi.lib d3d11.lib d3dcompiler.lib strmiids.lib ole32.lib oleaut32.lib uuid.lib winmm.lib advapi32.lib user32.lib
exit /b %errorlevel%
