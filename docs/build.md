# Building FrankyRenderer

Requirements: Windows x64, Visual Studio 2022 or later with Desktop development with C++, a Windows SDK containing `fxc.exe`, and **CPython 3.14 x64**.

From the repository root, in a Visual Studio x64 developer shell:

```powershell
python -m pip install -r requirements.txt
python src/Tools/fetch_dependencies.py
python src/Tools/prepare_sdk.py
python src/Tools/prepare_dv_dependencies.py --ffmpeg <pinned-FFmpeg-checkout>
.\build.cmd
python src/Tools/package_release.py --output releases/local-packages
```

Runtime dependencies are restored locally from official MSYS2 packages using the checked-in SHA256 lock file. libplacebo is built from pinned upstream commit `29a56f9290bacc3f7c6bd899ac7a64ec4e68c8e2` (7.387.0 release), with hash-verified local compiler packages and Python build tools. The first dependency restore also compiles libplacebo; later restores reuse its verified build cache. Build from a path without spaces, or with Windows 8.3 filenames enabled. The scripts locate Visual Studio through `vswhere`; no personal SDK paths are required. Build output is `src/build/bin/FrankyRenderer.ax`. `package_release.py` creates one shared ZIP in the selected new output folder without installing or publishing it.

The FFmpeg libraries for integrated DV processing are built from source in an additional step, after the MSYS2 restore. Their source commit and configure options are pinned in `src/deps/dv-ffmpeg.lock.json`; the setup step builds them with the verified compiler packages (plus `make` and `nasm` from `src/deps/ffmpeg-build.lock.json`) and generates matching MSVC import libraries. It needs Git for Windows for its bash. See [DV build requirements](../src/DolbyVision/BUILD.md).

All build sources are under `src`, including the required compatibility interfaces and DirectShow BaseClasses in `src/DirectShow`. FrankyRenderer uses its own libplacebo/D3D11 rendering path.

