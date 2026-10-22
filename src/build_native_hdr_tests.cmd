@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /I deps\msys\ucrt64\include test_native_hdr.cpp /Fobuild\obj\test_native_hdr.obj /Febuild\bin\test_native_hdr.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib user32.lib
