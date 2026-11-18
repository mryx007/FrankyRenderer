[English](Sources.md) · **Deutsch**

[Startseite](DE-Home.md) · [Menü bedienen](DE-Using-the-menu.md) · [Fehlerbehebung](DE-Troubleshooting.md)

## Stand der Anleitung

Die Anleitung wurde am **18. November 2026** mit dem Einstellungsmodell, dem Menü und der Implementierung abgeglichen. Die angezeigten Optionen hängen von Profilen, Anzeigen und installierten Shadern ab. Die deutschen Bezeichnungen stammen, soweit vorhanden, aus der [deutschen Menüübersetzung](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/translations/de.po). Änderungen zwischen Builds stehen im [Nightly-Changelog](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/docs/nightly-changelog.md).

## Implementierung

- [Einstellungsmodell und Menüoptionen](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/UI/Direct2DSettingsModel.h)
- [Menüaufbau und Darstellung](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/UI/Direct2DSettingsHost.cpp)
- [Verfügbare Einstellungen](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/SettingsChoices.h)
- [Standardwerte](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/Settings.h)
- [Ordnerprofile](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/ContentProfiles.h)
- [RTX Video](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/RtxVideoProcessor.h)
- [RTX-Bildverarbeitung und Rückfallpfade](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/RtxScaling.h)
- [Ausgabe und Statistik](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/Backend.cpp)
- [Erkennung eigener Shader](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Settings/CustomShaders.h)
- [Diagnosebericht](https://github.com/mryx007/FrankyRenderer-Nightly/blob/main/src/Renderer/DiagnosticReport.h)

## Referenzen zu Filtern und Farbverarbeitung

- [libplacebo-Optionen](https://libplacebo.org/options/) für Skalierung und Farbverarbeitung.
- [mpv-Handbuch](https://mpv.io/manual/master/) für Frame-Mixer, Dithering und Shader-Formate.
- [NVIDIA RTX Video](https://nvidia.custhelp.com/app/answers/detail/a_id/5448) für Anforderungen an Treiber und NVIDIA-App.
- [Microsoft Rendering Intents](https://learn.microsoft.com/en-us/windows/win32/wcs/color-rendering-intents) für ICC-Farbwiedergabe.
- [AMD FidelityFX CAS](https://gpuopen.com/fidelityfx-cas/) für kontrastadaptive Schärfung.
- [PlayKit-Shader-Dokumentation](https://github.com/hooke007/mpv_PlayKit/wiki/4_GLSL) für mitgelieferte Shader und ihre Herkunft.
- [FSRCNNX](https://github.com/igv/FSRCNN-TensorFlow), [RAVU](https://github.com/bjin/mpv-prescalers), [Anime4K](https://github.com/bloc97/Anime4K), [ArtCNN](https://github.com/Artoriuz/ArtCNN) und [CuNNy](https://github.com/funnyplanter/CuNNy) dokumentieren die ursprünglichen Shader und Modelle.

Wenn eine Kombination aus Video, Einstellungen und GPU Probleme macht, hilft ein reproduzierbarer Test. Die Schritte stehen unter [Fehlerbehebung](DE-Troubleshooting.md).
