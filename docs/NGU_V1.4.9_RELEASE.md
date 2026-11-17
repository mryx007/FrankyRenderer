# FrankyRenderer 1.4.9

Status: local release prepared 2026-11-17.

Tag: annotated `v1.4.9`

This release makes PotPlayer consume the same latest local package as MPC-BE.

## Change

- Per-user packaging still writes the current HKCU CLSIDs.
- If HKLM points to an older writable development package, registration now
  copies the new AX, runtime DLLs and shaders into that machine package.
- The copied AX hash is verified before the synchronization is reported as
  successful.
- PotPlayer therefore loads the new build even though its selected renderer is
  registered through the machine-level CLSID.

## Verification

```text
1.4.9 package DirectShow --ngu
  PASS_PRODUCT ngu_sharp DirectShow RGB48 1280x720 different=0 maximum=0

setup test
  PASS native setup

machine registration target
  E:\FrankyRenderer\releases\local-packages-ngu-1.4.6-20261117\
    FrankyRenderer-1.4.6-x64\FrankyRenderer.ax
  FileVersion 1.4.9
  hash equals the 1.4.9 packaged AX

current-user registration target
  E:\FrankyRenderer\releases\local-packages-ngu-1.4.9-20261117\
    FrankyRenderer-1.4.9-x64\FrankyRenderer.ax
  FileVersion 1.4.9
```

## Artifacts

```text
package
E:\FrankyRenderer\releases\local-packages-ngu-1.4.9-20261117\FrankyRenderer-1.4.9-x64.zip
88288DDBCC05324D035B007C6BC328B367C551BBA1A02D6AD084907CF0FE1A56

packaged AX
CB94561DADD136D4BF9A1D9F16642237E9774C45ABB229B859AE78DAFAE75303

source AX
B9E2F566CEC51DD8AA88F2B6B3B768D1653E41FFD1D109F9443580C739CECDA2
```

## Boundary

HKLM registry paths that are not writable still require the normal
administrator `Install.cmd` flow. The automatic synchronization is intended for
the local development package directories directly under
`E:\FrankyRenderer\releases`.
