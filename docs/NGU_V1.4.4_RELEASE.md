# FrankyRenderer 1.4.4

> Superseded by `1.4.5`. The Cb/Cr order change in 1.4.4 was necessary but did
> not fix the hardware-decoder plane adapter. Real 4K playback could still
> receive neutral chroma. See `docs/NGU_V1.4.5_RELEASE.md`.

Status: stable local release prepared 2026-11-16.

Tag: annotated `v1.4.4`

This release corrects the 4K SDR NGU chroma handoff used by real 4K playback.
NGU is still a normal native scaler peer behind the shared renderer hook.

## Included

- Corrected the `PL_HOOK_CHROMA_INPUT` storage-order adapter from `Cr,Cb` to
  the target `PS_396` semantic order `Cb,Cr`.
- Preserved the target-exact 4K chroma NGU graph through Realign at Low,
  Medium, High and VeryHigh.
- Preserved Franky ownership of window/fullscreen reduction, color management,
  dither, presentation and lifecycle.
- Added a Cb/Cr axis regression that cannot pass when the two channels are
  swapped.
- Added explicit window and fullscreen BMP output to the 4K DirectShow test.

## Evidence

```text
OpenMadVR E-178
target PS_396_e3199f71 semantic packing
real JUR-285 3840x2160 SDR BT.709 limited-range frame 30
```

The target captured `PS_396` surface converted with limited BT.709 gives mean
channel error `0.331` with `Cb,Cr`; swapped `Cr,Cb` gives `119.883` and
`maximum=255`.

## Verification

```text
4K NGU chroma Low      11 surfaces different=0 maximum=0
4K NGU chroma Medium   10 surfaces different=0 maximum=0
4K NGU chroma High     11 surfaces different=0 maximum=0
4K NGU chroma VeryHigh 11 surfaces different=0 maximum=0

test_ngu_backend
  PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K
  4K Upload/Import different=0 maximum=0

real 4K frame 30, NGU / ordinary RGB means
  1080 window  135.362,130.500,104.485 / 134.647,129.194,107.414
  4K fullscreen 135.376,130.515,104.492 / equivalent ordinary path
  pre-fix 4K  99.447,123.107,132.886

DirectShow real file, source AX
  1920x1080 window       visible and color-matched
  3840x2160 fullscreen   visible and color-matched

real JUR-285 title frame, NGU / ordinary RGB means
  source AX 1080 window       5.659,5.007,4.616 / 5.625,5.005,4.713
  source AX 4K fullscreen     5.480,4.787,4.365 / 5.428,4.791,4.483
  package AX 1080 window      5.659,5.007,4.616 / 5.625,5.005,4.713
  package AX 4K fullscreen    5.480,4.787,4.365 / 5.428,4.791,4.483
```

Detailed artifact hashes are recorded in OpenMadVR `E-181` and
`STEP-181-ngu-4k-cb-cr-order.md`.

## Artifacts

```text
source AX
E:\FrankyRenderer\src\build\bin\FrankyRenderer.ax
44F06084C77D1CE683E9E56C4256E1E88B495ED35819C6C8CC012016F98EBC6A

packaged AX
E:\FrankyRenderer\releases\local-packages-ngu-1.4.4-20261116\
FrankyRenderer-1.4.4-x64\FrankyRenderer.ax
0F5CCCD04E7486333C3522B48F5E4521DF418BB3CED64D294C04F0D5F248C4DC

ZIP
E:\FrankyRenderer\releases\local-packages-ngu-1.4.4-20261116\
FrankyRenderer-1.4.4-x64.zip
11F1E14EC66FC4F3A074D82A56076F07327F7A1BC1496B5BCBEC882843773537
```

## Boundaries

- This release closes the real 4K SDR window/fullscreen chroma-order defect.
- Existing fail-closed boundaries for unobserved HDR systems, unsupported
  quality/geometry pairs, parameter maps and activation ratios remain as
  documented in the OpenMadVR capability matrix.
- The release does not move presentation, tone mapping or arbitrary target
  scaling into NGU.

