@echo off
setlocal
if not exist "build\bin\shaders\Ngu" mkdir "build\bin\shaders\Ngu"
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_layer.cso" "Shaders\Ngu\luma_1080_layer.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_dense.cso" "Shaders\Ngu\luma_1080_dense.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_scale_x.cso" "Shaders\Ngu\luma_1080_scale_x.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_scale_y.cso" "Shaders\Ngu\luma_1080_scale_y.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_add.cso" "Shaders\Ngu\luma_1080_add.hlsl"
exit /b %errorlevel%
