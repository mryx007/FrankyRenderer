# FrankyRenderer 1.4.7

Status: local release prepared 2026-11-17.

Tag: annotated `v1.4.7`

This release corrects the remaining blue cast on hardware-decoder 4K sources.

## Change

- The 4K `PL_HOOK_CHROMA_INPUT` adapter now stores the shared source chroma in
  NGU's required Cb,Cr order.
- Final target `value.yx` storage order is unchanged.
- NGU arithmetic, range conversion and 1080p paths are unchanged.

## Verification

```text
4K NGU chroma Low/Medium/High/VeryHigh
  all declared surfaces different=0 maximum=0

test_ngu_backend
  PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K

1.4.7 package DirectShow --ngu
  PASS_PRODUCT ngu_sharp DirectShow RGB48 1280x720 different=0 maximum=0

P010 NGU 1920x1080
  PASS_PRODUCT DirectShow graph different=0 maximum=0

real JUR-285 3840x2160 source
  window and fullscreen warm-skin frame matches the ffmpeg source frame
```

## Artifacts

```text
package
E:\FrankyRenderer\releases\local-packages-ngu-1.4.7-20261117\FrankyRenderer-1.4.7-x64.zip
65494C08255B513A1FCE54110A8E65367D8D134DEF254827FBA9D0633166D8D7

packaged AX
9D7DE15DA41EE25AC3A8E7B91B6CEC38111427A4C94CD4747D3F74505EB37757

source AX
92298A726A9D7D87A9EF7543E6D302F1570EAC05652D5311C387BA474A797C01

ffmpeg source frame at 21 s
E:\FrankyRenderer\work\source_at21.png
0DA380200A391B130DC89300233E806A13FC0EE6813F64F36CDCA9131AB3957B

fixed 4K window
E:\FrankyRenderer\work\ngu_1.4.7_at21_fixed.png
9EB542765AD24120F22D052C61C6FEC582ECB4A5CA179AD1BFE63FDA17BCFFF0

fixed 4K fullscreen
E:\FrankyRenderer\work\ngu_1.4.7_at21_fixed_fullscreen.png
EBB7B412968CFFA14BE64487AA44A071775E3573D3EDD86E14FE940B9077A8B6
```

The packaging command registered this AX for the current Windows user. Both
renderer CLSIDs now point into the 1.4.7 package.
