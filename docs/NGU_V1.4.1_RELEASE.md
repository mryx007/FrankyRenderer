# FrankyRenderer 1.4.1

Release date: 2026-11-15

This patch preserves the 1.4.0 NGU quality matrix and fixes two player-facing
integration defects.

## Highlights

- A 3840x2160 source with `ngu_sharp` selected no longer becomes a black frame
  when NGU enlargement is not required. An unsupported native-source geometry
  at or below presentation size returns to the common Franky renderer.
- The common fallback uses the original `pl_frame`, restores configured
  scaling and dither parameters, and does not create a fixed-format 4K
  renderer or a 4K NGU graph.
- Native NGU is removed from the Custom Filter starting-preset choices.
  Attempts to convert `ngu_sharp` into a libplacebo custom preset are rejected
  without changing settings.
- NGU remains configured in the native scaler area. Anti-Ringing is not moved
  into Custom Filters or silently applied to NGU.

## Verification

```text
Backend native 4K fallback           PASS; configured dither restored; nonblack
DirectShow 3840x2160 -> 1920x1080    ReadPixels S_OK; 2,073,600/2,073,600 nonblack
DirectShow 3840x2160 -> 3840x2160    ReadPixels S_OK; 8,294,400/8,294,400 nonblack
Packaged native 4K                   ReadPixels S_OK; same full-frame SHA-256
Direct2D model                       PASS; native-to-custom conversion rejected
Direct2D page                        PASS
Source/package NGU Low/Medium/High   PS_4576 different=0 maximum=0
Source/package NGU VeryHigh          PS_4576 different=0 maximum=0
Packaged GPU/NGU/HDR/P016            PASS_PRODUCT
Setup/rollback/uninstall             PASS
```

## Anti-Ringing boundary

E-005, E-017 and E-021 show target anti-ringing selection in the NGU image
upscaling setting area. The exact mapping from an NGU anti-ringing strength to
its selected shader flag and constants is still unknown. No NGU-specific
Anti-Ringing control is added in this release; the existing global Anti-Ringing
continues to belong to the common renderer.

## Artifacts

```text
source FrankyRenderer.ax
A6F3BE0249AB106F7F688B9E66AC72CF71CBC800301AD0F32D68279A661D51E6

packaged FrankyRenderer.ax
93DB7FD7A7ABE57EDEFF97527EB50D48A9CE9A42F700DFA721B4F4238D2A29A8

FrankyRenderer-1.4.1-x64.zip
FE1400FFC185858480BC56FD4F60EA384675EE5D470844C5E6DA7A0B8921587E

build-manifest.json
43233AEF4577E32C1B0E3727C8F9D221AD82DEC8A23412C8F9AFB3407B0287EE
```

## Residual boundaries

- Exact target mapping for NGU Anti-Ringing strength remains unknown and is not
  exposed.
- All 1.4.0 quality, HDR, P016 and class-3 numerical boundaries are unchanged.
- Unsupported NGU source geometries that actually require enlargement remain
  fail-closed.
