**English** · [Deutsch](DE-Using-the-menu.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Troubleshooting](Troubleshooting.md)

<a id="using-the-menu-examples"></a>

## Everyday examples

Examples from everyday viewing.

| Situation | Setting | What to check |
| --- | --- | --- |
| You are looking for a setting but do not know where it is. | Press Ctrl+F and type a word, such as **bars** or **gamma**. | Clear the search to return to the page. |
| You changed too much and the picture looks odd. | The round arrow next to the setting, or **Reset Page**. | **Reset All** affects the whole display. |
| You do not want to tune anything yourself, but want more quality. | Switch **Quality** to **High Quality**. | If playback stutters, go back to **Default**. |
| You want to try a change first. | Change the setting, check it in the running video, then **OK** or **Cancel**. | **Cancel** restores the previous state. |

## Finding settings in the current menu

The sidebar has **General, Processing, Scaling, Color, HDR, Shaders, Profiles and Shortcuts**. The old separate Black Bars / Zoom category is now under Processing. Color combines Picture Controls and Calibration; HDR's zone adjustments are under Tone Controls. Existing Wiki page URLs are retained for those topics.

- **Search settings** filters settings by text. Clear the search to return to the normal page/tab view; Ctrl+F focuses search.
- **Number fields:** type a value or use the minus/plus buttons. Enter, Tab or leaving the field applies it; Escape restores the value being edited. Decimal settings can accept fractional values. The range appears beside the setting name; the centre of the page is not a percentage position.
- **Reset controls:** the circular arrow restores one setting. Reset Page affects the current page. On Default, Reset All restores the selected display's settings; when editing a content profile it removes that profile's overrides so it matches Default again. Reset Shader restores the selected shader's parameters. Check the confirmation before a broad reset.
- **Quality:** Fast, Default and High Quality are starting points. Measure this PC tests the current display/GPU workload, including active shaders, sharpening and Smooth Motion. Measured identifies a measured selection; changing a setting makes it Custom. Custom retains your own settings while trying presets.
- **Profile:** the selector beside Quality chooses the settings you edit. Default is the selected display's baseline. Folder-based profiles inherit settings they do not override; see [Profiles](Profiles.md).
- **Appearance and language:** General > Appearance changes the settings window's theme, accent and language. Remember Last Page also restores the last tab and scroll position. These preferences do not grade the video.
- **Live preview:** the player remains usable while the menu is open. OK accepts renderer changes; Cancel restores the original settings and profile edits. The menu closes when another file is opened or the player closes.
- **Window size:** the menu can be resized and retains its chosen size. The search and reset controls stay in the top area.
- **Import / Export:** a Settings.ini backup includes display settings, content profiles and Custom quality. Import replaces the current settings. Keep external files separately. See [settings backup and restore](Profiles.md#settings-backup-and-restore).
- **Unavailable controls** need the relevant switch, filter or processing route. For Custom Filters choose the **Role** first; only that role's settings are shown.

## Current page map

| Sidebar | Tabs / topics |
| --- | --- |
| General | Output, display modes, analysis and diagnostics; Background; Appearance |
| Processing | Image Cleanup; Deinterlacing & Dithering; Black Bars / Zoom |
| Scaling | Filters; Custom Filters and their Role selector |
| Color | Picture Controls; Calibration |
| HDR | Output & Peak Detection; Tone Mapping; Gamut Mapping; Tone Controls |
| Shaders | Alphabetical list, automatic or manual order and each shader's parameters |
| Profiles | Default/display baselines, folder matching and overrides |
| Shortcuts | Key assignments for settings and renderer actions |

## Basic terms

**Luminance** is brightness measured in nits. **Chroma** carries color information. **Gamut** is the range of colors. Tone mapping fits brightness and gamut mapping fits colors. **Ringing** appears as outlines at edges. **Banding** is visible stepping in a gradient. Debanding smooths existing bands. Dithering masks rounding steps.
