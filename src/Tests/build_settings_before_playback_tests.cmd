@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I deps\msys\ucrt64\include Tests\test_settings_before_playback.cpp Settings\Settings.cpp /Fobuild\obj\ /Febuild\bin\test_settings_before_playback.exe /link build\lib\libplacebo.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
build\bin\test_settings_before_playback.exe
exit /b %errorlevel%
