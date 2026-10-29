@echo off
setlocal
call "%~dp0..\Tools\vsenv.cmd"
if errorlevel 1 exit /b 1
set "overlayCompiler=%WindowsSdkDir%bin\%WindowsSDKVersion%x64\fxc.exe"
"%overlayCompiler%" /nologo /T vs_4_0 /E vertex_main /O3 /Fh "%~dp0..\build\OverlayVS.h" /Vn OverlayVS "%~dp0..\Shaders\Overlay.hlsl" >nul
if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T ps_4_0 /E pixel_main /O3 /Fh "%~dp0..\build\OverlayPS.h" /Vn OverlayPS "%~dp0..\Shaders\Overlay.hlsl" >nul
if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T vs_4_0 /E fullscreen_main /O3 /Fh "%~dp0..\build\OverlayFullscreen.h" /Vn OverlayFullscreen "%~dp0..\Shaders\Overlay.hlsl" >nul
if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T ps_4_0 /E alpha_main /O3 /Fh "%~dp0..\build\OverlayAlpha.h" /Vn OverlayAlpha "%~dp0..\Shaders\Overlay.hlsl" >nul

if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T cs_5_0 /E unpack422 /O3 /Fh "%~dp0..\build\Unpack422.h" /Vn Unpack422 "%~dp0..\Shaders\Unpack422.hlsl" >nul
