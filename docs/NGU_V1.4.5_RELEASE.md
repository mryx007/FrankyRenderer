# FrankyRenderer 1.4.5

Status: stable local release prepared 2026-11-17.

Tag: annotated `v1.4.5`

This release fixes real hardware-decoder 4K SDR playback for native NGU Sharp.
It supersedes `v1.4.4`: the Cb/Cr storage order was already correct in 1.4.4,
but Direct3D hardware-decoder plane views did not reliably supply that surface
to the NGU source adapter.

## Included

- Added a shared `PL_HOOK_CHROMA_INPUT` adapter surface for the admitted 4K
  chroma-only NGU plan.
- Samples the libplacebo-provided `rg8 1920x1080` chroma texture with a
  pipeline-ordered raster adapter and stores it in a canonical
  `B8G8R8A8_UNORM` render target consumed by the existing NGU source view.
- Retains `value.yx` in the target FinalStep storage adapter. The target
  `PS_396` semantic order remains `Cb,Cr`.
- Keeps NGU as a render-hook peer. Source frame replacement, fixed-output
  interception and per-window second-renderer paths are not used.
- Franky continues to own window/fullscreen reduction, color management,
  presentation, HDR metadata and player lifecycle.

## Root cause

The real MPC-BE/LAV decoder delivered an NV12 texture array. The copied
1920x1080 chroma plane was valid, but both legacy plane SRVs and D3D11.3
`PlaneSlice` views sampled neutral or saturated constants in the NGU adapter.
The shared renderer already exposes the correctly converted chroma plane at
`PL_HOOK_CHROMA_INPUT`; 1.4.5 consumes that shared surface instead of creating
another decoder plane view.

## Verification

```text
4K NGU chroma Low      11 surfaces different=0 maximum=0
4K NGU chroma Medium   10 surfaces different=0 maximum=0
4K NGU chroma High     11 surfaces different=0 maximum=0
4K NGU chroma VeryHigh 11 surfaces different=0 maximum=0

test_ngu_backend
  PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K

DirectShow NGU
  PASS_PRODUCT ngu_sharp DirectShow RGB48 1280x720 rendered=5 different=0 maximum=0
  PASS_PRODUCT ngu_sharp DirectShow P010 1920x1080 rendered=17 different=0 maximum=0
  PASS_PRODUCT ngu_sharp DirectShow P010 1920x1080 rendered=18 different=0 maximum=0 (HDR fixture)

packaged AX
  PASS_PRODUCT ngu_sharp DirectShow RGB48 1280x720 rendered=5 different=0 maximum=0
```

Real-file gate:

```text
file: Y:\JUR-285.[4K]@R90s\169bbs.com@JUR-285_[4K].mkv
source: 3840x2160 H.264 yuv420p 8-bit limited BT.709 SDR
decoder path: MPC-BE 1.9.3 x64, LAV hardware decoder
loaded AX: package FrankyRenderer.ax, FileVersion 1.4.5
trace: canonical NGU source -> NGU chroma hook returned 3840x2160 RGBA16
window: visible, correct color
fullscreen: visible, correct color
playback: 600 presented frames per 10 s, queued 0/0/0/0
steady GPU peak: approximately 12.2-12.7 ms per 60 Hz frame
```

## Artifacts

```text
package
E:\FrankyRenderer\releases\local-packages-ngu-1.4.5-20261117\FrankyRenderer-1.4.5-x64.zip
4DBE660344510960F6ADDF308FDE51B5191B027639AD015DBACC1F02DA256B6D

packaged AX
E:\FrankyRenderer\releases\local-packages-ngu-1.4.5-20261117\FrankyRenderer-1.4.5-x64\FrankyRenderer.ax
B443C2C5E4612CE04683A374A248877B4C6530F8745CCB188752748BA00B672A

source AX
E:\FrankyRenderer\src\build\bin\FrankyRenderer.ax
2B7EFD08E50A6C39680CA44A6E0ACABD75F723E1A6E7CA78F3ADAAF67EDE1B19

source frame at 19 s
E:\FrankyRenderer\work\source_at19.png
7BDC40D5AEBA0A82848674F6F507CB089426481E8360C2D19AE92CCC226E1DD7

source AX 4K window
E:\FrankyRenderer\work\final_4k_ngu_window_20261116.png
697EEBAF818D727ED924C3480D7F2703D479FBBDC2E5E48F8BE7008CC95A0A0F

source AX 4K fullscreen
E:\FrankyRenderer\work\final_4k_ngu_fullscreen_20261116.png
46B3C20FBF3731F7775EA8B84E0735C162E254B330BBC3255F5823EB1063DCFD

package AX 4K window
E:\FrankyRenderer\work\package_1.4.5_jur285_window.png
E4DED2AFFFD8211057CD5CD754216A06EC2E20E8E4D1FA576375DFA0E23F1843

package AX 4K fullscreen
E:\FrankyRenderer\work\package_1.4.5_jur285_fullscreen.png
068C435DFFA4F5485215FA3E2B62858D87737942E8BAA1D9E9B6B79C90F50A34
```

## Boundaries

- Only NGU Sharp is in scope. Other NGU families and unobserved parameter
  mappings remain outside this release.
- The admitted 4K path is the observed SDR 4:2:0 chroma-only graph. Other 4K
  formats, HDR systems and enlargement above source resolution remain
  fail-closed.
- A system-wide HKLM installation requires the normal administrator approval
  in `Install.cmd`. The package itself was additionally validated through a
  per-user CLSID registration and direct `--dll` package tests.
