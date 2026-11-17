# FrankyRenderer 1.5.1

Status: local release prepared 2026-11-17.

Tag: annotated `v1.5.1`

This release fixes the remaining blue cast in PotPlayer software decoding while
preserving MPC-BE hardware-decoder playback.

## Root cause

The shared 4K chroma path has two order boundaries:

1. `PL_HOOK_CHROMA_INPUT` to the NGU source plane.
2. The NGU FinalStep chroma texture to the common renderer.

CPU planes and hardware decoder surfaces reach these boundaries in different
storage order. Applying one fixed pair of swaps fixed one player and broke the
other.

## Change

- `nguCpuPlanes=true` uses direct source order and direct final order.
- Hardware decoder surfaces use swapped source order and swapped final order.
- Both choices are selected from the existing shared source state; NGU
  arithmetic is unchanged.
- Local packaging still updates HKCU and synchronizes a writable HKLM package
  path for PotPlayer.

## Verification

```text
4K NGU chroma Low/Medium/High/VeryHigh  all surfaces exact
test_ngu_backend                        PASS_PRODUCT
1.5.1 package DirectShow --ngu           PASS_PRODUCT
MPC-BE hardware 4K at 21 s              warm color matches source
PotPlayer S/W 4K at 19-21 s             warm color matches source

HKLM AX FileVersion 1.5.1
HKCU AX FileVersion 1.5.1
```

## Artifacts

```text
package
E:\FrankyRenderer\releases\local-packages-ngu-1.5.1-20261117\FrankyRenderer-1.5.1-x64.zip
263BD1BFFE89C67BE96FD082FC397EBA783E773CC78C7BBFDCBE35E2A434ADD9

packaged AX
30A07BC57ADB67990A9F27762CA3EE2561BC7355B53E05660EDC84F7AA0055CD

source AX
BFCFD6A891E0BC3CA54FCC953AE8FCDBD1EA2984AD566069DB49112A97B22BC8

PotPlayer S/W proof
E:\FrankyRenderer\work\potplayer_1.5.1_at21.png
5D66D36F52377DD19D1523041E1B9BF7DCC96F67728585E60B5C6BF87DBA8D4B

MPC-BE hardware proof
E:\FrankyRenderer\work\mpcbe_dual_order_at21.png
38EC55EE078A7F4CF2B1F9C5C4BA691B90D7D8C9FEA2CCBD5F33F80BD08107B6
```
