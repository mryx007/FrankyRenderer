# FrankyRenderer 1.4.6

Status: local release prepared 2026-11-17.

Tag: annotated `v1.4.6`

This release keeps the 1.4.5 hardware-decoder NGU chroma fix and adds local
test-package automation.

## Included

- `Ctrl+J` now ends with the running FrankyRenderer version.
- `package_release.py` registers each newly built package for the current
  Windows user by default, replacing the previous per-user CLSID paths.
- CI uses `--no-register` so GitHub build agents remain stateless.
- The renderer, resource metadata and Python packages read the version from
  `src/Renderer/Version.h`.

## Verification

```text
test_ngu_backend
  PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K
  statistics end with Version 1.4.6

run_tests.py --dll <1.4.6 package AX> --ngu
  PASS_PRODUCT ngu_sharp DirectShow RGB48 1280x720 rendered=5 different=0 maximum=0

real MPC-BE playback
  loaded package AX FileVersion 1.4.6
  Ctrl+J fullscreen shows "Version 1.4.6" as the final row
```

## Artifacts

```text
package
E:\FrankyRenderer\releases\local-packages-ngu-1.4.6-20261117\FrankyRenderer-1.4.6-x64.zip
59766BFB72D12CEBB7A2FA20DCFDE309B4085DC8B156254400073A5EDF698168

packaged AX
60CD75FCE0DA1FB24163C4BE3060A508E8708214561175C19C728D7BA7BC3F89

source AX
47708BEFC46EEA5EE114A6EE4F69D3C7DFE70BFF02BE4584E4ECA0EBE555B8E2

Ctrl+J fullscreen proof
E:\FrankyRenderer\work\ctrl_j_version_1.4.6_fullscreen_20261117.png
01AB9C579B79E72A66B8EF66C90458943A20D7E22E75809B4035E566ACB75C0D
```

## Registration

After packaging, both per-user renderer CLSIDs point into:

```text
E:\FrankyRenderer\releases\local-packages-ngu-1.4.6-20261117\
  FrankyRenderer-1.4.6-x64\FrankyRenderer.ax
```

The normal system-wide installer remains available through `Install.cmd` when
administrator registration is desired.
