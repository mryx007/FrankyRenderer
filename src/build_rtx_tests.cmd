@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /I deps\msys\ucrt64\include test_rtx_scaling.cpp /Fobuild\obj\test_rtx_scaling.obj /Febuild\bin\test_rtx_scaling.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib
if errorlevel 1 exit /b 1
build\bin\test_rtx_scaling.exe
exit /b %errorlevel%
