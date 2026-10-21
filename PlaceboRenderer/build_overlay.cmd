@echo off
setlocal
call "%~dp0vsenv.cmd"
if errorlevel 1 exit /b 1
set "overlayCompiler=%WindowsSdkDir%bin\%WindowsSDKVersion%x64\fxc.exe"
"%overlayCompiler%" /nologo /T vs_4_0 /E vertex_main /O3 /Fh "%~dp0build\OverlayVS.h" /Vn OverlayVS "%~dp0Overlay.hlsl" >nul
if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T ps_4_0 /E pixel_main /O3 /Fh "%~dp0build\OverlayPS.h" /Vn OverlayPS "%~dp0Overlay.hlsl" >nul
if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T vs_4_0 /E fullscreen_main /O3 /Fh "%~dp0build\OverlayFullscreen.h" /Vn OverlayFullscreen "%~dp0Overlay.hlsl" >nul
if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T ps_4_0 /E alpha_main /O3 /Fh "%~dp0build\OverlayAlpha.h" /Vn OverlayAlpha "%~dp0Overlay.hlsl" >nul

if errorlevel 1 exit /b 1
"%overlayCompiler%" /nologo /T cs_5_0 /E unpack422 /O3 /Fh "%~dp0build\Unpack422.h" /Vn Unpack422 "%~dp0Unpack422.hlsl" >nul
