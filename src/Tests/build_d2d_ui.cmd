@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0..\"
if not exist "build\bin" mkdir build\bin
if not exist "build\obj" mkdir build\obj

cl /nologo /utf-8 /std:c++20 /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /I deps\msys\ucrt64\include Tests\test_d2d_ui.cpp UI\Direct2DSettingsHost.cpp Settings\Settings.cpp /Fobuild\obj\ /Febuild\bin\test_d2d_ui.exe /link d2d1.lib dwrite.lib build\lib\libplacebo.lib comdlg32.lib comctl32.lib ole32.lib oleaut32.lib user32.lib gdi32.lib advapi32.lib shell32.lib shlwapi.lib dwmapi.lib
