@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir build\bin
if not exist "build\obj" mkdir build\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I . /I deps\msys\ucrt64\include Tests\test_quality_benchmark.cpp Settings\Settings.cpp /Fobuild\obj\ /Febuild\bin\test_quality_benchmark.exe /link build\lib\libplacebo.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
build\bin\test_quality_benchmark.exe
