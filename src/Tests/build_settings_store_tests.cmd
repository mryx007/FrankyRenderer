@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
if not exist src\build\obj\test_settings_store mkdir src\build\obj\test_settings_store
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I src /I src\deps\msys\ucrt64\include src\Tests\test_settings_store.cpp src\Settings\Settings.cpp /Fosrc\build\obj\test_settings_store\ /Fesrc\build\bin\test_settings_store.exe /link src\build\lib\libplacebo.lib advapi32.lib
if errorlevel 1 exit /b 1
if defined FRANKY_TEST_BUILD_ONLY exit /b 0
src\build\bin\test_settings_store.exe
