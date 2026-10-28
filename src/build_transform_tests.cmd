@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build\transform-tests\obj mkdir build\transform-tests\obj
if not exist build\transform-tests\bin mkdir build\transform-tests\bin
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DNOMINMAX /I deps\msys\ucrt64\include Backend.cpp ColorManagement.cpp PixelLayout.cpp Settings.cpp test_transform.cpp /Fobuild\transform-tests\obj\ /Febuild\transform-tests\bin\test_transform.exe /link build\lib\libplacebo.lib d3d11.lib dxgi.lib user32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
copy /y build\bin\*.dll build\transform-tests\bin\ >nul
if errorlevel 1 exit /b 1
if "%~1"=="" (
 build\transform-tests\bin\test_transform.exe "%~dp0build\bin\FrankyRenderer.ax"
) else (
 build\transform-tests\bin\test_transform.exe "%~f1"
)
exit /b %errorlevel%
