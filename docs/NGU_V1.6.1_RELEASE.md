# FrankyRenderer 1.6.1

Status: Hotfix release accepted 2026-11-17.

This release fixes the PotPlayer black-screen failure for the supplied
`Sony_4K_HDR_Camp.mp4` HDR fixture. PotPlayer exposes that 10-bit HEVC stream as
8-bit NV12 while retaining PQ/BT.2020 metadata. Franky 1.6.0 rejected the
source depth before entering the NGU chroma hook, which caused the renderer to
drop the frame.

## Change

- Admit native-size 4K HDR 4:2:0 NV12 in addition to P010/P016.
- Run only NGU CbCr scaling at `PL_HOOK_CHROMA_INPUT`; luma, transfer,
  primaries, tone/gamut mapping, metadata and presentation remain owned by
  Franky.
- If NGU preparation fails for an unexpected source, use a visible common
  renderer fallback instead of failing the frame and producing a black screen.
- Preserve the existing P010/P016 DirectShow paths.

## Verification

PotPlayer trace with the supplied file:

```text
NGU HDR admission geometry=3840x2160 target=3840x2160 layout=1 bits=8/8
sys=4 full=0 chromaOnly=1 handoff=0 4k=1
NGU plan probe hdr=1 output=3840x2160
NGU output hook entered
NGU chroma hook returned 3840x2160 RGBA16 texture
```

The player loaded `FrankyRenderer.ax` version `1.6.1` and the captured window
shows the expected HDR scene, not a black frame.

```text
test_ngu_backend
PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K

packaged P010 DirectShow
PASS_PRODUCT ngu_sharp DirectShow P010 3840x2160 graph rendered=34 different=0 maximum=0

packaged P016 CPU-plane DirectShow
PASS_PRODUCT ngu_sharp DirectShow P016 3840x2160 graph rendered=19 different=0 maximum=0

default packaged DirectShow
EXIT 0x0
```

## Artifacts

```text
ZIP
E:\FrankyRenderer\releases\local-packages-ngu-hdr-1.6.1-20261117\FrankyRenderer-1.6.1-x64.zip
C6DAA0777C179C9849C83C59ED14166B7640853DFE5BCD19D4F2204D39689154

packaged AX
E:\FrankyRenderer\releases\local-packages-ngu-hdr-1.6.1-20261117\FrankyRenderer-1.6.1-x64\FrankyRenderer.ax
52422475DD8895925BA2B05413B10B0C6E80D400EE4079E0C7B1DE90AAFE1E44

source AX
E:\FrankyRenderer\src\build\bin\FrankyRenderer.ax
5D3256773E399AE73265CF1E5A5E350220E4F3C7297ACC5922CDFB45767F2884

PotPlayer proof
E:\OpenMadVR\work\sony_4k_hdr_20261117\potplayer_1.6.1_package_window.bmp
AAE0DCF9C8E8010AFFF1916E56F9FBA4FB81AB1FDC712D16832BDE9A587DD975
```

Log hashes:

```text
Backend
E846222073EDB86EADCD1B633BFDE83B6C6FF1C37454D2E612AB7E4DF0FA676A

Packaged default DirectShow
166F45D24433EB7052094DBE07AAE98382C0D689D5BFF9B489085440C51FCAD2

Packaged P010
C6D41ADA8EB56DFCFD43B1078C27367D8CB7B5A46EB5B21E50EDAC5795093599

Packaged P016
B6DC8C946C1D403BD2FBB975C610D722D8130B1A06EED188B136492FE4688EB3

PotPlayer trace
EB2E7D75327690ED92EEDD0E6E99EA97DAD017D429ECD05014FC83861BE92A6D
```

## Boundaries

- The 8-bit NV12 HDR path is a Franky transport path: NGU scales CbCr and does
  not own HDR presentation. It is not a claim of madVR final-frame parity.
- P210/P216 are 4:2:2 10/16-bit transport formats; P210/P216 HDR NGU
  integration and 4K enlargement remain separate unsupported rows.
- Unknown source depths or unsupported NGU mappings now fall back visibly
  instead of blacking out the player.
