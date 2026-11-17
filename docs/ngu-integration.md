# NGU Sharp integration

Branch: `FrankyRenderer_NGU`

> This document is the Franky-side implementation log and Evidence map. It is
> not the active roadmap. Scope and work order are defined by
> `E:\OpenMadVR\docs\FRANKY_NGU_HDR_PLAN.md`, with the V1 foundation in
> `E:\OpenMadVR\docs\FRANKY_NGU_V1_PLAN.md`. Historical status paragraphs below
> must not be treated as an active backlog. OpenMadVR is a capture/Evidence
> corpus only; it is not a second renderer implementation target.

## Goal

Add madVR-compatible NGU Sharp image processing to FrankyRenderer while keeping
changes narrow enough to review, maintain and ultimately merge upstream. The
madVR v0.92.17 binary and cited OpenMadVR Evidence remain the behavior oracle.

On 2026-11-11 the user explicitly authorized direct source changes for this
work. The preferred product form is a dedicated native scaling module selected
as `ngu_sharp`, peer to `ewa_lanczos` and the other normal scaler choices.

## Canonical source boundary (2026-11-14)

Normal NGU execution now receives `NguSourceFrame` from the common Backend
adapter. It describes dimensions, luma/chroma depth, chroma sampling/layout,
range, color metadata, orientation and plane views. NV12, I420, P010, P016,
P210, P216 and RGB48 no longer select separate execution branches in
`Backend`; the adapter constructs one canonical descriptor.

I420 is losslessly interleaved by the common CPU-plane adapter. `NguScaler`
contains no `PixelFormat` dependency and selects its evidence-backed graph from
canonical semantics plus `NguTargetPlan`. The plan preserves 720 Double,
1080 no-luma, class-3 output-hook behavior and arbitrary `PL_HOOK_RGB` window
handoff.

Low/Medium/High are implemented for the target-observed Sharp paths recorded in
the capability matrix. They are not a claim of complete quality/geometry/HDR
coverage; unlisted paths remain fail-closed.

## HDR ownership boundary (2026-11-14; expanded 2026-11-17)

This is an immutable interface rule. FrankyRenderer is the sole owner of the
HDR presentation pipeline: HDR/SDR policy, source/display range interpretation,
presentation linearization, transfer and primaries conversion, gamut mapping,
tone mapping, peak detection, metadata precedence and policy, output encoding,
final presentation dither and presentation. NGU is not a second HDR renderer.

FrankyRenderer, not NGU, owns HDR scaling, transfer handling, primaries
conversion, tone mapping, peak metadata and output encoding. The NGU module
uses the observed source-domain constants for the P010 1280x720/1920x1080
BT.2020/PQ chroma and SuperChromaRes strength-3 core, stops at source-resolution
`PS_4576`, and returns `R16G16B16A16_UNORM` at `PL_HOOK_RGB`. Those constants
select the NGU arithmetic input domain; they are not a madVR HDR renderer.
The target's `PS_340/1200/1208/1012/1368` HDR presentation tail is not owned by
NGU. Franky's common HDR implementation may reproduce target-observed
presentation behavior from its own Evidence; it must not expose that work as an
NGU output contract or a second renderer.

`PS_396`, `PS_4412` and `PS_4576` are NGU source-domain arithmetic stages
recovered from target Evidence. Target-assigned YCbCr-to-RGB, matrix and range
operations in these stages are allowed. NGU must not linearize or tone-map for
a display, choose display peak or output transfer/primaries, change HDR peak
metadata, or make a second presentation conversion. The hook returns correct
source-domain metadata so Franky performs the only HDR presentation pass.
`PL_HOOK_RGB` defines the handoff position; it does not transfer HDR ownership
to NGU.

Future work must be phrased and scoped as integrating NGU with Franky's HDR
pipeline, never as implementing madVR's HDR pipeline in NGU. Any unobserved
transfer, primaries, range, format or target relation fails closed.

E-160 establishes the corresponding A/B evidence: the NGU feature network and
inputs are unchanged, while color-domain constants require the HDR profile.
E-171 adds the observed 1080p BT.2020/HLG and BT.709/PQ profiles, both exact
through `PS_4576`. In v1.5.1, the admitted source contract is limited-range
P010 at 720p/1080p, with only the observed BT.2020/PQ, BT.2020/HLG and
BT.709/PQ combinations. Other transfers, primaries, ranges and geometries fail
closed until the next-release HDR matrix supplies Evidence and gates.

E-166/STEP-156 close the frame-metadata integration gap. `NguSourceFrame`
takes its color from the current sample and uses the media-type description
only to fill unknown fields. A real DirectShow P010 source with PQ/BT.2020
media information and HDR10 side data reports `hdr=1`, plans
`1920x1080`, and returns a source-resolution HDR hook texture with
1000-nit/MaxCLL 1200/MaxFALL 400 metadata. Source and packaged AX pass the
same HDR lifecycle. Captured 1080p `PS_644/PS_1516/PS_396` are exact.

E-168/STEP-158 correct the 1080p dense-Cr oracle attribution and repeat the
raw stage audit with the canonical HDR plan selected before execution. The
native `PS_2672` denseCr output is exact. Before E-169, the first residual was
the target `PS_748` L16 write and propagated to `PS_4576`; the capture-backed
`PS_644/PS_1516/PS_396` audit was exact. The residual was the known
ps_3_0-to-SM5 output boundary, not a Franky HDR or madVR tail stage.

E-169/STEP-159 close that boundary. E-099/E-156 identify `/Gis
/Gfp:strict` as required for target PS_748 instruction order. The generic
chroma-scale X/Y builds now use that contract. Raw P010 1080p BT.2020/PQ runs
`PS_564 -> PS_4576` at `different=0 maximum=0`; captured 720p
`PS_260 -> PS_4576` is also exact. Source and packaged AX pass the real 720p
and 1080p HDR DirectShow lifecycle. The observed HDR NGU source-domain
handoff is now byte-exact through `PS_4576`.

The NGU source-format translation now lives in the common
`Renderer/NguSourceFormat.h` adapter. NV12, I420, P010, P016, P210, P216 and
RGB48 are mapped there; `Backend.cpp` no longer contains NGU format switches.
E-170 correlates a same-frame P016 1280x720 Double source and closes the
pre-presentation `PS_908` comparison at `different=0 maximum=0`. The target's
following `PS_1368` remains presentation work and is handed to Franky through
`PL_HOOK_RGB`.

The Backend accepts both decoder-copy RGBA16 packed chroma and the common
CPU-plane `R16G16_UNORM` representation. HDR never runs the SDR luma
network/FinalStep. Final HDR BMP equality against madVR is not an NGU gate
because post-`PS_4576` HDR presentation intentionally belongs to Franky. This
ownership boundary is not a claim that Franky's complete HDR presentation
matrix is finished.
E-100/E-160 show that the observed `PS_4576` ToRGB stage performs the captured
BT.2020 matrix conversion without a subsequent transfer conversion; the hook
therefore keeps the source PQ transfer and BT.2020 primaries on the RGBA16
handoff while Franky performs the only tone mapping and output encoding.

## Architecture decision: native scaler peer (accepted 2026-11-12)

The user accepted performance-first changes to FrankyRenderer's scaling source.
NGU is therefore an implementation peer of the existing `ewa_lanczos`,
`ewa_lanczossharp`, `ewa_jinc`, and related algorithm identifiers, not a
user-shader-only extension.

This is a deliberate route change. The public mpv-compatible shader-pack
transport remains valuable for isolated arithmetic validation, but it is no
longer the required product pipeline. The product pipeline is:

```text
persisted scaler id "ngu_sharp"
  -> generic native-scaler resolution
  -> NguScaler-owned D3D11 resources and pass schedule
  -> existing FrankyRenderer colour management and presentation
```

The immediate reason is E-094 [observed]: the SuperChromaRes strength-3 path
needs three independent 16-bit RGB codes, while one RGBA16F user-shader `SAVE`
surface can carry only two exact 16-bit codes via high/low halves. A native
module can use the target-required integer/UNORM/F16 surfaces and, where
observed, native MRT topology without lossy packing or producer recomputation.
This changes transport and scheduling only; it does not change arithmetic,
resource selection, pass order, or fallback behavior.

This authorization is a change to the implementation route, not to the
behavioral oracle. The product-level identifier is `ngu_sharp`; it is currently
published as an experimental normal scaler choice whose resolver owns only the
declared native contracts. Every unsupported geometry, format, quality or
resource path fails explicitly rather than falling through to a guessed
algorithm.

The accepted implementation may change FrankyRenderer's scaling source and
resource transport directly. In particular, it does not have to force the
SuperChromaRes strength-3 network's independent 16-bit RGB results through the
general public user-shader path or one packed intermediate. The native module
may own the target-observed R16/G16R16/RGBA16 UNORM surfaces, MRT topology,
samplers and scheduling while preserving the captured arithmetic pass by pass.
This is a performance-first transport decision, not permission to invent
missing target arithmetic.

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

The first generic resolver implementation is
`src/Settings/NativeScaler.h`: persisted identifiers resolve to a
`NativeScalerKind`, and the renderer stores that route instead of an
NGU-specific boolean. `ngu_sharp` resolves to the native NGU module; unknown or
ordinary identifiers resolve to `None` and continue through the existing
libplacebo path. Adding another native algorithm later should extend this
resolver rather than add another flag and selection branch.

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
6. Verify final image and DirectShow playback for each newly admitted
   Low/Medium/High row.

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

The captured-input 1920x1080 strength-3 composition now has a same-frame final
oracle. Capture draw 921-928 and draw 953 PS_1352 produce
`maximum=1`, `diff_gt_1_pixels=0`; the test selects the frame with
`NGU_1080_FRAME_BASE=921`. Earlier `maximum=21/40` results were cross-frame
comparisons and are no longer acceptance evidence.

The native source bridge now imports P010 decoder planes through D3D11.3 plane
views into `R16_UNORM` luma and `R16G16B16A16_UNORM` chroma without CPU
readback. The active 1280x720 crop and packed U/V values match the captured
target sample at zero difference. The local adapter reports no P016 hardware
resource support, so P016 continues through the common CPU-plane adapter;
hardware import remains unobserved.

The first chroma-core pass is native and exact: PS_260 SplitCb_Cr maps one
packed CbCr sample into L16 Cb and Cr with byte-identical target output. Complete
D3D9 state replay resolved the PS_748 coordinate contract: X uses effective
`c0.x=640`, while Y uses captured `c0.x=720` rather than a halved value. Native
PS_748 X/Y L16 passes now build. X matches its target-written triangle exactly;
Y reports `274/921600` changed samples, `maximum=1`, and
`diff_gt_1_pixels=0`. Explicit half-up and round-even conversion tests were
worse, so this remains a cross-API L16 rounding boundary rather than a
substituted coordinate formula.

The native chroma core now continues through PS_1672, all 28 PS_4620 layers and
PS_2672 dense. PS_1672 matches all four 640x360 RGBA16_FLOAT outputs exactly.
Each layer, tested with its captured inputs, stays within `3.052e-5`; the dense
stage is exact from captured inputs. The one-call
`RunNativeChromaVeryHigh720` scheduler reuses two four-texture groups and
produces a complete cascade within `3.29614e-5` of the captured dense surface
at `2.849 ms/frame` on this host. Canonical run3 L16 words are read as
R16_UNORM; the half-encoded capture variant is not interchangeable.

The native PS_564 L16 and PS_644 G16R16 combines are now connected to reusable
D3D11 resources inside the same `NguScaler` module. Complete D3D9 target replay
with the captured bytecode, textures, constants and render state is `0/0` for
both. The native SM5 translation is `maximum=1`, `diff_gt_1_pixels=0`
(PS_564: 30855 tied codes; PS_644: 35191 tied codes). Captured synthetic probes
fix the half-cell dense index, `A/B/G/R` component matrix and 1:1 add/Cb fetch;
the remaining one-code boundary is ps_3_0 versus SM5 floating-point and L16
conversion, not a missing mapping.

Cr uses the same network constants and scheduler resources as Cb. Its complete
dense result stays within `3.16799e-5`. PS_1516 Realign consumes the captured
G16R16 combine output using the exact `1/1280` constant and is bounded at
`maximum=1` with exact zero BA. PS_396 then packs Y/L8 and the Realign RG
surface exactly from the captured inputs. The connected captured-input
core -> combines -> Realign -> PS_396 -> strength-3 SuperChromaRes path reaches
PS_4576 at `maximum=6`, `diff_gt_8=0`.

`RunNativeChromaVeryHigh720Full` now starts from one same-frame packed-UV
surface. It constructs the Cb/Cr split, parallel X/Y scale branches, two dense
networks, combines, Realign, PS_396 and strength-3 SuperChromaRes internally,
reaching PS_4576 at `maximum=6`, `diff_gt_8=0` without captured add textures.

The same-frame result connects directly to `RunNativeLuma4x720Full`. The final
pipeline reports `maximum=5` with 17 pixels above one, while the captured-input
final path remains `maximum=1`, `diff_gt_1_pixels=0`. The combined gate is
explicitly pending and fails under `NGU_COMBINED_FINAL_GATE=1`.

`RunNativeNgu720FromPlanes` is the mergeable product entry: it accepts packed
UV and Y, reuses them as the observed diff/main/base originals, expands L8 to
RGB through a native transport shader, and executes the complete graph. Its
same-frame final residual is `maximum=5`, 18 pixels above one.

The native source-peer direction is closed rather than provisional:
`ngu_sharp` is a normal persisted scaler identifier resolved by the generic
native-scaler boundary, and NGU may own target-format D3D11 resources, MRTs,
samplers and scheduling without routing through an EWA or user-shader transport.
The `3 x 16-bit RGB` SuperChromaRes restriction is therefore no longer a
transport blocker. Target arithmetic, pass order, bindings and fallback behavior
remain evidence-bound, and unsupported contracts still fail closed.

The native chroma core now owns separate immutable 720p and 1080p constant sets.
Cubici2, layer and dense shaders derive raster dimensions from their inputs, and
the scheduler selects the 960x540 geometry only after validating its views.
Native 1080p PS_1672 matches all four captured float outputs exactly; the Cb/Cr
28-layer cascades reach PS_2672 with `maximum_abs` `3.28422e-05` and
`3.15905e-05`. The 1080p regression now starts from packed UV and runs native
split, X/Y scale, dense networks, PS_564/PS_644, Realign, PS_396 and the
connected strength-3 scheduler. PS_4576 is `maximum=5`, `diff_gt_8=0`; the
720p regression is unchanged. SuperRes diff/main/base originals remain captured
inputs, so this is not final decoder or playback acceptance.

`PrepareNativeChromaVeryHigh1080Full` and
`RunNativeChromaVeryHigh1080Full` now expose the complete graph as one call.
The 720p and 1080p paths share `NativeChromaFullResources` and one scheduler
implementation; geometry selects the observed constants and SuperRes variant.
The one-call 1080 result matches the manually scheduled PS_4576 result.

`PrepareNativeNgu1080FromPlanes` and `RunNativeNgu1080FromPlanes` extend that
boundary through Y expansion and the 1080p luma network to 3840x2160. The
same-frame capture proves packed UV = SuperRes diff original and Y =
main/base originals. A captured plane-entry run is fully nonblack; it remains
a smoke test until paired with a same-frame PS_1352 oracle.

Backend now accepts P010 for the declared 720p/1080p geometries and routes
decoder plane views into the one-call NGU entry. The RGB48 numerical gates
remain unchanged, and a Backend P010 smoke reaches a valid 4K output. A limited
experience ZIP is prepared; P010 final-image, real DirectShow lifecycle and
P016 hardware acceptance remain open.

The native SuperChromaRes strength-3 scheduler can also feed its RGBA16 output
directly into the 720p full path without a readback. With the four captured
strength-3 inputs, the composed final result is `maximum=1` and
`diff_gt_1_pixels=0` in `20.811 ms/frame`. The product source path still needs
to produce those four inputs from the real chroma network/decoder.

This is selectable only for the experimental RGB48 Backend path. The DirectShow
filter graph now renders five RGB48 frames and its full-frame readback matches
the captured PS_1352 final image at `maximum=1`. The verification source uses
the captured PS_460 input, declares the correct 16:9 display aspect and remains
SDR; the earlier black frame was a test-source 1:1 aspect error that introduced
840-pixel pillarboxes. This closes the RGB48 graph/readback boundary only.
Real decoder/YUV source binding, production of the combined SuperChromaRes
strength-3 inputs, 1080p composition, other geometries/formats, full lifecycle
coverage, and the performance matrix remain open.

## Distance to the first product milestone

The architecture decision is complete. The remaining distance is evidence and
product integration, not another transport redesign:

| Gate | Current evidence | Still required |
|---|---|---|
| Build | Full FrankyRenderer build passes with the generic native-scaler resolver | Repeat release packaging for the selected distribution |
| VeryHigh 720p luma | All checked stages `0/0`; final `maximum=1`, `diff_gt_1_pixels=0`; this run `62.789 ms/frame` (prior recorded run `69.358 ms/frame`) | Real decoder input and broader performance matrix |
| VeryHigh 1080p luma | Captured-input scheduler, Backend RGB48 and same-frame PS_1352 composition pass at `maximum=1`, no pixel above one | Real decoder input and production strength-3 source generation |
| SuperChromaRes strength 3 | Captured-input scheduler is `0/0`; composed 720p path is `maximum=1`, `diff_gt_1_pixels=0`; this run `10.003 ms/frame` (prior recorded run `20.811 ms/frame`) | Produce the four inputs from the real chroma/decoder network |
| Native HDR chroma | BT.2020/PQ 720p/1080p and BT.2020/HLG plus BT.709/PQ 1080p are exact through source-resolution `PS_4576`; arbitrary HDR targets hand off to Franky | Unobserved HDR combinations; complete Franky-owned HDR presentation is tracked separately in `FRANKY_NGU_HDR_PLAN.md` |
| Native 1080p plane entry | packed UV + Y reaches the same-frame `PS_1352` target | Out-of-scope ratios |
| Native product entry | P210/P216 1080p combined is `maximum=1`, `diff_gt_1_pixels=0` | Broader performance and format matrix |
| High-bit 720p Double | P010 uses the distinct `PS_484/1412/6140/4280x3/1528/1672/4620x28/2672/908` graph. E-170 correlates P016 and matches `PS_4576` and pre-presentation `PS_908` exactly. | P010 and CPU-plane P016 admitted; hardware P016 remains unknown |
| I420 720p Double | I420/NV12 canonical product equality is exact | No format-specific NGU branch |
| `ngu_sharp` peer | Generic resolver, RGB48/P010 and P210/P216 Backend gates pass | Upstream review and release packaging |
| DirectShow | P210/P216 720p and 1080p pass pause/flush/restart with final max 1 | Broader player matrix |
| Low/Medium/High | Implemented for the exact rows listed in the OpenMadVR capability matrix | Expand only through Evidence; do not infer unlisted quality/geometry/HDR rows |

The historical shortest path above is closed for the first VeryHigh slice by
E-154 through E-157. No missing mapping may be replaced by a guessed fallback.

## First experimental VeryHigh gate (2026-11-12)

E-148 corrects `NguDither` to the target's observed final MT recurrence,
`state[396] & (mixed >> 1) ^ magic`. The regression covers the captured seed
state, the first eight words, word 623 and a 65,536-word FNV-1a64 hash. This
fix changes target-visible color-noise state; it is not a tolerance adjustment.

Settings-sensitive NGU DirectShow tests now create an isolated per-process
registry root by default. Set `PLACEBO_RENDERER_TEST_ROOT` explicitly to retain
a caller-selected test profile. This prevents persisted display/profile values
from changing a same-frame oracle comparison.

The experimental milestone passes:

```text
RGB48 DirectShow maximum=1
Backend RGB/stored/1080 maximum=1, diff_gt_1_pixels=0
P010 DirectShow lifecycle
P010 1080 final PS_1352 maximum=1, diff_gt_1_pixels=0
SuperChromaRes strength-3 and tested VeryHigh stages exact or within declared bounds
```

It remains experimental. P016 is exact through PS_1672 but first exceeds the
declared PS_4620 layer tolerance at layer 8. The 720p combined final retains two
pixels above one pending exact target dither-surface replay. P210/P216 and the
real-player lifecycle matrix are still open.

The package overlay now copies generated NGU CSO assets in addition to source
HLSL. The expanded nightly ZIP passes RGB48 DirectShow and P010 lifecycle
smoke.

E-150 closes the remaining SDR presentation gap. Native FinalStep output is
handed to the renderer without a second libplacebo dither or source-transfer
conversion. DirectShow and the expanded package now pass P010 1080 at
`maximum=1`, `diff_gt_1_pixels=0`; RGB48 and Backend remain green.

On the P016 fixture, the presentation fix first reduced the final residual from
`maximum=114` over 1,437,693 pixels to `maximum=11` over seven pixels.
E-152 then matched the target PS_4620 accumulation order; all 28 layers and
dense outputs are bit-exact and P016 closes at `maximum=1`,
`diff_gt_1_pixels=0`.

The 720p FinalStep residual is also closed at the dither-surface boundary.
Replaying the captured target stage-5 texture with the byte-identical pre-final
input passes at `maximum=1`. Independent product/target clock seeds still vary
at two pixels; that variance is reported separately rather than treated as NGU
arithmetic failure.

The exact PS_4620 layer order also closes the native 720p combined path at
`maximum=1`, `diff_gt_1_pixels=0` without substituting a target dither surface.

The decoder-backed paused-seek lifecycle now also passes with installed LAV
0.80.0 and `test_paused_seek --ngu`: 4K paused preview, 5s/12s/2s seeks and
resume all update actual product pixels. External player process automation
remains a separate gate.

E-154 adds the first isolated P210/P216 class-3 720p scheduler slice. The
target `PS_6392/5140/4804` post-chain, class-3 `PS_536`, and the
`PS_1200/1176/1208/1052` tail are channel-exact for both formats; `PS_5868`
is `maximum=1` with no channel above one. The product continues to reject
P210/P216 because the available class-3 capture is chroma-only and does not
establish combined NGU-luma ordering. P210/P216 1080p class-3 also uses
observed `PS_1152/PS_4280` labels that are not yet implemented.

E-155 captures the combined target path at 720p. With NGU luma VeryHigh, NGU
chroma VeryHigh and SuperRes strength 3, class-3 chroma reaches `PS_4576` at
1280x720 and then enters the already implemented NGU-luma 4x scheduler at
`PS_460`. The SuperXbr chain in E-154 is the chroma-only branch. Product
admission therefore still waits on the exact class-3 `PS_4412/PS_4576`
producer and the 1080p `PS_1152/PS_4280` topology.

## First verified VeryHigh 1080p gate (2026-11-13)

E-157 supersedes the earlier `PS_1152/PS_4280` 1080p statement. The observed
P210/P216 VeryHigh class-3 1080p route is the same DAG at 2x geometry:
`PS_260` through `PS_4576`, followed by `PS_460` and the existing 1080p
NGU-luma path to `PS_1352`.

The implementation now includes the parameterized class-3 1080p core,
combined NGU-luma entry, Backend admission and DirectShow lifecycle:

```text
P210/P216 core stages 0,6,7,8,9,10,11: different=0 maximum=0
P210 combined PS_1352: maximum=1 diff_gt_1_pixels=0
P216 combined PS_1352: maximum=1 diff_gt_1_pixels=0
Backend P210-1080: maximum=1 diff_gt_1_pixels=0
Backend P216-1080: maximum=1 diff_gt_1_pixels=0
DirectShow P210/P216 1080p: pause, flush, restart PASS
```

The Backend timing includes plane upload, the native graph and CPU readback:
20.233 ms/frame for P210 and 19.148 ms/frame for P216. This is not presented as
a GPU-only shader timing claim.

At this checkpoint, Low/Medium/High remained experimental. Later releases
admitted the exact rows recorded in the current capability matrix; unlisted
paths continue to fail closed.

## Native-size fallback and settings placement (2026-11-15)

E-175 closes the player-facing 4K black-frame defect. If `ngu_sharp` is
selected for a source geometry outside the observed 1280x720/1920x1080 NGU
inputs and the presentation target does not enlarge that source, the native
hook stays inactive and the original shared `pl_frame` goes through the common
Franky renderer. The fallback restores the configured libplacebo scaling and
dither parameters. This is not a 4K NGU graph and does not admit unsupported
enlargement.

The same boundary keeps native NGU out of the libplacebo Custom Filter editor.
It is a renderer peer, not a filter preset with a synthetic kernel/window
contract. E-005/E-017/E-021 show that target anti-ringing selection belongs to
the NGU image-upscaling setting area. The exact strength-to-shader mapping is
still unknown, so the common global Anti-Ringing control is neither moved into
Custom Filters nor silently applied to native NGU.
