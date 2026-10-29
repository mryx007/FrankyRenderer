# Source layout

- **Renderer/** — DirectShow renderer, video input, GPU processing, presentation and statistics.
- **Settings/** — configuration, display profiles, imports/exports and shader settings.
- **UI/** — native Direct2D settings interface and controls.
- **DolbyVision/** — integrated Dolby Vision processing and metadata.
- **Shaders/** — HLSL sources and bundled GLSL shaders.
- **Tests/** — regression tests, test build scripts and the isolated test runner.
- **Tools/** — production build, dependency restore, packaging, installation and formatting scripts.
- **DirectShow/** — upstream compatibility interfaces and BaseClasses, with original notices.
- **deps/** — dependency locks and locally restored SDKs.
- **build/** — generated objects, binaries, caches and logs; not tracked in Git.

Run production builds from the repository root using `build.cmd native` or `build.cmd potplayer`. Packages retain their existing layout and installation commands.
