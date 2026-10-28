# Third-party components

- **MPC Video Renderer** - GPL-3.0-or-later. Renderer interface, media format, side-data and allocation code reuse; required upstream headers and DirectShow BaseClasses build inputs are included under `src/DirectShow`. Original notices are retained. Upstream: https://github.com/Aleksoid1978/VideoRenderer
- **DirectShow BaseClasses** - see original source notices and `src/DirectShow/external/BaseClasses/Readme.md`.
- **libplacebo** - LGPL-2.1-or-later. Built from upstream commit `275f7dea27924cdd34ab2e6c20e448b6bc4fab65` (7.387.0 release). Source URL and SHA-256 are pinned in `src/deps/libplacebo-source.lock.json`; local build tools are pinned separately. The upstream source is not patched. Upstream: https://github.com/haasn/libplacebo
- **xxHash and glad-generated OpenGL loader** - compiled into libplacebo. Their original notices are included beside the libplacebo license; their source/build inputs are pinned in the libplacebo build-tool locks.
- **libdovi**, **Little CMS**, **shaderc**, **SPIRV-Cross**, **Vulkan loader** and GCC runtime libraries - runtime dependencies obtained from MSYS2. Their exact package versions, URLs and SHA256 hashes are recorded in `src/deps/packages.lock.json`. Package license files are restored under `src/deps/msys/ucrt64/share/licenses`.
- **PlayKit shader collection** - upstream https://github.com/hooke007/mpv_PlayKit . Included source files retain their original authorship and licenses; see `src/shaders/PlayKit` and its source manifest. Collection licensing does not replace individual shader licenses.
- **Adaptive Sharpen** - original shader source and notice are retained under `src/shaders` and in the embedded source header.

Bundled source notices take precedence for their respective components. The renderer's name change does not change third-party ownership or licensing.

## NVIDIA NVAPI

The optional NVIDIA HDR10 driver interface uses the public NVAPI definitions under the MIT license. The driver-provided nvapi64.dll is loaded from Windows; no NVIDIA runtime DLL is bundled. See `licenses/nvapi/`.

## NVIDIA driver video processing

The optional Super Resolution request uses the Direct3D 11 Video Processor extension also used by MPC Video Renderer and Chromium. No NVIDIA RTX Video SDK headers, libraries, runtime DLLs or helper executable are included or linked. Super Resolution must be enabled in the NVIDIA driver settings; the driver controls availability and quality. A successful D3D11 request is not a guarantee that the driver applies the enhancement.

## Integrated Dolby Vision processing

The internal processing module in `src/DolbyVision` retains the player's splitter and base-layer decoder. FFmpeg reads the same local file and decodes only the enhancement layer when required. MEL metadata is parsed with libdovi; libplacebo combines the delivered layers in FrankyRenderer.

The FFmpeg build inputs come from the LAV-maintained FFmpeg fork, commit `29e0b0813d855d55536cd11ae4a8b6de88ee6461`: https://github.com/clsid2/FFmpeg . Exact runtime SHA256 values are recorded in `src/deps/dv-ffmpeg.lock.json`. The original FFmpeg license files are included under `licenses/ffmpeg` in packages. No LAV DirectShow decoder is bundled or registered by this feature. Build inputs can be restored with `src/prepare_dv_dependencies.py`; see `src/DolbyVision/BUILD.md`.
