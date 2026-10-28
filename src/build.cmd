@echo off
setlocal
set "variant=%~1"
if not defined variant set "variant=native"
if /I not "%variant%"=="native" if /I not "%variant%"=="potplayer" exit /b 2
set "variantFlags="
set "variantBin=build\bin"
set "variantObj=build\obj"
set "variantImport=build\lib\FrankyRenderer.lib"
if /I "%variant%"=="potplayer" (
 set "variantFlags=/DFRANKY_POTPLAYER"
 set "variantBin=build\variants\potplayer\bin"
 set "variantObj=build\variants\potplayer\obj"
 set "variantImport=build\variants\potplayer\FrankyRenderer.lib"
)
call "%~dp0build_base.cmd"
if errorlevel 1 exit /b 1
call "%~dp0build_overlay.cmd"
if errorlevel 1 exit /b 1
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
if not exist "%variantBin%" mkdir "%variantBin%"
if not exist "%variantObj%" mkdir "%variantObj%"
xcopy /E /I /Y shaders\PlayKit "%variantBin%\shaders\PlayKit" >nul
if errorlevel 1 exit /b 1
lib /nologo /def:build\libplacebo.def /machine:x64 /out:build\lib\libplacebo.lib
if errorlevel 1 exit /b 1
rc /nologo /c65001 %variantFlags% /fo"%variantObj%\Renderer.res" Renderer.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX %variantFlags% /I deps\lavffmpeg\include /I deps\msys\ucrt64\include /I DirectShow\external\BaseClasses /LD DolbyVision\Processor.cpp Backend.cpp ColorManagement.cpp PixelLayout.cpp MediaFormat.cpp MediaSampleSideData.cpp SideDataAllocator.cpp Settings.cpp Filter.cpp Direct2DSettingsHost.cpp Direct2DSettingsPage.cpp dllmain.cpp /Fo"%variantObj%\\" /Fe"%variantBin%\FrankyRenderer.ax" /link /LTCG /DEF:Renderer.def /IMPLIB:"%variantImport%" "%variantObj%\Renderer.res" build\lib\BaseClasses.lib build\lib\libplacebo.lib deps\lavffmpeg\lib\avformat-lav-63.lib deps\lavffmpeg\lib\avcodec-lav-63.lib deps\lavffmpeg\lib\avutil-lav-61.lib deps\lavffmpeg\lib\libdovi.lib d3dcompiler.lib d2d1.lib dwrite.lib comdlg32.lib strmiids.lib d3d11.lib dxgi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib winmm.lib uuid.lib

if errorlevel 1 exit /b 1
copy /y deps\lavffmpeg\bin\*.dll "%variantBin%\" >nul
exit /b %errorlevel%
