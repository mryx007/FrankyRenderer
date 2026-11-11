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

This authorization is a change to the implementation route, not to the
behavioral oracle. The product-level identifier is `ngu_sharp`, but the current
branch still keeps that identifier out of the exposed scaler lists until there
is a complete, evidence-backed image path. The user-authorized selection-list
integration is therefore implemented as a staged capability: the generic list
path is prepared first; the name is published only when the native path can
execute without a silent fallback.

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

## Source-level peer contract

The upstreamable shape is a generic native-scaler boundary, not an NGU branch
spread through libplacebo or the presentation code:

1. Settings persist only the ordinary scaler identifier string. They do not
   grow NGU-specific fields or bypass the existing scaling-role model.
2. Scaler resolution first asks the native-scaler adapter whether it owns the
   requested identifier. If it does, the request is handled by `NguScaler`
   using explicit resources and formats.
3. If the native adapter does not own an identifier, resolution falls through
   to the existing `pl_options_set_str` filter path unchanged. `ngu_sharp` is
   never passed to `pl_options_set_str` as if it were a libplacebo kernel.
4. A native request that is known but unsupported (quality, geometry, format,
   missing resource, or incomplete graph) fails explicitly. It must never be
   converted into bilinear, a user-shader approximation, or a different NGU
   quality level.
5. The native module owns NGU-specific shader selection, samplers, textures,
   MRTs and scheduling. `Backend.cpp` only supplies source/frame state and
   receives the result; colour management and presentation remain generic.

This boundary is intentionally narrow enough to be reviewed as an additional
host capability. If the adapter is not accepted upstream, the NGU module and
its generated assets can be dropped without changing EWA, colour management,
or existing playback paths.

## Upstream merge slicing

Keep the merge request and local commits separable in this order:

1. Generic native-scaler capability/resolution plumbing with no NGU behavior.
2. `NguScaler` lifecycle, resource cache and explicit format contracts.
3. Target-derived shaders, immutable tables and reproducible asset manifests.
4. Target-ordered graph execution and Backend source/output binding.
5. `ngu_sharp` settings/UI exposure after the final-image and playback gates.
6. Regression, performance and documentation updates.

Do not combine an NGU arithmetic change with an EWA change, a libplacebo fork,
or a presentation-path rewrite. A reviewer must be able to see that ordinary
filters behave identically when the native adapter is absent.

## Performance change rules

Source-level optimization is allowed when it changes representation or
scheduling rather than target-visible arithmetic:

- choose explicit L16/G16R16/A16B16G16R16 UNORM or other evidence-required
  formats instead of packing independent RGB codes through RGBA16F;
- preserve observed native MRT topology where the target establishes it;
- use dedicated sampler state, especially for point-sampled LUT and UV inputs;
- allocate immutable resources once and reuse textures, views and samplers;
- avoid avoidable readbacks, uploads, copies and format conversions;
- fuse or batch passes only when the fused arithmetic and resource values are
  proved identical to the target pass-by-pass; optimization is not evidence
  for a changed arithmetic expression.

## Implementation order

1. Add an isolated `NguScaler` lifecycle and capability boundary.
2. Move the already target-exact VeryHigh chroma/luma cores into native
   scheduling without changing arithmetic.
3. Add explicit L16/G16R16/MRT contracts and the SuperChromaRes strength-3
   tail in target order.
4. Integrate image upscaling, merge and FinalStep for the primary 720p/1080p
   to 4K path.
5. Register `ngu_sharp` in settings/UI through the generic native-scaler
   resolution path only after the native path renders.
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
also exact; the complete 720p 4x luma core executes at `3.430 ms/frame`.
PS_2100 dense and PS_564 add now extend that path exactly to the native
`5120x2880` luma surface in `10.337 ms/frame`. The latest product AX is
`C50906926BCBDEDA378FC591957AA6D71EBEF0F167982F43B8B40DE2DF55C012`.

The complete captured 720p 4x post-network path is now stage-exact. The
captured `f36` tail is A16B16G16R16 UNORM rather than RGBA16F, and PS_536,
PS_1200, PS_1176, PS_1208, PS_1052, PS_5868, PS_1284 X/Y and PS_548 all report
`different=0 maximum=0` with their captured inputs. `PS_1200/1176` use the
61,440-byte separable D3D9 UV LUT. The 4x chroma X/Y normalization uses the
captured output-axis constant `c0.y`; the old 2x source-axis formula was only
valid for the 1080p geometry.

The completed `VeryHighLuma4x720Full` scheduler runs source RGB through the
720p 4x luma network, CbCr X/Y, SuperChromaRes-aware merge and owned-dither
FinalStep. The recorded source-to-final result is
`maximum=1`, `diff_gt_1_pixels=0`, `69.358 ms/frame`; the one-LSB boundary is
the expected time-seeded dither.

`ngu_sharp` is now an experimental normal scaler choice. Backend accepts a
top-down 1280x720 RGB48 source and 3840x2160 target, creates the exact source
view, runs the native graph and replaces the image with the 4K RGB result.
The Backend regression also reports `maximum=1`, `diff_gt_1_pixels=0`.
Unsupported geometry, source format, orientation or target size fail closed.

Backend dispatch now covers the captured 1920x1080 RGB48 path as well as
1280x720. Both select native schedulers and both are within the declared
one-LSB final boundary.

The native SuperChromaRes strength-3 scheduler can also feed its RGBA16 output
directly into the 720p full path without a readback. With the four captured
strength-3 inputs, the composed final result is `maximum=1` and
`diff_gt_1_pixels=0` in `20.811 ms/frame`. The product source path still needs
to produce those four inputs from the real chroma network/decoder.

This is selectable only for the experimental RGB48 Backend path. The DirectShow
filter graph renders five RGB48 frames through the normal lifecycle, but real
decoder/YUV source binding, graph-frame readback, the combined SuperChromaRes
strength-3 path, 1080p composition, other geometries/formats, lifecycle
coverage, and the performance matrix remain open.
