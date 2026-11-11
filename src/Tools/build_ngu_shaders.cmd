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
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_2160_up_x.cso" "Shaders\Ngu\luma_2160_up_x.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_2160_up_y.cso" "Shaders\Ngu\luma_2160_up_y.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_2160_merge.cso" "Shaders\Ngu\luma_2160_merge.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_2160_final.cso" "Shaders\Ngu\luma_2160_final.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\superres_down_x.cso" "Shaders\Ngu\superres_down_x.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\superres_down_y.cso" "Shaders\Ngu\superres_down_y.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\superres_diff.cso" "Shaders\Ngu\superres_diff.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /Gec /T ps_5_0 /E PS /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\superres_to_rgb_main.cso" "Shaders\Ngu\superres_to_rgb_main.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /Gec /T ps_5_0 /E PS /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\superres_to_rgb_base.cso" "Shaders\Ngu\superres_to_rgb_base.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /Fo "build\bin\shaders\Ngu\superres_asm_main.cso" "Shaders\Ngu\superres_asm_main.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /Fo "build\bin\shaders\Ngu\superres_asm_base.cso" "Shaders\Ngu\superres_asm_base.hlsl"
if errorlevel 1 exit /b 1
exit /b %errorlevel%
