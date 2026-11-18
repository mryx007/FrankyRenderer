[English](Home.md) · **Deutsch**

Willkommen im **FrankyRenderer-Einstellungsleitfaden**.

Stand **18. November 2026**. Die Anleitung orientiert sich am Entwicklungsstand. Neuere Optionen sind in den [Nightly-Builds](https://github.com/mryx007/FrankyRenderer-Nightly/releases/latest) verfügbar.

[Menü bedienen](DE-Using-the-menu.md) zeigt, wo du Einstellungen findest und zurücksetzt.

## Einstellungen und Bedienung

| Leitfaden | Thema |
| --- | --- |
| [Allgemein](DE-General.md) | Ausgabe, Bildanalyse, Anzeigemodus, Diagnose und Darstellung |
| [Verarbeitung](DE-Processing.md) | Bildbereinigung, Deinterlacing und Dithering |
| [Schwarze Balken und Zoom](DE-Black-Bars-and-Zoom.md) | Balkenerkennung, Zoom, vertikale Bildposition und eingebrannte Untertitel |
| [Skalierung](DE-Scaling.md) | Filter, Smooth Motion, RTX VSR und benutzerdefinierte Filter |
| [Farbanpassung](DE-Color-Adjustment.md) | Helligkeit, Kontrast, Gamma und weitere Bildregler |
| [Farbmanagement](DE-Color-Management.md) | Kalibrierung, ICC-Profile und 3D-LUTs |
| [HDR und Farbzuordnung](DE-HDR-and-Color-Mapping.md) | HDR-Ausgabe, Dolby Vision und [welche Tone-Mapping-Kurve passt](DE-HDR-and-Color-Mapping.md#choosing-a-tone-mapping-function) |
| [Shader](DE-Shaders.md) | Mitgelieferte und eigene Shader, Einstellungen und Reihenfolge |
| [Profile](DE-Profiles.md) | Anzeigen, Ordnerregeln und Sicherung der Einstellungen |
| [Tastenkürzel](DE-Shortcuts.md) | Einstellungen und Renderer-Aktionen per Tastendruck |
| [Fehlerbehebung](DE-Troubleshooting.md) | Strg+J-Statistik, RTX-Probleme und Diagnosebericht |

## Neu und aktualisiert

- Mit der [vertikalen Bildposition](DE-Black-Bars-and-Zoom.md#black-bars-zoom-detection-zoom-vertical-image-position) rückst du das Bild an den oberen oder unteren Rand. Erkannte eingebrannte Untertitel können bei aktivem Schutz in abgeschnittenen Balken sichtbar bleiben.
- [Dolby Vision verwenden](DE-HDR-and-Color-Mapping.md#hdr-color-mapping-hdr-output-use-dolby-vision) spielt Profile 7 und 8 bei **Aus** über die HDR10- oder HLG-Basisschicht ab.
- [LUT aus dem Videoordner](DE-Color-Management.md#color-management-3d-lut-lut-from-video-folder) wählt passend zum Video eine .cube-Datei.
- [Darstellung und Statistik](DE-General.md#general-themes), [automatische Shader-Reihenfolge und SSimDownscaler_AR](DE-Shaders.md#shaders-chain-controls-automatic-order), [Sicherung der Einstellungen](DE-Profiles.md#einstellungen-sichern-und-wiederherstellen) und [Strg+J-Anzeigen](DE-Troubleshooting.md#renderzeit-und-bildaussetzer) sind ebenfalls aktualisiert.

Ändere eine Einstellung nach der anderen. Vergleiche dieselbe Szene und prüfe mit **Strg+J**, welche Verarbeitung läuft.

## Wiedergabe und Vergleich

Wähle FrankyRenderer in **MPC-BE ab 1.9.3**, **PotPlayer ab 261030 Beta (64 Bit)** mit nativer Unterstützung oder **MPC-HC ab 2.8.5.14** unter **Optionen > Wiedergabe > Ausgabe**.

Beim Wechsel von 1.2.1 oder älter auf einen aktuellen Nightly führe Install.cmd einmal als Administrator aus. Das entfernt das alte Installationsskript und die alte Sicherung der Registrierung.

[Quellen](DE-Sources.md) · [Stable-Download](https://github.com/mryx007/FrankyRenderer/releases/latest) · [Nightly-Download](https://github.com/mryx007/FrankyRenderer-Nightly/releases/latest) · [Fehler melden](https://github.com/mryx007/FrankyRenderer/issues)
