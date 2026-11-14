# FrankyRenderer 1.4.0

Release date: 2026-11-15

This release adds the evidence-gated Low, Medium and High NGU Sharp chroma
profiles to the native `ngu_sharp` renderer peer.

## Highlights

- The verified all-quality product path is 1280x720 SDR NV12, rendered with
  image NGU Sharp VeryHigh and selectable chroma NGU Sharp Low, Medium, High or
  VeryHigh.
- NGU remains a normal shared-renderer scaler. Input conversion, target sizing,
  crop, colour management, arbitrary-window presentation and player lifecycle
  stay in the common Franky pipeline.
- The previous VeryHigh SDR paths remain available: 720p Double, 1080p/4K,
  P010/P016 high-bit Double and recorded P210/P216 class-3.
- The observed P010 HDR source domains remain supported through source-domain
  `PS_4576`: BT.2020/PQ, BT.2020/HLG and BT.709/PQ.
- All-quality combinations outside the verified 720p SDR NV12 path fail closed.
- Image quality other than NGU Sharp VeryHigh continues to fail closed.

## Verification boundary

The all-quality numerical gate is the deterministic product `PS_4576` handoff.
The same-frame full-frame DirectShow references covered image VeryHigh with
chroma Low, Medium, High and VeryHigh. Every target and source/package product
handoff is byte-exact:

```text
Low      DD58F59B970723DC56152F7BECA1672DE6FCA83E6A50145C2CF344AF2DAFF1D7
Medium   BEE34A71EF86BC015FB945DC17EEEAE6FE65CD050144FCE28B42652982A0FD2E
High     AA25669BEB99AAEB5E005F761B4CDE0C98655D3A712172400E8EEF7361918866
VeryHigh 4D4DFC862BB1F84278FF30A1458DC76D71EE27EBEED0CED1DC99A8D1C9517880
```

The final SDR BMP is recorded as an independent 4K playback/render result, but
is not used for strict equality because the target dither phase is
frame-dependent. The evidence-backed numerical boundary remains `PS_4576`.

## Additional gates

```text
Low/Medium network tests                 PASS_PRODUCT different=0 maximum=0
High network tests                       PASS_PRODUCT different=0 maximum=0
Low/Medium Cb and Cr initializers        PASS_PRODUCT different=0 maximum=0
VeryHigh 720p luma 4x full chain         PASS_PRODUCT maximum=1 diff_gt_1_pixels=0
P016 720p Double PS_908                   PASS_PRODUCT different=0 maximum=0
P010 1080p BT.2020/HLG PS_4576            PASS_PRODUCT different=0 maximum=0
P010 1080p BT.709/PQ PS_4576               PASS_PRODUCT different=0 maximum=0
Source DirectShow GPU/NGU/P016             PASS_PRODUCT
Packaged DirectShow GPU/NGU/P016           PASS_PRODUCT
Source/package all-quality PS_4576         exact for all four chroma qualities
Setup, rollback, uninstall, force-uninstall PASS
```

## Regression fixed

The quality-profile refactor initially stopped initializing the legacy
VeryHigh shader and constant slots retained by the high-bit 720p Double path.
The resulting P016 product graph was exact through `PS_4576` but diverged at
`PS_908`. The legacy slots now alias the same VeryHigh descriptor resources, and
the correlated P016 gate is exact again at `different=0 maximum=0`.

## Artifacts

```text
source FrankyRenderer.ax
C8BCF6487F81F04B71643AC825942C202D5774C01874866A8B6C77D4C223E305

packaged FrankyRenderer.ax
5BA0D181A137B0D13A38D24075DC2EE721D3F6C55E6F29CAFA7E619FA0800633

FrankyRenderer-1.4.0-x64.zip
9E8B5D3677DA459FACB29D2CF8B795B9C09A02DEA56F6F3C592D1841048BCBB0

build-manifest.json
E7686A141A037D8AA8BCF6778EC73259B71CDF023983C18E77968359EA99CD57

acceptance summary
92CBA1EAF8368F70286C41D150148BE20F269B31A9881DB1484FCB8B4CA811A4
```

## Residual boundaries

- Image NGU Sharp Low/Medium/High and all-quality paths beyond the verified
  1280x720 SDR NV12 geometry remain fail-closed.
- Hardware P016 import remains host-dependent; CPU P016 is the accepted path.
- Unobserved HDR combinations and real TDR/device-removal injection remain
  unclaimed.
- Interactive UAC-gated installation into an external player was not run in
  this session. Package-level DirectShow renderer loading, full-frame playback,
  setup ownership, rollback and uninstall gates passed.

## Installation

Close all players, extract the ZIP to a permanent folder and run `Install.cmd`.
Select **FrankyRenderer** in a player build with native FrankyRenderer support.
