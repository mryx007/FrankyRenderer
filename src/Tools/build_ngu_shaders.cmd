@echo off
setlocal
if not exist "build\bin\shaders\Ngu" mkdir "build\bin\shaders\Ngu"
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_layer.cso" "Shaders\Ngu\luma_1080_layer.hlsl"
exit /b %errorlevel%
