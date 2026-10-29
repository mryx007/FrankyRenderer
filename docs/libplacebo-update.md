# libplacebo 7.387.0 release update

FrankyRenderer now builds the official libplacebo **v7.387.0** release, pinned to
upstream commit `275f7dea27924cdd34ab2e6c20e448b6bc4fab65` (released 25 October 2026).
The release adds one Vulkan pipeline-stage synchronization fix to the previously
used snapshot `1532bf6893f7e6dbe1b1943488180d125a421f6f`. The API remains 387.
Our renderer-side screenshot correction is retained.

## Integration

- `src/deps/libplacebo-source.lock.json` pins the original source archive and SHA-256.
- `src/deps/libplacebo-build.lock.json` pins the local GCC toolchain packages.
- `src/deps/libplacebo-python.lock.json` pins the Meson/glad code-generation tools.
- `python src/Tools/fetch_dependencies.py` restores the runtime dependencies and builds
  libplacebo locally through `src/Tools/build_libplacebo.py`. A verified cache avoids
  rebuilding unchanged inputs. No upstream source patch is applied.
- The matching API-387 headers, DLL and MSVC import definition are used together.
  Packaged `libplacebo-387.dll` retains the user-facing name `VideoProcessing.dll`.
- Frame-mixing timestamps use the upstream double-precision representation.
- Dolby Vision reshape pivots use code / 2^BL_bit_depth, matching the new upstream
  mapping. FEL metadata is still rejected without a decoded enhancement layer.
  This update does not enable FEL decoding in PotPlayer.
- Existing renderer settings and the Ctrl+J shortcut are retained. Newly added
  libplacebo options do not automatically create additional menu controls.

## Release validation on 26 October 2026

- Official release source archive and SHA-256 verified; no upstream source changes.
- Upstream tests: **16 passed, 0 failed, 1 skipped** (surfaceless OpenGL).
  D3D11 and Vulkan GPU tests passed.
- Renderer rebuilt against the matching release headers and runtime.
- Extracted 1.0.6 replacement ZIP passed CPU and GPU screenshot checks while
  playing and paused, CPU/GPU DirectShow regression tests, Smooth Motion,
  concurrent flush, reconnect and settings round trips.
- Bwdif TFF and Yadif BFF both passed exact end-of-stream draining.
- The package still contains 62 installation files and retains the renderer-side
  screenshot fix. This update does not enable additional menu features or FEL decoding.

## Previous snapshot validation on 25 October 2026

- Upstream tests: **16 passed, 0 failed, 1 skipped**. The skipped test is
  surfaceless OpenGL; D3D11 and Vulkan GPU tests passed.
- Clean build from the pinned source/tool locks and unchanged cache restore passed.
- Renderer build, package dependency/name checks and file hashes passed.
- DirectShow CPU and native GPU input, MPC-BE aggregation, reconnect, flush races,
  subtitle lifecycle, temporal frame mixing and settings round trips passed.
- Bwdif TFF and Yadif BFF tests passed, including exact end-of-stream draining.
- Native-HDR signal/metadata tests passed with a mocked driver controller;
  no physical display HDR setter was called.
- RTX pipeline tests, image analysis, tone/gamut controls, clipping protection,
  INI import/export, display profile cancellation/rollback and monitor-change
  detection passed.
- Child-window, keyboard-focus and cursor-lifecycle regression tests passed.
- The 28 original SDR/HDR10/HLG patterns passed CPU/GPU output equality, RGB-level
  conversion and exact settings restoration checks. All 40 independent numeric
  gray/color/PQ checks passed. The results are under the local
  `Renderer-Testbilder/Pruefung/libplacebo-387` directory.
- Python syntax and Git whitespace checks passed.

The native HDR tests establish code/GPU behavior, not the actual HDMI signal at
the user's TV. Real PotPlayer/MPC-BE playback and visual comparison of the changed
tone mapping remain separate user checks. An initial user playback check was
reported successful before preparing release 1.0.5.
