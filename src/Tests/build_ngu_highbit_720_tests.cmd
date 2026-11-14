@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir "build\bin"
if not exist "build\obj" mkdir "build\obj"
call "%~dp0..\Tools\build_ngu_shaders.cmd"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /I . Tests\test_ngu_highbit_720.cpp /Fobuild\obj\ /Febuild\bin\test_ngu_highbit_720.exe /link d3dcompiler.lib d3d11.lib dxgi.lib
if errorlevel 1 exit /b 1
exit /b %errorlevel%
