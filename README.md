# FrankyRenderer

A standalone Windows x64 DirectShow video renderer, based on MPC Video Renderer components, using **libplacebo** for image processing and **Direct3D 11 / DXGI** for presentation.

FrankyRenderer is a development project. Synthetic GPU and DirectShow regression tests cover its main paths; compatibility and image quality still depend on the player, decoder, graphics driver and display.

## Features

- Native D3D11 decoder textures and CPU video input.
- Image and chroma scaling, custom filter configuration and temporal frame mixing.
- Debanding, dithering, Bwdif / Yadif deinterlacing and sharpening.
- Color adjustment, SDR ICC profiles and `.cube` LUTs.
- HDR passthrough and HDR-to-SDR tone mapping, gamut mapping and peak detection.
- Separate HDR Shadows, Midtones and Highlights controls.
- An adjustable mpv-compatible shader chain, including bundled PlayKit shaders with their original notices.
- Experimental black bar detection and zoom controls.
- Live settings preview: valid changes appear immediately; OK keeps them and Cancel restores the dialog's opening values.
- Ctrl+J statistics and a persistent shader cache.

Some HDR metadata is decoder-dependent. Missing metadata is not reconstructed from filenames. Dolby Vision support relies on metadata provided by the decoder; this is not Dolby Vision HDMI tunneling or FEL reconstruction.

## PotPlayer installation

After building, run `python src/package.py`. The complete installable package is created in `dist`. Its displayed filter name is **FrankyRenderer**; `PlaceboRenderer.ax` remains the internal binary filename for compatibility.

1. Keep the `dist` directory at a permanent location and close PotPlayer.
2. Run `Installieren.cmd` inside `dist`, or run `.\setup.ps1 -Action Install` from that directory. Windows requests administrator permission for COM registration.
3. In PotPlayer, select **MPC Video Renderer** under Video Renderer. This is PotPlayer's fixed menu label; the loaded filter is FrankyRenderer.
4. Open the filter's settings to configure it.

FrankyRenderer currently uses the MPC Video Renderer CLSID for PotPlayer discovery. It cannot occupy that registration alongside the original MPC Video Renderer. The installer refuses to overwrite a registration pointing elsewhere. Existing madVR registration is separate.

Do not move or delete the installed `dist` directory. Uninstall using its removal launcher or `setup.ps1 -Action Remove`.

## Project layout

- `src` - all build sources, shaders, tests and scripts; required DirectShow compatibility code is included under `src/DirectShow` with its original notices.
- `dist` - locally generated installable package, excluded from Git.
- `LICENSE` and `THIRD_PARTY.md` - licensing and attribution.

## Build

Requirements: Windows x64, Visual Studio 2022 or later with Desktop development with C++, a Windows SDK containing `fxc.exe`, and **Python 3.14+**.

From the repository root:

```powershell
python -m pip install -r requirements.txt
python src/fetch_dependencies.py
python src/prepare_sdk.py
.\build.cmd
python src/package.py
```

Dependencies are restored locally from official MSYS2 packages using the checked-in SHA256 lock file. The scripts locate Visual Studio through `vswhere`; no personal SDK paths are required. Build output is `src/build/bin/PlaceboRenderer.ax` and its adjacent runtime DLLs and shaders.

All build sources are under `src`, including the required compatibility interfaces and DirectShow BaseClasses in `src/DirectShow`. FrankyRenderer uses its own libplacebo/D3D11 rendering path.

## Tests

```powershell
.\src\build_tests.cmd
python src/run_tests.py
python src/run_tests.py --live-ui
.\src\build_sdr_target.cmd
python src/run_tests.py --sdr-target
.\src\build_sdr_present.cmd
python src/run_tests.py --sdr-present
.\src\build_tonal.cmd
python src/run_tests.py --tonal
```

Tests use disposable settings roots and do not inherit the normal user's Auto HDR setting. The default test covers DirectShow, menus and GPU readback. `--live-ui` separately exercises immediate changes and Cancel/OK behavior. Targeted tests cover the SDR luminance boundary, the actual 10-bit presentation signal across 203 nits, and HDR tonal controls. These tests do not replace real playback and display calibration.

The settings and shader-cache storage names from the earlier development build are retained, so upgrading does not discard existing settings.

## License and credits

FrankyRenderer is distributed under **GPL-3.0-or-later**, due to its MPC Video Renderer code reuse. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md). Existing copyright notices and individual shader licenses are preserved. FrankyRenderer is an independent project and is not an official MPC Video Renderer, libplacebo, mpv or PotPlayer release.
