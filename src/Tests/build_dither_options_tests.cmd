@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir build\bin
if not exist "build\obj" mkdir build\obj
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /I . /I deps\msys\ucrt64\include Tests\test_dither_options.cpp /Fobuild\obj\test_dither_options.obj /Febuild\bin\test_dither_options.exe /link build\lib\libplacebo.lib
if errorlevel 1 exit /b 1
build\bin\test_dither_options.exe
exit /b %errorlevel%
