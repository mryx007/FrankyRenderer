# NGU Sharp integration

Branch: `FrankyRenderer_NGU`

## Goal

Add madVR-compatible NGU Sharp image processing to FrankyRenderer without
changing Franky's player, D3D11 or color-management architecture. The madVR
v0.92.17 binary and cited OpenMadVR Evidence remain the behavior oracle.

## Existing extension points

- `src/Renderer/ShaderHooks.h`: parses mpv-compatible multi-pass GLSL hooks.
- `src/Renderer/Backend.cpp`: submits the hook chain to libplacebo.
- `src/Settings/ShaderCatalog.h`: persistent shader selection/parameters.
- `src/Shaders/PlayKit/`: bundled shader resources.
- `src/Tests/test_user_shaders.cpp`: shader-chain regression coverage.

## Proposed implementation

1. Add a separate NGU Sharp shader family rather than pretending it is a
   libplacebo built-in scaler.
2. For the first milestone, port the target-observed VeryHigh chroma plus
   SuperRes3 pass graph for 720p/1080p to 4K.
3. Bundle immutable NGU weight and scale tables and load them as shader
   textures.
4. Preserve half/float intermediate formats, sampler coordinates and pass
   order from the cited Evidence.
5. Compare every deterministic pass by SHA-256 before adding settings/UI.
6. Add luma image upscaling and all four Sharp quality levels after the first
   path is exact.

## Initial acceptance

- Same-frame target/product comparison.
- Final `maximum <= 1`, `diff_gt_1_pixels=0`.
- Real DirectShow playback through FrankyRenderer.
- No RTX 5090 or remote host dependency.

## Current local blocker

The clean checkout does not yet have Visual Studio 2022 C++ tools,
Windows SDK/`fxc`, Python 3.14, or hash-verified local dependencies.
