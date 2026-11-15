# FrankyRenderer 1.4.2

Status: stable local release prepared 2026-11-15.

This release completes Image NGU Sharp Low, Medium and High for the observed
1280x720 SDR 4x path and exposes independent Image and Chroma quality selectors.
VeryHigh remains the admitted quality for the existing 1080p, HDR and
P016/P010 Double paths.

## Included

- Target-captured Image Low/Medium/High network assets, constants and tails.
- Exact Low/Medium/High 720p pass surfaces: `different=0 maximum=0`.
- Independent `NGU Quality: Image` and `NGU Quality: Chroma` UI controls.
- Owned-dither finalization for the Image quality product path.
- Common-renderer fallback for unsupported non-VeryHigh quality geometries.
- Existing 4K native-size black-frame fallback and Custom Filter rejection.

## Verification

```text
focused:
  Low/Medium/High retained surfaces different=0 maximum=0

source AX DirectShow:
  low/Low      maximum=73 diff_gt_1_pixels=2047110
  med/Medium   maximum=71 diff_gt_1_pixels=2086765
  high/High    maximum=76 diff_gt_1_pixels=2217678
  veryHigh     PS_4576 different=0 maximum=0

package AX DirectShow:
  low/Low      maximum=72 diff_gt_1_pixels=2049469
  med/Medium   maximum=71 diff_gt_1_pixels=2086843
  high/High    maximum=76 diff_gt_1_pixels=2223125
  veryHigh     PS_4576 different=0 maximum=0

backend:
  PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K
  native 4K common fallback nonblack

UI:
  Direct2D model 350 controls PASS
```

The final-frame maxima are the existing target-vs-Franky presentation delta.
Native NGU-internal and declared handoff comparisons remain strict.

## Artifacts

```text
source AX
E:\FrankyRenderer\src\build\bin\FrankyRenderer.ax
82DCEE8CFF8B0D01400A3D4B3E6B2C8C3E8AA529064ED7AD2121E6CB3D6C022D

package directory
E:\FrankyRenderer\releases\local-packages-ngu-all-quality-20261115-v3\
FrankyRenderer-1.4.2-x64

packaged AX
9780E3098C09CD58FC7ED80084655A70E679755E278137F5A0BF36D180568594

ZIP
E:\FrankyRenderer\releases\local-packages-ngu-all-quality-20261115-v3\
FrankyRenderer-1.4.2-x64.zip
31b221343adb3e9cd42d8e566d70d3bd193b3d7276358efe317b36f9db217ab5
```

## Boundaries

- Image Low/Medium/High are closed only for 1280x720 SDR 4x.
- 1080-to-4K, 720 Double, HDR, arbitrary ratios and P210/P216 Image
  Low/Medium/High remain fail-closed.
- NGU-specific anti-ringing is not exposed because its target mapping is not
  evidence-closed.
- Existing HKLM registration of 1.4.1 prevented an unattended package
  reinstall without interactive elevation. The packaged AX was still tested
  directly through DirectShow.

Evidence: OpenMadVR `E-176`, `STEP-166`.
