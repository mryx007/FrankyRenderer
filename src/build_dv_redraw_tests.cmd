@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\dv-redraw mkdir build\dv-redraw
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /DNOMINMAX /I deps\lavffmpeg\include /I deps\msys\ucrt64\include Backend.cpp ColorManagement.cpp PixelLayout.cpp Settings.cpp test_dv_redraw_race.cpp /Fobuild\dv-redraw\ /Febuild\bin\test_dv_redraw_race.exe /link build\lib\libplacebo.lib deps\lavffmpeg\lib\avutil-lav-61.lib d3dcompiler.lib d3d11.lib dxgi.lib user32.lib advapi32.lib ole32.lib
exit /b %errorlevel%
