@echo off
setlocal
if not exist "build\bin\shaders\Ngu" mkdir "build\bin\shaders\Ngu"
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_layer.cso" "Shaders\Ngu\luma_1080_layer.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_1080_split.cso" "Shaders\Ngu\luma_1080_split.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_source_copy.cso" "Shaders\Ngu\ngu_source_copy.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_chroma_split.cso" "Shaders\Ngu\ngu_chroma_split.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /DNGU_SCALE_X /Fo "build\bin\shaders\Ngu\ngu_chroma_scale_x.cso" "Shaders\Ngu\ngu_chroma_scale.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /DNGU_SCALE_Y /Fo "build\bin\shaders\Ngu\ngu_chroma_scale_y.cso" "Shaders\Ngu\ngu_chroma_scale.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_chroma_cubici2.cso" "Shaders\Ngu\ngu_chroma_cubici2.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_chroma_layer.cso" "Shaders\Ngu\ngu_chroma_layer.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_chroma_dense.cso" "Shaders\Ngu\ngu_chroma_dense.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_chroma_combine1.cso" "Shaders\Ngu\ngu_chroma_combine.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /DNGU_COMBINE_2 /Fo "build\bin\shaders\Ngu\ngu_chroma_combine2.cso" "Shaders\Ngu\ngu_chroma_combine.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_chroma_realign.cso" "Shaders\Ngu\ngu_chroma_realign.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\ngu_chroma_ycbcr.cso" "Shaders\Ngu\ngu_chroma_ycbcr.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_scale_x.cso" "Shaders\Ngu\luma_4x720_scale_x.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_scale_y.cso" "Shaders\Ngu\luma_4x720_scale_y.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_upsample.cso" "Shaders\Ngu\luma_4x720_upsample.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_convert.cso" "Shaders\Ngu\luma_4x720_convert.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_tail_ycbcr.cso" "Shaders\Ngu\luma_4x720_tail_ycbcr.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /Fo "build\bin\shaders\Ngu\luma_4x720_tail_down_x.cso" "Shaders\Ngu\luma_4x720_tail_down_x.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /Fo "build\bin\shaders\Ngu\luma_4x720_tail_down_y.cso" "Shaders\Ngu\luma_4x720_tail_down_y.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_tail_ssim_rt.cso" "Shaders\Ngu\luma_4x720_tail_ssim_rt.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_tail_ssim_final.cso" "Shaders\Ngu\luma_4x720_tail_ssim_final.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_tail_sharpen_ar.cso" "Shaders\Ngu\luma_4x720_tail_sharpen_ar.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_tail_chroma_x.cso" "Shaders\Ngu\luma_4x720_tail_chroma_x.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /Fo "build\bin\shaders\Ngu\luma_4x720_tail_chroma_y.cso" "Shaders\Ngu\luma_4x720_tail_chroma_y.hlsl"
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
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /DNGU_SUPERRES_DOWN_WIDTH=960 /Fo "build\bin\shaders\Ngu\superres_down_x_1080.cso" "Shaders\Ngu\superres_down_x.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /DNGU_SUPERRES_DOWN_WIDTH=960 /Fo "build\bin\shaders\Ngu\superres_down_y_1080.cso" "Shaders\Ngu\superres_down_y.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gfp:strict /DNGU_SUPERRES_DOWN_WIDTH=960 /Fo "build\bin\shaders\Ngu\superres_diff_1080.cso" "Shaders\Ngu\superres_diff.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /DNGU_SUPERRES_FULL_WIDTH=1920 /DNGU_SUPERRES_FULL_HEIGHT=1080 /Fo "build\bin\shaders\Ngu\superres_asm_main_1080.cso" "Shaders\Ngu\superres_asm_main.hlsl"
if errorlevel 1 exit /b 1
fxc /nologo /T ps_5_0 /E PSMain /O3 /Gis /DNGU_SUPERRES_FULL_WIDTH=1920 /DNGU_SUPERRES_FULL_HEIGHT=1080 /Fo "build\bin\shaders\Ngu\superres_asm_base_1080.cso" "Shaders\Ngu\superres_asm_base.hlsl"
if errorlevel 1 exit /b 1
exit /b %errorlevel%
