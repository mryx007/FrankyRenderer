# FrankyRenderer 1.4.3

Status: stable local release prepared 2026-11-16.

This release corrects NGU option and statistics presentation without changing
the verified NGU rendering graphs.

## Fixes

- Removed stale `SDR + P010 1080p PQ` capability text from the native scaler
  option label.
- `Ctrl+J` now shows the selected NGU quality for Image and Chroma:
  `NGU Sharp Low/Medium/High/VeryHigh`.
- When NGU is inactive because the source is already at presentation size,
  statistics still show the actual placeholder filter and identify the fallback:
  `nearest (NGU Sharp <quality> inactive; common renderer fallback)`.

The `nearest` value for a native-size 4K source is therefore real filter state,
not a display typo. The common renderer fallback currently uses the native
scaler placeholder filter.

## Verification

```text
test_ngu_backend       PASS_PRODUCT
active statistics      NGU Sharp VeryHigh (native hook)
4K fallback statistics nearest ... NGU Sharp VeryHigh inactive; common renderer fallback
Direct2D model         PASS, 350 controls
NGU focused surfaces   unchanged, all different=0 maximum=0
packaged DirectShow    Low/Low PASS, maximum=72
```

## Artifacts

```text
source AX
E:\FrankyRenderer\src\build\bin\FrankyRenderer.ax
0ED57318FCFBFBCF21177D37D7CB2EC934CC895DB65DA6C88ABAD4BE8A0A93D8

packaged AX
E:\FrankyRenderer\releases\local-packages-ngu-1.4.3-20261116\
FrankyRenderer-1.4.3-x64\FrankyRenderer.ax
74E3BB32B1A34BEB213A86C023A461C55D07D2E94390137D4351F50C15B92FAD

ZIP
E:\FrankyRenderer\releases\local-packages-ngu-1.4.3-20261116\
FrankyRenderer-1.4.3-x64.zip
C65B899A511AE81D25A011E099CBFEC62E23C68E9B9AB279579DFA0106AE705E
```

## Boundaries

This release changes presentation only. NGU arithmetic, pass graphs, quality
admission and HDR ownership are unchanged from 1.4.2.
