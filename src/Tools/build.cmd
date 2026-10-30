@echo off
setlocal
if not "%~1"=="" exit /b 2
set "rendererBin=build\bin"
set "rendererObj=build\obj"
set "rendererImport=build\lib\FrankyRenderer.lib"
call "%~dp0..\Tools\build_base.cmd"
if errorlevel 1 exit /b 1
call "%~dp0..\Tools\build_overlay.cmd"
if errorlevel 1 exit /b 1
call "%~dp0..\Tools\vsenv.cmd"
cd /d "%~dp0..\"
if not exist "%rendererBin%" mkdir "%rendererBin%"
if not exist "%rendererObj%" mkdir "%rendererObj%"
xcopy /E /I /Y Shaders\PlayKit "%rendererBin%\shaders\PlayKit" >nul
if errorlevel 1 exit /b 1
lib /nologo /def:build\libplacebo.def /machine:x64 /out:build\lib\libplacebo.lib
if errorlevel 1 exit /b 1
rc /nologo /c65001 /fo"%rendererObj%\Renderer.res" Renderer\Renderer.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /I deps\lavffmpeg\include /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses /LD DolbyVision\Processor.cpp Renderer\Backend.cpp Renderer\ColorManagement.cpp Renderer\PixelLayout.cpp Renderer\MediaFormat.cpp Renderer\MediaSampleSideData.cpp Renderer\SideDataAllocator.cpp Settings\Settings.cpp Renderer\Filter.cpp UI\Direct2DSettingsHost.cpp UI\Direct2DSettingsPage.cpp Renderer\dllmain.cpp /Fo"%rendererObj%\\" /Fe"%rendererBin%\FrankyRenderer.ax" /link /LTCG /DEF:Renderer\Renderer.def /IMPLIB:"%rendererImport%" "%rendererObj%\Renderer.res" build\lib\BaseClasses.lib build\lib\libplacebo.lib deps\lavffmpeg\lib\avformat-lav-63.lib deps\lavffmpeg\lib\avcodec-lav-63.lib deps\lavffmpeg\lib\avutil-lav-61.lib deps\lavffmpeg\lib\libdovi.lib d3dcompiler.lib d2d1.lib dwrite.lib comdlg32.lib strmiids.lib d3d11.lib dxgi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib winmm.lib uuid.lib

if errorlevel 1 exit /b 1
copy /y deps\lavffmpeg\bin\*.dll "%rendererBin%\" >nul
exit /b %errorlevel%
