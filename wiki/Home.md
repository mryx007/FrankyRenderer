**English** · [Deutsch](DE-Home.md)

Welcome to the **FrankyRenderer settings guide**.

This guide was reviewed against the current development menu on **18 November 2026**. Recent additions are available in [Nightly builds](https://github.com/mryx007/FrankyRenderer-Nightly/releases/latest); an older stable release may have fewer settings. Source links target the development tree and can advance after this review.

[Using the menu](Using-the-menu.md) shows where to find settings and how to reset them.

## Settings and usage

| Guide | Current menu / topic |
| --- | --- |
| [General](General.md) | General: output, display modes, analysis, diagnostics, Background and Appearance |
| [Processing](Processing.md) | Processing: Image Cleanup and Deinterlacing & Dithering |
| [Black Bars and Zoom](Black-Bars-and-Zoom.md) | Processing: Black Bars / Zoom |
| [Scaling](Scaling.md) | Scaling: filters, Smooth Motion, RTX VSR and Custom Filter Role |
| [Color Adjustment](Color-Adjustment.md) | Color: Picture Controls |
| [Color Management](Color-Management.md) | Color: Calibration, ICC and 3D LUTs |
| [HDR and Color Mapping](HDR-and-Color-Mapping.md) | HDR output, Dolby Vision and [which tone mapping curve to use](HDR-and-Color-Mapping.md#choosing-a-tone-mapping-function) |
| [Shaders](Shaders.md) | Shaders: automatic or manual order, bundled shaders and custom files |
| [Profiles](Profiles.md) | Profiles: display baselines, folder matching and overrides |
| [Shortcuts](Shortcuts.md) | Shortcuts: settings and renderer actions |
| [Troubleshooting](Troubleshooting.md) | Statistics, RTX failures and diagnostic reports |

## Recent changes

- [Vertical Image Position](Black-Bars-and-Zoom.md#black-bars-zoom-detection-zoom-vertical-image-position) aligns the active picture and keeps detected burned-in subtitles visible in cropped bars with subtitle protection enabled.
- [Use Dolby Vision](HDR-and-Color-Mapping.md#hdr-color-mapping-hdr-output-use-dolby-vision) uses the HDR10 or HLG base layer of Profiles 7 and 8 when set to **Off**.
- [LUT from Video Folder](Color-Management.md#color-management-3d-lut-lut-from-video-folder) selects a .cube file alongside the video.
- [Appearance and statistics](General.md#general-themes), [shader order and SSimDownscaler_AR](Shaders.md#shaders-chain-controls-automatic-order), [settings backups](Profiles.md#settings-backup-and-restore) and [Ctrl+J timing](Troubleshooting.md#rendering-time-and-drops) have updated instructions.

Change one setting at a time on the selected display and profile. Compare the same scene and press **Ctrl+J** to see what processing is active.

## Players and references

Use a compatible 64-bit player. Select FrankyRenderer in **MPC-BE 1.9.3 or newer**, **PotPlayer 261030 Beta 64-bit or newer** with native support, or **MPC-HC 2.8.5.14 or newer** under Options > Playback > Output.

When upgrading to a recent Nightly from 1.2.1 or earlier, run Install.cmd once as administrator to remove the legacy setup script and registration backup.

[Sources](Sources.md) explains the code/documentation basis. [Stable downloads](https://github.com/mryx007/FrankyRenderer/releases/latest) · [Nightly downloads](https://github.com/mryx007/FrankyRenderer-Nightly/releases/latest) · [Report a bug](https://github.com/mryx007/FrankyRenderer/issues)
