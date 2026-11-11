# NGU Sharp integration

Branch: `FrankyRenderer_NGU`

## Goal

Add madVR-compatible NGU Sharp image processing to FrankyRenderer while keeping
changes narrow enough to review, maintain and ultimately merge upstream. The
madVR v0.92.17 binary and cited OpenMadVR Evidence remain the behavior oracle.

On 2026-11-11 the user explicitly authorized direct source changes for this
work. The preferred product form is a dedicated native scaling module selected
as `ngu_sharp`, peer to `ewa_lanczos` and the other normal scaler choices.

## Existing extension points

- `src/Renderer/ShaderHooks.h`: parses mpv-compatible multi-pass GLSL hooks.
- `src/Renderer/Backend.cpp`: submits the hook chain to libplacebo.
- `src/Settings/ShaderCatalog.h`: persistent shader selection/parameters.
- `src/Shaders/PlayKit/`: bundled shader resources.
- `src/Tests/test_user_shaders.cpp`: shader-chain regression coverage.
- `src/Settings/SettingsChoices.h`: scaler selection identifiers and labels.
- `src/UI/Direct2DSettingsModel.h`: curated scaler choices and descriptions.
- `src/Renderer/RenderOptions.h`: settings-to-render-parameter conversion.

## Implementation boundary

NGU is a native FrankyRenderer scaling algorithm. It should live behind a
dedicated `NguScaler` module and integrate through generic host interfaces:

- add `ngu_sharp` to the normal scaler identifier and UI choice lists only when
  a real execution path exists;
- keep NGU scheduling out of unrelated EWA/filter code;
- prefer existing public or internal libplacebo/D3D11 interfaces;
- keep any required host change generic and independently reviewable;
- do not require a libplacebo fork for the baseline;
- no D3D9 bridge;
- use immutable, target-derived weights, constants and scale tables;
- use explicit L16/G16R16/F16 intermediate textures where the target evidence
  requires them;
- preserve the target's native MRT topology instead of recomputing split
  single-output passes;
- leave colour management, presentation and final output ownership with the
  existing renderer;
- fail clearly for unsupported quality/geometry/format paths;
- never publish an `ngu_sharp` name that silently resolves to bilinear.

The existing PlayKit NGU GLSL packs remain useful as pass-by-pass validation
oracles and experimental references. They are not the required final product
transport. In particular, the observed SuperChromaRes strength-3 chain uses
three 16-bit RGB surfaces downstream of Realign; a single RGBA16F user-shader
`SAVE` cannot preserve them exactly without extra producers or recomputation.

## Implementation order

1. Add an isolated `NguScaler` lifecycle and capability boundary.
2. Move the already target-exact VeryHigh chroma/luma cores into native
   scheduling without changing arithmetic.
3. Add explicit L16/G16R16/MRT contracts and the SuperChromaRes strength-3
   tail in target order.
4. Integrate image upscaling, merge and FinalStep for the primary 720p/1080p
   to 4K path.
5. Register `ngu_sharp` in settings/UI only after the native path renders.
6. Verify final image and DirectShow playback before enabling Low/Medium/High
   beyond experimental status.

## Initial acceptance

- Same-frame target/product comparison.
- Final `maximum <= 1`, `diff_gt_1_pixels=0`.
- Real DirectShow playback through FrankyRenderer.
- No RTX 5090 or remote host dependency.

## Current implementation state

The branch has buildable experimental shader packs, but no native `NguScaler`
module and no NGU scaler selection yet. Existing shader-pack exactness is
evidence of pass arithmetic, not completion of the native product path.
