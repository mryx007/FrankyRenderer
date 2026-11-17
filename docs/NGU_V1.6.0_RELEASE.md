# FrankyRenderer 1.6.0

Status: final local release accepted 2026-11-17.

Tag: annotated `v1.6.0`

This release makes NGU work normally inside Franky's HDR framework. NGU remains
a shared-hook native scaler peer; Franky remains the sole owner of HDR range,
transfer/primaries, tone/gamut mapping, peak metadata, output encoding, dither
and presentation. Franky's HDR image is not required to match madVR.

## Change

- Removed the blanket common-renderer fallback for 4K HDR sources.
- Admitted limited-range 4K 4:2:0 P010/P016 HDR through
  `PL_HOOK_CHROMA_INPUT`.
- NGU expands CbCr at source resolution; the common renderer retains luma,
  presentation scaling, HDR processing and final output.
- HDR metadata and hook reporting are preserved in diagnostics and `Ctrl+J`.
- Version advanced to `1.6.0`.
- `test_filter` now sends the supplied 4K P010/P016 fixture through the
  DirectShow source. It no longer substitutes the synthetic gradient used by
  the ordinary NGU smoke test.

## Verification

```text
test_ngu_backend
PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K

packaged DirectShow 4K HDR P010
PASS_PRODUCT NGU 4K source produced a visible 1080 window
PASS_PRODUCT NGU window-to-fullscreen transition is visible
PASS_PRODUCT P010 NGU 3840x2160 pause, flush, restart and reconnect
PASS_PRODUCT ngu_sharp DirectShow P010 3840x2160 graph rendered=50 different=0 maximum=0

packaged DirectShow 4K HDR P016
PASS_PRODUCT NGU 4K source produced a visible 1080 window
PASS_PRODUCT NGU window-to-fullscreen transition is visible
PASS_PRODUCT ngu_sharp DirectShow P016 3840x2160 graph rendered=19 different=0 maximum=0

default packaged DirectShow
EXIT 0x0
```

The real `Sony_4K_HDR_Camp.mp4` fixture was opened through the packaged Franky
DirectShow client at 1920x1080 and 3840x2160. The trace confirms `hdr=1`,
PQ/BT.2020, `PL_HOOK_CHROMA_INPUT` and active NGU on every rendered frame. Both
captures show the expected scene with normal color. PotPlayer loaded the final
packaged AX at version 1.6.0. Desktop BitBlt of PotPlayer's hardware-HDR surface
is black on this host, so that capture method is not used as product evidence.

The host GPU rejects a D3D11 P016 decoder texture with `E_INVALIDARG`; P016 is
therefore verified through the packed CPU-plane DirectShow path. P010 hardware
decoder and packaged playback are verified.

## Artifacts

```text
package
E:\FrankyRenderer\releases\local-packages-ngu-hdr-1.6.0-release\FrankyRenderer-1.6.0-x64.zip
FF47C419B2B760A9A0C85F2C6F58EFA06F11562C3A195621E9404E9555A627B9

packaged AX
E:\FrankyRenderer\releases\local-packages-ngu-hdr-1.6.0-release\FrankyRenderer-1.6.0-x64\FrankyRenderer.ax
F349556D7D0FF0BEAC5E5D89914F8AD0B324C47576FF4973568DF3B08B166446

source AX
E:\FrankyRenderer\src\build\bin\FrankyRenderer.ax
196F595AC9355A57DA91B44045DF213E7CBA2C2A12A45A7EA4A5AD1789C76BEC

real-file window proof
E:\OpenMadVR\work\sony_4k_hdr_20261117\playgrab_current_window.bmp
C3B407B229FF6DDE93AEFDE1AAB03D462BFC46300822978E56205FB461B55C5F

real-file 4K proof
E:\OpenMadVR\work\sony_4k_hdr_20261117\playgrab_current_fullscreen.bmp
360F9F715FFE89B9B084EC2EE9234B6FE9D98F310C7204E38A045C85D0EB123B

Backend log
E:\FrankyRenderer\work\test_ngu_backend_1.6.0_source_final_20261117.log
457F7EEDE265E19604F42B444EBCA17653E91BF8E456AEA851173F8C62A5BFC6

packaged P010 DirectShow log
E:\FrankyRenderer\work\p010_fixture_package_1.6.0_20261117.log
B27AAFEB3C67FB246738E238FEC66C98028714E32DE4418EF35C6379F0D6CD9F

packaged P016 CPU-plane DirectShow log
E:\FrankyRenderer\work\p016_fixture_cpu_package_1.6.0_20261117.log
B6DC8C946C1D403BD2FBB975C610D722D8130B1A06EED188B136492FE4688EB3

packaged default DirectShow log
E:\FrankyRenderer\work\run_tests_package_1.6.0_final_20261117.log
B0C2287C90AE0869A5A5CF4753A1824B3DD3D780995F5535D9BDD569294CD7BE

real-file window trace
E:\OpenMadVR\work\playgrab_current_trace_window_20261117.log
1F0F462EAECC4212E08DA9FF70805A4AB1F4DBB5CF9BB87818B769454711A47B

real-file fullscreen log
E:\OpenMadVR\work\playgrab_current_fullscreen_20261117.log
DF62D15458A68C13430C44638076B59FBF7B581A582F8E7037A89B84E371AF04
```

## Boundaries

- Only the declared NGU HDR rows are covered. P210/P216 HDR, enlargement
  above 4K, additional NGU qualities/parameters and unobserved HDR systems
  remain explicit fallback or unsupported rows.
- P016 hardware decoder textures are rejected by this host's D3D11 device;
  P016 product coverage is the CPU-plane DirectShow path.
- MadVR's HDR presentation curves and final-frame output are not parity gates.
