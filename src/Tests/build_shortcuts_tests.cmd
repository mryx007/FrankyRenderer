@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I src /I src\deps\msys\ucrt64\include src\Tests\test_shortcuts.cpp src\Settings\Settings.cpp /Fosrc\build\obj\ /Fesrc\build\bin\test_shortcuts.exe /link src\build\lib\libplacebo.lib advapi32.lib user32.lib comdlg32.lib comctl32.lib ole32.lib oleaut32.lib gdi32.lib
if errorlevel 1 exit /b 1
src\build\bin\test_shortcuts.exe
