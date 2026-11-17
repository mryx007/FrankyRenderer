# FrankyRenderer repository instructions

## Immutable iron rule: native scalers use the shared renderer pipeline

This section is user-mandated and permanent as of 2026-11-13. No agent may
delete, weaken, reinterpret, or bypass it. A change requires explicit user
approval.

- NGU and every future native scaler are peers of the existing image and plane
  scalers. They consume shared libplacebo frame/hook inputs and return a
  texture plus correct rectangle, color representation, component and
  colorspace metadata to the common renderer pipeline.
- Use the libplacebo render-hook/scaler stage. `LUMA_INPUT`, `CHROMA_INPUT`,
  `RGB`, `NATIVE` or another placement and graph may be selected only from REA
  Evidence. A `pl_filter_config` is insufficient for NGU's multi-pass, MRT and
  SuperRes graph.
- Do not pre-empt the frame before `pl_render_image`, force a fixed output
  format/geometry such as `3840x2160 BGRA8`, replace the source frame and then
  call that normal scaler integration.
- A scoped release may advertise only observed capabilities such as
  `1920x1080 -> 3840x2160`. Restrict the capability itself, not the rendering
  architecture. Admitted inputs and outputs must use the shared pipeline.
- Do not add format-by-format or window-size branches in Backend when the
  shared frame/hook contract can carry the information. Extend the common
  adapter instead.
- Crop/aspect/window resampling, color management, dithering, presentation and
  host lifecycle remain common-renderer responsibilities unless target
  Evidence explicitly assigns a stage to the native scaler.
- Every new native scaler uses the same adapter contract. Unsupported or
  unobserved hook/metadata mappings fail closed; never add a guessed
  normalizer, silent fallback or synthetic fixed-target path.

## Immutable iron rule: HDR presentation belongs to Franky

This section is user-mandated and permanent as of 2026-11-17. No agent may
delete, weaken, reinterpret, or bypass it. A change requires explicit user
approval.

- Franky owns HDR/SDR policy, source/display range interpretation,
  linearization policy, presentation transfer and primaries conversion, gamut
  mapping, tone mapping, inverse tone mapping, peak detection, HDR metadata
  precedence and policy, display encoding, final dither and presentation.
- NGU is an ordinary native scaler peer. It consumes the shared
  `pl_frame`/hook input, performs only the source-domain work assigned to the
  NGU graph by target Evidence, and returns the texture with correct rectangle,
  components, representation, transfer, primaries, range and metadata.
- Evidence-backed source-domain work is allowed inside NGU. This includes
  target-assigned YCbCr-to-RGB, matrix/range handling and color-domain constants
  at stages such as `PS_396`, `PS_4412` and `PS_4576`. It does not transfer HDR
  presentation ownership to NGU.
- NGU must not choose or execute the madVR HDR presentation tail
  (`PS_340/1200/1208/1012/1368`), select a tone mapper or display peak, choose
  the target transfer/primaries or HDR output mode, rewrite HDR metadata, or
  perform the final presentation conversion or dither.
- Franky's shared HDR implementation may reproduce target-observed
  presentation behavior from its own Evidence. That work belongs to the common
  renderer, not to NGU and not to a second whole-frame pipeline.
- Unknown ownership, hook placement, metadata mapping or stage selection fails
  closed. The existence of a general Franky HDR path does not authorize an
  unevidenced NGU color-domain or target row.
- "Complete HDR" means both the Franky presentation path and the admitted NGU
  source-domain handoffs are implemented and verified. It is never claimed from
  an interface, a non-black frame, a capture, a fallback or a visual match.

### Current NGU V1 release boundary

This subsection records the shipped/current V1 limits. It is not a permanent
ceiling on the next HDR milestone; extensions still require their own Evidence
and numerical gates.

V1 is VeryHigh, SuperRes-strength-3 NGU with a target-aware plan. Existing
verified 720p/1080p paths use the same hook/input/output contract. The observed
1080-to-1080 no-luma branch and 2x/4x target relations are in scope; target
reduction after the NGU intermediate is owned by Franky's configured
downscaler.

At v1.5.1, the admitted HDR source domains are the target-observed
limited-range P010 BT.2020/PQ 720p/1080p, BT.2020/HLG 1080p and BT.709/PQ
1080p inputs. NGU uses the corresponding source-domain SuperChromaRes
constants, stops at source-resolution `PS_4576`, and returns RGBA16 to
`PL_HOOK_RGB`. Franky owns transfer handling, primaries conversion, tone
mapping, peak metadata, display encoding and every later scaling stage. NGU
does not own the madVR HDR presentation tail
(`PS_340/1200/1208/1012/1368`). Unobserved source geometries, other HDR
formats, transfer/primaries combinations, unadmitted Low/Medium/High rows,
formats and activation/tail branches fail closed.

NGU remains behind the shared libplacebo frame/hook interface. The NGU module
must not implement madVR's NGU-specific post-downscaler algorithms. A future
port of an individual madVR downscaler is a separate Franky algorithm module.

The `ngu_hook_output` shader is a storage-order-only adapter for the target
FinalStep texture. It must not alter NGU arithmetic or presentation intent.
Backend and DirectShow verifiers compare one semantic channel order; a
channel swap in either verifier is not an accepted fix. V1 is complete only
after same-frame comparison, DirectShow playback, artifact hashes and the
repository gates are recorded.

Ordinary SDR NV12/RGB/P010 paths use `PL_HOOK_RGB` so window and fullscreen
presentation scaling remains in the common renderer. The observed class-3
P210/P216 graph retains its 4K `PL_HOOK_OUTPUT` boundary until a separate
precision contract closes RGB-stage class-3 output. Unsupported class-3
window targets must fall back visibly to the common renderer, never black.
