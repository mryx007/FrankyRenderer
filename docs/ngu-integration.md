# NGU Sharp integration

Branch: `FrankyRenderer_NGU`

## Goal

Add madVR-compatible NGU Sharp image processing to FrankyRenderer while keeping
changes narrow enough to review, maintain and ultimately merge upstream. The
madVR v0.92.17 binary and cited OpenMadVR Evidence remain the behavior oracle.

On 2026-11-11 the user explicitly authorized direct source changes for this
work. The preferred product form is a dedicated native scaling module selected
as `ngu_sharp`, peer to `ewa_lanczos` and the other normal scaler choices.

## Architecture decision (2026-11-12)

The user accepted performance-first changes to FrankyRenderer's scaling source.
NGU is therefore an implementation peer of the existing `ewa_lanczos`,
`ewa_lanczossharp`, `ewa_jinc`, and related algorithm identifiers, not a
user-shader-only extension.

This decision is required by observed madVR behavior. The SuperChromaRes
strength-3 chain carries independent 16-bit RGB results that cannot be
represented exactly by one public RGBA16F user-shader `SAVE` target. A native
algorithm may own explicit `R16_UNORM`, `R16G16_UNORM`, `R16G16B16A16_UNORM`,
and MRT surfaces, plus dedicated samplers and a reusable render graph. Those
implementation choices may change transport and scheduling, but not the
target-observed arithmetic.

To keep the change reviewable and upstreamable:

- select NGU through the ordinary scaler identifier/selection path;
- keep algorithm-specific execution in `NguScaler`, behind a narrow interface;
- avoid NGU conditionals in colour management, presentation, and unrelated
  EWA/filter code;
- keep host changes generic rather than requiring a libplacebo fork;
- package target-derived immutable tables and shader stages with reproducible
  generation scripts;
- initialize GPU state once, reuse textures/samplers/views, and avoid readbacks,
  copies, or format changes that alter target-visible results;
- reject unsupported quality, geometry, or format combinations explicitly.

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
- model the implementation as another algorithm selected by the same backend
  interface as the `ewa_*` family, rather than as a special case in the
  presentation path;
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

The branch now has a native `NguScaler` lifecycle and an exact one-call
1920x1080-to-4K VeryHigh luma scheduler. Native PS_460 Y/CbCr split, luma
initializer/layers/dense/scale/add, 4x reconstruction, merge, FinalStep, dither,
and the SuperChromaRes strength-3 primitive chain are covered by stage-exact
tests. A one-call strength-3 scheduler now owns and reuses the captured X/Y/diff
surfaces, validates every external format/geometry, and produces the target
PS_4576 output exactly at `0.156 ms/frame` on the validation geometry. The
1280x720 VeryHigh 4x luma low core now also runs natively through PS_460,
PS_520 X/Y scale, PS_1412 and all 15 low-resolution PS_3588 layers with every
checked stage exact at `1.278 ms/frame`. Three PS_4920 four-output upsamplers,
three PS_912 quadrant converters and all six high-resolution PS_3588 layers are
also exact; the complete 720p 4x luma core executes at `3.430 ms/frame`. The
latest product AX is
`63E62A59B954A861C8D48922E29788451FF32976CA247B8719A841D93093BA34`.

This is not yet a selectable complete scaler. Backend/source-surface binding,
real DirectShow playback, the combined chroma-plus-SuperRes-plus-luma chain,
other geometries/formats, lifecycle coverage, and the performance matrix remain
open. `ngu_sharp` must stay unexposed until the final-image and playback gates
pass.
