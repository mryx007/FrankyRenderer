@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE Tests\test_display_modes.cpp /Fobuild\obj\test_display_modes.obj /Febuild\bin\test_display_modes.exe /link user32.lib advapi32.lib
