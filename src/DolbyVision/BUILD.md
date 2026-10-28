# Integrated Dolby Vision processing

`build.cmd native` and `build.cmd potplayer` build the internal `Processor.cpp` directly into FrankyRenderer.ax. There is no separate DV filter AX, class registration or menu switch. Supported local Profile 7 FEL/MEL files are detected automatically. The player's existing splitter and base-layer decoder remain in use; FFmpeg reads matching enhancement data, and libplacebo reconstructs and processes the image.

Restore normal libplacebo/MSYS2 dependencies first. In a Visual Studio x64 tools prompt:

```
python src/prepare_dv_dependencies.py --ffmpeg <local-clsid2-FFmpeg-checkout> --runtime <matching-runtime-folder>
```

This verifies `src/deps/dv-ffmpeg.lock.json` and restores headers, import libraries and four runtime DLLs. No filter is registered by the dependency setup. Both package variants retain only their existing renderer and settings-page identities. Their installers retire their own legacy DV-filter entries when updating an existing installation, without touching the other variant.

The integrated path accepts local progressive Profile 7 with P010 base input. Base dimensions come from the decoded input; enhancement dimensions come from the enhancement decoder's SPS/output. There is no fixed 3840x2160 base or 1920x1080 enhancement requirement. CPU and native D3D11 inputs are supported. MEL needs metadata only. This provides processed HDR/PQ or SDR output, not native Dolby Vision TV signalling. Ordinary SDR/HDR10 and other DV profiles retain their existing renderer path.

Processing and file I/O run before the renderer's scheduling/presentation locks. Flush and Stop cancel enhancement reads before waiting for Receive; segment changes and disconnects reset the corresponding reader/device state. Prepared FEL/MEL frames enter the existing Smooth Motion queue. Each history entry owns its base planes, enhancement planes and DV metadata, so temporal mixing cannot substitute another frame's enhancement layer. Oversample and custom temporal filters use the normal timing, paused-seek and end-of-stream handling.

`src/build_dv_motion_tests.cmd` builds a GPU regression test for FEL/MEL history ownership, released input samples, cache eviction and temporal mixing with differing layer sizes. Run `src/build/bin/test_dv_motion.exe` after building it. Real-file tests additionally cover CPU/native D3D11 base input, pause, seeks, subtitles, live Smooth Motion toggles and end of stream.
