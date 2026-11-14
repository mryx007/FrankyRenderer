# FrankyRenderer 1.3.0

Release date: 2026-11-15

This is the first stable release containing the native `ngu_sharp` renderer
peer.

## Highlights

- NGU Sharp VeryHigh with SuperChromaRes strength 3 runs as a normal
  FrankyRenderer scaler choice.
- SDR paths include the observed 720p Double and 1080p/4K graphs, P010/P016
  high-bit Double, NV12/I420 and the recorded P210/P216 class-3 paths.
- HDR source-domain support includes the observed P010 BT.2020/PQ 720p/1080p,
  BT.2020/HLG 1080p and BT.709/PQ 1080p profiles.
- Arbitrary window sizing is handled by the shared Franky renderer through
  `PL_HOOK_RGB`; there is no NGU-specific player or window workaround.
- FrankyRenderer remains the sole owner of HDR display policy, tone mapping,
  metadata application, final scaling and presentation.

## Verification boundary

NGU numerical identity is required through the evidence-backed handoff:
source-resolution `PS_4576` for HDR and pre-presentation `PS_908` for the P016
720p Double path. Output produced by Franky's different downstream HDR and
presentation implementation is not required to match madVR's final BMP
pixel-for-pixel.

All NGU-internal stages, constants, sampling and handoff comparisons remain
strict `different=0 maximum=0` gates where deterministic; declared final SDR
dither remains bounded at `maximum=1` with no pixel above one.

## Residual boundaries

- Low/Medium/High NGU qualities remain fail-closed.
- Hardware `DXGI_FORMAT_P016` import remains host-dependent; CPU P016 is the
  accepted path.
- Unobserved HDR transfer/primaries/range combinations remain fail-closed.
- A complete raw-source oracle for 720p HLG and BT.709/PQ is not yet claimed.
- Real TDR/device-removal injection remains unobserved; replacement-device
  recovery passes.

## Artifacts

```text
source AX
292D42280285555EF9E02CACFBCBA8741E9781B51A392D450EA1C4F06B997416

packaged AX
DBC3ED4BEE02C011980D30DEB0D4A9212D56A99A1D05B0CFB858431187A9E0AE

FrankyRenderer-1.3.0-x64.zip
1AA9ED72A2EFEFDABE6E2F86B9C6FC230C80C668227494A8FCD7DA7B3F5E5DC3
```

## Installation

Close all players, extract the ZIP to a permanent folder and run
`Install.cmd`. Select **FrankyRenderer** in MPC-BE, MPC-HC or a PotPlayer build
with native FrankyRenderer support.
