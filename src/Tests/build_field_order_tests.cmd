@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist build\obj\test_field_order mkdir build\obj\test_field_order
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I . /I deps\msys\ucrt64\include Tests\test_field_order.cpp /Fobuild\obj\test_field_order\ /Febuild\bin\test_field_order.exe /link d3d11.lib
if errorlevel 1 exit /b 1
if defined FRANKY_TEST_BUILD_ONLY exit /b 0
build\bin\test_field_order.exe
exit /b %errorlevel%
