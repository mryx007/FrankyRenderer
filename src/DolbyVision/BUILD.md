# Integrated Dolby Vision processing

`build.cmd native` and `build.cmd potplayer` build the internal `Processor.cpp` directly into FrankyRenderer.ax. There is no separate DV filter AX, class registration or menu switch. Supported local Profile 7 FEL/MEL files are detected automatically. The player's existing splitter and base-layer decoder remain in use; FFmpeg reads matching enhancement data, and libplacebo reconstructs and processes the image.

Restore normal libplacebo/MSYS2 dependencies first. In a Visual Studio x64 tools prompt:

```
python src/prepare_dv_dependencies.py --ffmpeg <local-clsid2-FFmpeg-checkout> --runtime <matching-runtime-folder>
```

This verifies `src/deps/dv-ffmpeg.lock.json` and restores headers, import libraries and four runtime DLLs. No filter is registered by the dependency setup. Both package variants retain only their existing renderer and settings-page identities. Their installers retire their own legacy DV-filter entries when updating an existing installation, without touching the other variant.

Current scope remains local progressive UHD Profile 7 with 3840x2160 P010 base and 1920x1080 FEL enhancement. CPU and native D3D11 inputs are supported. MEL needs metadata only. This provides processed HDR/PQ or SDR output, not native Dolby Vision TV signalling. Ordinary SDR/HDR10 and other DV profiles retain their existing renderer path.

Processing and file I/O run before the renderer's scheduling/presentation locks. Flush and Stop cancel enhancement reads before waiting for Receive; segment changes and disconnects reset the corresponding reader/device state. The current layered path keeps its existing progressive, non-temporal behavior. Integrated Smooth Motion for layered DV is not claimed.
