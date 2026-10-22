@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include test_display_profiles.cpp Settings.cpp /Fobuild\obj\ /Febuild\bin\test_display_profiles.exe /link build\lib\libplacebo.lib user32.lib advapi32.lib
