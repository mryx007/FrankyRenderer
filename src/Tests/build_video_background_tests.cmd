@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT Tests\test_video_background.cpp /Fobuild\obj\test_video_background.obj /Febuild\bin\test_video_background.exe /link user32.lib gdi32.lib
