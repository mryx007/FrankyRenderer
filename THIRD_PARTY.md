# Third-party components

- **MPC Video Renderer** - GPL-3.0-or-later. Renderer interface, media format, side-data and allocation code reuse; required upstream headers and DirectShow BaseClasses build inputs are included under `src/DirectShow`. Original notices are retained. Upstream: https://github.com/Aleksoid1978/VideoRenderer
- **DirectShow BaseClasses** - see original source notices and `src/DirectShow/external/BaseClasses/Readme.md`.
- **libplacebo** - LGPL-2.1-or-later. Built from upstream commit `29a56f9290bacc3f7c6bd899ac7a64ec4e68c8e2` (7.387.0 release with later upstream fixes). Source URL and SHA-256 are pinned in `src/deps/libplacebo-source.lock.json`; local build tools are pinned separately. One small source patch (dense D3D11 UAV slot numbering) is applied; it is included under `licenses/dependencies/libplacebo/patches` and in `src/deps/patches/libplacebo`. Upstream: https://github.com/haasn/libplacebo
- **xxHash and glad-generated OpenGL loader** - compiled into libplacebo. Their original notices are included beside the libplacebo license; their source/build inputs are pinned in the libplacebo build-tool locks.
- **libdovi**, **Little CMS**, **shaderc**, **SPIRV-Cross**, **Vulkan loader** and GCC runtime libraries - runtime dependencies obtained from MSYS2. Their exact package versions, URLs and SHA256 hashes are recorded in `src/deps/packages.lock.json`. Package license files are restored under `src/deps/msys/ucrt64/share/licenses`.
- **PlayKit shader collection** - upstream https://github.com/hooke007/mpv_PlayKit . Included source files retain their original authorship and licenses; see `src/Shaders/PlayKit` and its source manifest. Collection licensing does not replace individual shader licenses.
- **SSimDownscaler** - LGPL-3.0-or-later, by igv. `src/Shaders/PlayKit/SSimDownscaler_RT.glsl` is igv's original (https://gist.github.com/igv/36508af3ffc84410fe39761d6969be10) with the PlayKit THR and DEBUG parameters added; its notice is retained in the file header.
- **SSimDownscaler_AR** - LGPL-3.0-or-later, based on igv's SSimDownscaler. `src/Shaders/Custom/SSimDownscaler_AR.glsl` adds an edge-selective anti-ringing limit to the reconstruction; igv's notice is retained in the file header.
- **SubRenderIntf.h** (subtitle renderer interface used by XySubFilter) - BSD license, (C) 2011-2015 madshi; included unchanged under `src/DirectShow/Include`.
- **Adaptive Sharpen** - the notice is in `licenses/adaptive-sharpen/LICENSE.txt` of the package and in the embedded source header; the original shader is under `src/Shaders`.

Bundled source notices take precedence for their respective components. The renderer's name change does not change third-party ownership or licensing.

## NVIDIA NVAPI

The optional NVIDIA HDR10 driver interface uses the public NVAPI definitions under the MIT license. The driver-provided nvapi64.dll is loaded from Windows; no NVIDIA runtime DLL is bundled. See `licenses/nvapi/`.

## NVIDIA driver video processing

The optional Super Resolution request uses the Direct3D 11 Video Processor extension also used by MPC Video Renderer and Chromium. No NVIDIA RTX Video SDK headers, libraries, runtime DLLs or helper executable are included or linked. Super Resolution must be enabled in the NVIDIA driver settings; the driver controls availability and quality. A successful D3D11 request is not a guarantee that the driver applies the enhancement.

## Integrated Dolby Vision processing

The internal processing module in `src/DolbyVision` retains the player's splitter and base-layer decoder. FFmpeg reads the same local file and decodes only the enhancement layer when required. MEL metadata is parsed with libdovi; libplacebo combines the delivered layers in FrankyRenderer.

FFmpeg is built from the LAV-maintained FFmpeg fork, commit `f67e27d31cc4da05d463d57300bc8f4358e4ef6b`: https://github.com/clsid2/FFmpeg . The build enables only HEVC decoding (D3D11VA) and the Matroska, MP4 and MPEG-TS demuxers and is licensed under the LGPL 2.1 or later; no GPL components are enabled. The exact configure options are recorded in `src/deps/dv-ffmpeg.lock.json`, which packages include as `licenses/ffmpeg/source-and-runtime.json`, together with the FFmpeg license files. zlib is linked statically. No LAV DirectShow decoder is bundled or registered by this feature. The libraries are built with `src/Tools/prepare_dv_dependencies.py`; see `src/DolbyVision/BUILD.md`.
