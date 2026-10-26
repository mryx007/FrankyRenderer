# FrankyRenderer

> **Developer test build — native CLSID integration.** For player developers testing native FrankyRenderer integration. Not intended for general use.

I've been using the madVR renderer for many years now. Unfortunately, updates have become quite rare, with what feels like only one update per year.

Meanwhile, MPC Video Renderer is still being actively developed, and libplacebo, which also continues to receive regular updates, has built a very good reputation for its image processing capabilities.

So I thought: Why not take the core functionality of MPC Video Renderer, build my own renderer around it, and let libplacebo handle the image processing?

And that's exactly what I did.

I decided to call my renderer Franky, inspired by Frankenstein, who pieced together different body parts to create something entirely new. Since my renderer is also built by combining different components into something new, I thought the name was quite fitting.

It's still a work in progress, so there are probably quite a few bugs I haven't discovered yet, especially considering the number of available settings.

This developer preview uses only FrankyRenderer's dedicated renderer CLSID: `{B864E92A-1BDF-4DA1-9004-CBF467DF239A}`. The MPC Video Renderer compatibility registration is not provided. Native player integration is required.

Feel free to give it a try and report any bugs you encounter. Feedback is always welcome!

## Screenshots

General and presentation settings:

[<img src="docs/screenshots/general.png" width="640" alt="FrankyRenderer General and Presentation settings">](docs/screenshots/general.png)

Scaling and frame mixing:

[<img src="docs/screenshots/scaling.png" width="640" alt="FrankyRenderer Scaling and Frame Mixing settings">](docs/screenshots/scaling.png)

Color Adjustment:

[<img src="docs/screenshots/color-adjustment.png" width="640" alt="FrankyRenderer Color Adjustment settings">](docs/screenshots/color-adjustment.png)

HDR and Color Mapping:

[<img src="docs/screenshots/hdr-color-mapping.png" width="640" alt="FrankyRenderer HDR and Color Mapping settings">](docs/screenshots/hdr-color-mapping.png)

Color Management — ICC profiles and 3D LUTs:

[<img src="docs/screenshots/color-management.png" width="640" alt="FrankyRenderer Color Management settings">](docs/screenshots/color-management.png)

Shader Chain:

[<img src="docs/screenshots/shaders.png" width="640" alt="FrankyRenderer Shader Chain settings">](docs/screenshots/shaders.png)

The screenshots show example settings, not recommended presets.

## Features

### Video input and output

- DirectShow video rendering for Windows x64, with libplacebo image processing and Direct3D 11 / DXGI presentation.
- Native D3D11 decoder textures and CPU video input, with the active transfer path shown in the statistics overlay.
- Support for 8-, 10-, 12- and 16-bit input layouts, including NV12, P010/P012/P016, planar YUV, packed YUV, RGB and grayscale formats. Native GPU input depends on the decoder's texture format and device capabilities.
- Selectable output texture format: automatic, 8-bit integer, preferred 10-bit integer or preferred 16-bit float.
- Configurable RGB output levels, dither target bit depth and maximum frames in flight.
- D3D11 subtitle composition through compatible host callbacks.

### Scaling and Smooth Motion

- Separate image upscaling, image downscaling, chroma/alpha upscaling and chroma/alpha downscaling filters.
- EWA Lanczos / Jinc, EWA Lanczos Sharp, Lanczos, Spline16/36/64, Mitchell, Bicubic, Robidoux and additional libplacebo filters.
- Custom filter editor with a starting filter, kernel, window, radius, clamp, blur, taper, anti-ringing, kernel/window parameters and EWA mode.
- Global anti-ringing strength and configurable downscaling anti-aliasing behavior.
- Smooth Motion through temporal frame mixing, with selectable and customizable frame mixer filters.
- Optional NVIDIA RTX Video Super Resolution through the driver, without an additional NVIDIA runtime DLL or helper process. Enable Super Resolution and choose quality in the NVIDIA app or Control Panel. Processed source images are cached before Smooth Motion; subtitles and final color processing follow afterward. Supports SDR and HDR10/PQ with 8-/10-bit processing; unsupported formats and native-size/downscaled output keep normal libplacebo scaling. Statistics report the driver request, not independently confirmed AI activity. Dolby Vision, HLG, linear, rotated and alpha-bearing input use normal scaling.

### Image processing

- Debanding with adjustable iterations, threshold, radius and grain.
- Adaptive Sharpen with adjustable strength.
- Bwdif and Yadif deinterlacing with temporal references, automatic field order and manual top/bottom-field overrides.
- Blue-noise, ordered and random dithering, with optional temporal dithering.

### HDR and color mapping

- Two HDR modes: Passthrough HDR to Display and Tone Map HDR to SDR.
- Native NVIDIA HDR10 passthrough sends HDR metadata to the display while leaving the Windows HDR setting off. Moving the video between displays updates the HDR output, including while paused, and restores the previous display's state. This driver path has been tested on NVIDIA hardware; AMD native HDR output is not implemented.
- A Clipping Protection switch for HDR-to-SDR tone mapping.
- Tone mapping functions including Spline, BT.2390, BT.2446A, Reinhard, Mobius, Hable and ST 2094 curves.
- Adjustable SDR target brightness and HDR peak override.
- HDR peak detection with smoothing, scene-change thresholds, percentile selection, black cutoff and delayed peak detection control.
- Detailed tone curve controls for knee adaptation, Spline slope/contrast and other function-specific parameters.
- Local contrast recovery with adjustable strength and smoothness.
- Perceptual, relative/absolute colorimetric, softclip, saturation and additional gamut mapping methods.
- Gamut compression controls, internal LUT resolution and optional tricubic gamut interpolation.
- Separate HDR Shadows, Midtones and Highlights controls, each with exposure, contrast and saturation adjustments.
- Inverse tone mapping, gamut expansion, metadata selection and clipping/LUT visualization controls.

### Color adjustment and management

- Brightness, contrast, saturation, gamma, hue and color temperature controls.
- SDR ICC profile support with selectable rendering intent.
- Custom `.cube` LUT loading and LUT interpretation controls.

### Shader chain

- 21 bundled mpv-compatible GLSL shaders from the PlayKit collection, with original authorship and license notices preserved.
- Neural/adaptive upscaling options: FSRCNNX 2x, AiUpscale Photo 2x and RAVU Zoom.
- Animation-oriented upscaling: Anime4K CNN 2x M, ArtCNN C4F16 and CuNNy 2x12 DS.
- Chroma reconstruction: KrigBilateral and CfL Prediction.
- Sharpening, detail and local contrast: AMD CAS, Guided Detail Enhance and Clarity.
- Edge cleanup: FineDehalo, Anti-Ringing and PixelClipper.
- Denoising and debanding: NLMeans, Bilateral Denoise, Chroma Denoise, hdeband and neo-f3kdb.
- Detail-aware downscaling: SSimDownscaler and DPID.
- Per-shader parameters, configurable processing order and double-click enable/disable controls.

### Black bars and zoom

- Automatic detection of hardcoded black bars from video pixels.
- Crop and zoom controls to fit active content or fill the output.
- Adjustable detection threshold, switching delay and stable zoom behavior.
- Protection for detected subtitle content in the black bars.

### Settings, presentation and diagnostics

- Native dark settings interface drawn with Direct2D and DirectWrite, with English sections for General, Processing, Scaling, Black Bars / Zoom, Color Adjustment, Color Management, HDR and Color Mapping, and Shaders.
- Search across settings, scrollable pages, readable descriptions and explicit default values and ranges.
- Compact shader list with double-click enable/disable and a parameter editor for the selected shader.
- Live settings preview: valid changes appear immediately; OK keeps them and Cancel restores the dialog's opening values.
- Separate settings for each display, with editable device names. Newly connected displays start with default settings; reconnecting a known display restores its saved profile.
- Import and export all saved display profiles in one INI backup, including disconnected devices and custom names. Older single-profile INI files can still be imported into the selected display.
- Numeric entry, fine adjustment steps, reset controls and contextual descriptions.
- Configurable background color, transparency and rendering quality/performance options.
- PotPlayer fullscreen playlist-transition handling.
- Ctrl+J statistics with display/video information, active processing settings, frame counters, presentation timing and CPU/GPU render timings.
- Persistent shader cache for reusing compiled processing work across playback sessions.

Some HDR metadata is decoder-dependent. Missing metadata is not reconstructed from filenames. Dolby Vision support relies on metadata provided by the decoder; this is not Dolby Vision HDMI tunneling or FEL reconstruction.

## Developer integration build

This preview is for native player integration testing on Windows x64, tested on Windows 11.
It contains `FrankyRenderer.ax`, runtime libraries, shaders and registration tools.
It requires a player build that explicitly loads FrankyRenderer's dedicated CLSID.
It may not appear in the renderer selection of unmodified player releases.

The original MPC Video Renderer can remain registered separately. Another
FrankyRenderer installation using the same dedicated CLSID cannot be registered
alongside this preview. Registration tools refuse to overwrite another installation.

Build locally with `python src/package.py`; the complete package is written to `dist`.

Use **Export** in the settings menu to save all display profiles in one INI backup and **Import** to restore them. Disconnected displays, custom names, all menu settings and shader parameters/order are included. Existing profiles absent from the backup are kept. Older single-profile INI files are imported into the selected display. External ICC and LUT files are referenced by path and must be kept separately.

This preview never registers under the MPC Video Renderer CLSID. Existing MPCVR and madVR registrations are separate.

Do not move or delete the installed package directory. Uninstall using `Uninstall.cmd` or `setup.ps1 -Action Remove`.

## Source code and dependency sources

The renderer source and build instructions are available in this repository. [Dependency sources](docs/dependency-sources.md) lists the exact official source packages, build recipes and SHA-256 checksums corresponding to the bundled runtime libraries.

## Project layout

- `src` - all build sources, shaders, tests and scripts; required DirectShow compatibility code is included under `src/DirectShow` with its original notices.
- `dist` - locally generated installable package, excluded from Git.
- `LICENSE` and `THIRD_PARTY.md` - licensing and attribution.

## Build

Requirements: Windows x64, Visual Studio 2022 or later with Desktop development with C++, a Windows SDK containing `fxc.exe`, and **CPython 3.14 x64**.

From the repository root:

```powershell
python -m pip install -r requirements.txt
python src/fetch_dependencies.py
python src/prepare_sdk.py
.\build.cmd
python src/package.py
```

Runtime dependencies are restored locally from official MSYS2 packages using the checked-in SHA256 lock file. libplacebo is built from pinned upstream commit `275f7dea27924cdd34ab2e6c20e448b6bc4fab65` (7.387.0 release), with hash-verified local compiler packages and Python build tools. The first dependency restore also compiles libplacebo; later restores reuse its verified build cache. Build from a path without spaces, or with Windows 8.3 filenames enabled. The scripts locate Visual Studio through `vswhere`; no personal SDK paths are required. Build output is `src/build/bin/FrankyRenderer.ax` and its adjacent runtime DLLs and shaders.

All build sources are under `src`, including the required compatibility interfaces and DirectShow BaseClasses in `src/DirectShow`. FrankyRenderer uses its own libplacebo/D3D11 rendering path.

## Code formatting

The project's C++ sources use the checked-in `.clang-format` configuration with four-space indentation and expanded control flow. Third-party sources under `src/DirectShow` retain their upstream formatting.

```powershell
python -m pip install -r requirements-dev.txt
python src/format_code.py
python src/format_code.py --check
```

The formatting script uses clang-format 21.1.8 and only processes tracked C++ files directly under `src`.

## License and credits

FrankyRenderer is distributed under **GPL-3.0-or-later**, due to its MPC Video Renderer code reuse. See [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md). Existing copyright notices and individual shader licenses are preserved. FrankyRenderer is an independent project and is not an official MPC Video Renderer, libplacebo, mpv or PotPlayer release.

Image processing is powered by [libplacebo](https://github.com/haasn/libplacebo), the rendering library also used by mpv. It provides scaling, HDR tone mapping, color management and other image processing features.
