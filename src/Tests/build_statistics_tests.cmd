@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /I deps\msys\ucrt64\include Tests\test_statistics_bitmap.cpp /Fobuild\obj\test_statistics_bitmap.obj /Febuild\bin\test_statistics_bitmap.exe /link build\lib\libplacebo.lib user32.lib gdi32.lib d3d11.lib dxgi.lib
