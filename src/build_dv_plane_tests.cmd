@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /MT /I deps\lavffmpeg\include /I deps\msys\ucrt64\include test_dv_planes.cpp /Fobuild\obj\test_dv_planes.obj /Febuild\bin\test_dv_planes.exe /link build\lib\libplacebo.lib deps\lavffmpeg\lib\avutil-lav-61.lib d3d11.lib d3dcompiler.lib
exit /b %errorlevel%
