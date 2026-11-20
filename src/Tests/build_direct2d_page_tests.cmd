@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
cl /nologo /utf-8 /std:c++20 /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include Tests\test_direct2d_page.cpp /Fobuild\obj\test_direct2d_page.obj /Febuild\bin\test_direct2d_page.exe /link ole32.lib oleaut32.lib user32.lib gdi32.lib comctl32.lib uuid.lib
if errorlevel 1 exit /b 1
rem The page test loads the renderer and applies settings: run it only through the
rem runner, with its own settings key, never against the user's Settings.ini.
python "%~dp0run_tests.py" --direct2d-page
