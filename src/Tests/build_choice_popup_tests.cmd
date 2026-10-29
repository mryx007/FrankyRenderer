@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE Tests\test_choice_popup.cpp /Fobuild\obj\test_choice_popup.obj /Febuild\bin\test_choice_popup.exe /link user32.lib gdi32.lib
if errorlevel 1 exit /b 1
build\bin\test_choice_popup.exe
