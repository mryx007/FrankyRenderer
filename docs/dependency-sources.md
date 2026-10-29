# Runtime dependency sources

libplacebo is built from pinned upstream source; the remaining runtime libraries use official MSYS2 source packages. The local libplacebo build recipe is `src/Tools/build_libplacebo.py`, with source, compiler and Python tool locks under `src/deps/libplacebo-*.lock.json`. Download links are provided below; the [machine-readable manifest](dependency-sources.json) records the same URLs, sizes and SHA-256 checksums. The [runtime names](runtime-names.md) document the DLL names used in the installation package.

- [libplacebo 7.387.0 release, commit 275f7dea2792](https://codeload.github.com/haasn/libplacebo/tar.gz/275f7dea27924cdd34ab2e6c20e448b6bc4fab65)

  SHA-256: `adbca47395410ada01414bc527e0103c716b9b758be857464c40e5ff0ec695be`

- [LAV-maintained FFmpeg, commit 29e0b0813d85](https://github.com/clsid2/FFmpeg/tree/29e0b0813d855d55536cd11ae4a8b6de88ee6461)

  Used internally for local-file enhancement-layer reading/decoding, without replacing the player's DirectShow decoder. The exact four runtime hashes are in [dv-ffmpeg.lock.json](../src/deps/dv-ffmpeg.lock.json); the package includes their original FFmpeg license files under `licenses/ffmpeg`. [Build setup](../src/DolbyVision/BUILD.md) verifies the source revision and runtime hashes before generating MSVC import libraries.

- [mingw-w64-lcms2-2.19.1-1.src.tar.zst](https://repo.msys2.org/mingw/sources/mingw-w64-lcms2-2.19.1-1.src.tar.zst)

  SHA-256: `83414d0d95c5cb2548d3050670694e9efc64304a52190f42bcad951efa038b41`

- [mingw-w64-libdovi-3.4.0-1.src.tar.zst](https://repo.msys2.org/mingw/sources/mingw-w64-libdovi-3.4.0-1.src.tar.zst)

  SHA-256: `50e7303249bd57b9bb543e87e5de4b6331d744b22d2d26861bc8968ee60a70a3`

- [mingw-w64-shaderc-2026.4-1.src.tar.zst](https://repo.msys2.org/mingw/sources/mingw-w64-shaderc-2026.4-1.src.tar.zst)

  SHA-256: `7c9df6e89d39cc343aa914b3728d4fbacc35832c5ecda83d54ae386966186e2a`

- [mingw-w64-spirv-cross-1~1.4.363.0-1.src.tar.zst](https://repo.msys2.org/mingw/sources/mingw-w64-spirv-cross-1~1.4.363.0-1.src.tar.zst)

  SHA-256: `2043f9e7e1359edbf1618a59c38a687d47e3a5779844db6adc4f8de78c5e1eed`

- [mingw-w64-vulkan-loader-1~1.4.363.0-1.src.tar.zst](https://repo.msys2.org/mingw/sources/mingw-w64-vulkan-loader-1~1.4.363.0-1.src.tar.zst)

  SHA-256: `61ed2ebe6007c464af1687e4ecad93199e9faeb3d8dd4d3c24e5b2576f730c9b`

- [mingw-w64-gcc-16.2.0-6.src.tar.zst](https://repo.msys2.org/mingw/sources/mingw-w64-gcc-16.2.0-6.src.tar.zst)

  SHA-256: `a901d8afb78438abcff9ed020cd9aa9431db76de1992171c2454c3c07ad289e8`

- [mingw-w64-winpthreads-14.0.0.r495.g63ebe6cdb-1.src.tar.zst](https://repo.msys2.org/mingw/sources/mingw-w64-winpthreads-14.0.0.r495.g63ebe6cdb-1.src.tar.zst)

  SHA-256: `2e4dcb3933fc2dfbd6939935f601e10ae7f9abad06f0be508e1e152768e3e55b`
