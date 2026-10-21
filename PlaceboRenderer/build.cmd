@echo off
setlocal
call "%~dp0build_base.cmd"
if errorlevel 1 exit /b 1
call "%~dp0build_overlay.cmd"
if errorlevel 1 exit /b 1
call "%~dp0vsenv.cmd"
cd /d "%~dp0"
xcopy /E /I /Y shaders\PlayKit build\bin\shaders\PlayKit >nul
if errorlevel 1 exit /b 1
lib /nologo /def:build\libplacebo.def /machine:x64 /out:build\lib\libplacebo.lib
if errorlevel 1 exit /b 1
rc /nologo /c65001 /fobuild\obj\Renderer.res Renderer.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DNOMINMAX /I deps\msys\ucrt64\include /I ..\MPCVideoRenderer\external\BaseClasses /LD Backend.cpp ColorManagement.cpp HdrDisplay.cpp PixelLayout.cpp MediaFormat.cpp MediaSampleSideData.cpp SideDataAllocator.cpp Settings.cpp Filter.cpp PropertyPage.cpp ScalingPage.cpp ColorMappingPage.cpp OutputOptions.cpp TonalEditor.cpp ShaderPage.cpp dllmain.cpp /Fobuild\obj\ /Febuild\bin\PlaceboRenderer.ax /link /LTCG /DEF:Renderer.def /IMPLIB:build\lib\PlaceboRenderer.lib build\obj\Renderer.res build\lib\BaseClasses.lib build\lib\libplacebo.lib comdlg32.lib strmiids.lib d3d11.lib dxgi.lib user32.lib gdi32.lib ole32.lib oleaut32.lib advapi32.lib winmm.lib uuid.lib
