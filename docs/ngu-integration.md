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

## Implementation boundary

NGU will be an external mpv-compatible GLSL shader pack. It must use the public
libplacebo user-shader contract and the existing Franky hook submission path:

- no libplacebo source changes;
- no NGU logic inside `Backend.cpp`;
- no D3D9 bridge;
- immutable NGU weights/tables embedded as `TEXTURE` assets;
- intermediate passes connected with `SAVE`/`BIND` and sized with
  `WIDTH`/`HEIGHT`;
- luma/chroma attachment through `HOOK LUMA` and `HOOK CHROMA`;
- colour conversion and final presentation left to libplacebo.

Single-output hook passes cannot directly reproduce a two- or four-target
target shader. An MRT stage is represented by several passes evaluating the
same deterministic shader body and returning one target each. This preserves
the data dependency while increasing GPU work. It is accepted only after
pass-level numerical comparison.

The current catalogue is compiled into `ShaderCatalog.h`, so the eventual
loader needs a small manifest/discovery layer. The first feasibility pack may
register one temporary catalogue entry, but the shader implementation itself
must remain external and replaceable.

## Implementation order

1. For the first milestone, port the target-observed VeryHigh chroma plus
   SuperRes3 pass graph for 720p/1080p to 4K.
2. Prove the MRT split and embedded table sampling pass by pass.
3. Port VeryHigh luma, then compare same-frame 4K output.
4. Compare every deterministic pass by SHA-256 before adding settings/UI.
5. Add luma image upscaling and all four Sharp quality levels after the first
   path is exact.

## Initial acceptance

- Same-frame target/product comparison.
- Final `maximum <= 1`, `diff_gt_1_pixels=0`.
- Real DirectShow playback through FrankyRenderer.
- No RTX 5090 or remote host dependency.

## Current local blocker

The clean checkout does not yet have Visual Studio 2022 C++ tools,
Windows SDK/`fxc`, Python 3.14, or hash-verified local dependencies.
