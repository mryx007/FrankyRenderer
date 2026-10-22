@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I src\deps\msys\ucrt64\include src\test_settings_ini.cpp src\Settings.cpp /Fosrc\build\obj\ /Fesrc\build\bin\test_settings_ini.exe /link src\build\lib\libplacebo.lib advapi32.lib
if errorlevel 1 exit /b 1
src\build\bin\test_settings_ini.exe
