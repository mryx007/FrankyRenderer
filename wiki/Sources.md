**English** · [Deutsch](DE-Sources.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Troubleshooting](Troubleshooting.md)

## Review scope

Reviewed **18 November 2026** against the development settings model, menu host and implementation. Installed shaders, displays and profiles affect which controls appear. The [Nightly changelog](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/docs/nightly-changelog.md) tracks changes between builds.

## Implementation references

- [Menu controls and default bindings](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/UI/Direct2DSettingsModel.h)
- [Visible page grouping, numbers, profiles and shader list](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/UI/Direct2DSettingsHost.cpp)
- [Available choices](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/SettingsChoices.h)
- [Settings defaults and validation](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/Settings.h)
- [Folder profile matching and inheritance](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/ContentProfiles.h)
- [RTX input/output, initialization validation and failure detail](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/RtxVideoProcessor.h)
- [RTX frame reuse, metadata and independent HDR/VSR fallback](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/RtxScaling.h)
- [Rendering, display output and statistics](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/Backend.cpp)
- [Custom shader discovery](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/CustomShaders.h)
- [Diagnostic report contents](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/DiagnosticReport.h)

## External terminology and algorithm references

- [libplacebo options](https://libplacebo.org/options/) and [public headers](https://github.com/haasn/libplacebo/tree/master/src/include/libplacebo): processing/filter concepts and parameter definitions. FrankyRenderer's bundled revision and integration take precedence over unrelated upstream defaults.
- [mpv manual](https://mpv.io/manual/master/): temporal filters, dithering and shader concepts. mpv commands are not automatically FrankyRenderer menu settings.
- [NVIDIA RTX Video FAQ](https://nvidia.custhelp.com/app/answers/detail/a_id/5448): driver/app requirements. Driver-dependent activation must be distinguished from accepting a renderer request.
- [Microsoft ICC rendering intents](https://learn.microsoft.com/en-us/windows/win32/wcs/rendering-intents): intent terminology, distinct from libplacebo gamut mapping.
- [AMD FidelityFX CAS](https://gpuopen.com/fidelityfx-cas/): contrast-adaptive sharpening context.
- [PlayKit shader guide](https://github.com/hooke007/mpv_PlayKit/wiki/4_GLSL) and the renderer's bundled source manifest: provenance and the particular variants shipped in the package.
- [FSRCNNX](https://github.com/igv/FSRCNN-TensorFlow), [RAVU](https://github.com/bjin/mpv-prescalers), [Anime4K](https://github.com/bloc97/Anime4K), [ArtCNN](https://github.com/Artoriuz/ArtCNN) and [CuNNy](https://github.com/funnyplanter/CuNNy): upstream shader/model families.

Explanations and examples are written for this guide. Code review, an automated check and a real-player test are different levels of evidence; none alone guarantees every source/filter/GPU combination. Use [Troubleshooting](Troubleshooting.md) for a reproducible report.
