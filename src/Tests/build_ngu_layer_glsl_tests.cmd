@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir build\bin
if not exist "build\obj" mkdir build\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /I . /I deps\msys\ucrt64\include Tests\test_ngu_layer_glsl.cpp /Fobuild\obj\test_ngu_layer_glsl.obj /Febuild\bin\test_ngu_layer_glsl.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib
if errorlevel 1 exit /b 1
exit /b %errorlevel%
