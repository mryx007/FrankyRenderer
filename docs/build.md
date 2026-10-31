# Building FrankyRenderer

Requirements: Windows x64, Visual Studio 2022 or later with Desktop development with C++, a Windows SDK containing `fxc.exe`, and **CPython 3.14 x64**.

From the repository root, in a Visual Studio x64 developer shell:

```powershell
python -m pip install -r requirements.txt
python src/Tools/fetch_dependencies.py
python src/Tools/prepare_sdk.py
python src/Tools/prepare_dv_dependencies.py --ffmpeg <pinned-FFmpeg-checkout> --runtime <matching-FFmpeg-runtime-folder>
.\build.cmd
python src/Tools/package_release.py --output releases/local-packages
```

Runtime dependencies are restored locally from official MSYS2 packages using the checked-in SHA256 lock file. libplacebo is built from pinned upstream commit `275f7dea27924cdd34ab2e6c20e448b6bc4fab65` (7.387.0 release), with hash-verified local compiler packages and Python build tools. The first dependency restore also compiles libplacebo; later restores reuse its verified build cache. Build from a path without spaces, or with Windows 8.3 filenames enabled. The scripts locate Visual Studio through `vswhere`; no personal SDK paths are required. Build output is `src/build/bin/FrankyRenderer.ax`. `package_release.py` creates one ZIP for both MPC-BE and PotPlayer with native FrankyRenderer support, without installing or publishing it.

The FFmpeg inputs for integrated DV processing are an additional prerequisite, separate from the MSYS2 restore. Their exact source commit and runtime hashes are pinned in `src/deps/dv-ffmpeg.lock.json`; the setup step verifies them and generates matching MSVC import libraries. See [DV build requirements](../src/DolbyVision/BUILD.md).

All build sources are under `src`, including the required compatibility interfaces and DirectShow BaseClasses in `src/DirectShow`. FrankyRenderer uses its own libplacebo/D3D11 rendering path.

