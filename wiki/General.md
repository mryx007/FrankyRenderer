**English** · [Deutsch](DE-General.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

Output, image analysis, background, display modes and menu appearance.

**On this page**

- [Everyday examples](#general-examples)
- [Output](#general-output)
- [Image Analysis](#general-image-analysis)
- [Background & Transparency](#general-background-transparency)
- [Display Mode](#general-display-mode)
- [Diagnostics](#general-diagnostics)
- [Appearance](#general-themes)

<a id="general-examples"></a>

## Everyday examples

Examples from everyday viewing. Change one setting at a time and compare the same scene.

| Situation | Setting | What to check |
| --- | --- | --- |
| On the TV, black looks gray and the whole picture looks flat. | Set [RGB Output Levels](#general-output-rgb-output-levels) to the other value, such as **TV Levels (16–235)** instead of **PC Levels (0–255)**. | Night skies and dark clothing should be deep black and still show detail. |
| Shadows disappear completely and bright clouds look blown out. | Check [RGB Output Levels](#general-output-rgb-output-levels) as well. | Display, GPU driver and FrankyRenderer must use the same range. |
| Movies judder during camera pans on a 60 Hz TV. | [Switch Display Mode in Fullscreen](#general-display-mode-switch-display-mode-in-fullscreen) **On**, [Allowed Modes](#general-display-mode-allowed-modes) `23, 24, 50, 59, 60`. | Ctrl+J shows the new refresh rate. A 23.976 fps movie then plays at 23.976 Hz. |
| After the switch, the first seconds are missing because the TV is still black. | Set [Pause After Switching](#general-display-mode-pause-after-switching-seconds) to 3 to 5 seconds. | Playback should start only once the picture is back. |
| From the couch, the Ctrl+J statistics are too small. | Set [Statistics Text Size](#general-image-analysis-statistics-text-size) to 150. | 100 is the automatic size. |
| You want to see whether highlights or shadows are clipped. | [Image Analysis](#general-image-analysis-image-analysis) **On**, for example with the histogram or false color. | Turn it off again for normal viewing. |
| Something goes wrong and you want to report it. | [Diagnostic Log](#general-diagnostics-diagnostic-log) **On**, restart the player, reproduce the problem, then [Save Report](#general-diagnostics-save-report). | The report is saved to the desktop. |

<a id="general-output"></a>

## Output

Match the RGB output levels to the display input. A mismatch can wash out blacks or clip shadow and highlight detail.

| Setting | What it does |
| --- | --- |
| <a id="general-output-output-texture-format"></a>**Output Texture Format** | Chooses the numerical precision of the final GPU image that is handed to the presentation stage. This is separate from the file’s encoded bit depth. |
| <a id="general-output-rgb-output-levels"></a>**RGB Output Levels** | Sets the relationship between black, white and the numerical RGB signal range. PC Levels uses the full RGB range; TV Levels reserves the end regions. |
| <a id="general-output-dither-target-bit-depth"></a>**Dither Target Bit Depth** | Tells dithering how many output levels to target. Dithering distributes rounding errors as fine noise so smooth gradients are less likely to show visible steps. |
| <a id="general-output-max-frames-in-flight"></a>**Max Frames in Flight** | Limits how much GPU/presentation work can be buffered ahead. A longer queue can help absorb short scheduling variations, but increases the time between processing an image and seeing it. |

<a id="general-image-analysis"></a>

## Image Analysis

The Ctrl+J statistics and image analysis appear directly over the video.

| Setting | What it does |
| --- | --- |
| <a id="general-image-analysis-image-analysis"></a>**Image Analysis** | **On** shows analysis overlays such as a waveform, histogram or false color over the video. **Off** hides the analysis overlay. |
| <a id="general-image-analysis-statistics-text-size"></a><a id="general-image-analysis-statistics-in-video-area"></a>**Statistics Text Size (%)** | Scales the Ctrl+J statistics text for this display. 100% uses the automatic size. |

<a id="general-background-transparency"></a>

## Background & Transparency

| Setting | What it does |
| --- | --- |
| <a id="general-background-transparency-background-red"></a>**Background Red** | Sets the red component of the renderer background, on a normalized scale from 0 to 1. The background is used for uncovered output areas and transparent pixels. |
| <a id="general-background-transparency-background-green"></a>**Background Green** | Sets the green component of the renderer background, on a normalized scale from 0 to 1. The background is used for uncovered output areas and transparent pixels. |
| <a id="general-background-transparency-background-blue"></a>**Background Blue** | Sets the blue component of the renderer background, on a normalized scale from 0 to 1. The background is used for uncovered output areas and transparent pixels. |
| <a id="general-background-transparency-background-transparency"></a>**Background Transparency** | Sets the alpha/transparency of the rendered background. Zero is opaque and one is fully transparent. |
| <a id="general-background-transparency-corner-rounding"></a>**Corner Rounding** | Rounds the corners of the rendered image. Increasing the value removes a larger curved area at each corner and exposes the background or transparent region there. |
| <a id="general-background-transparency-skip-target-clearing"></a>**Skip Target Clearing** | **Off (default)** clears the background around the video. **On** skips clearing, so old pixels can remain around the edges. |
| <a id="general-background-transparency-blend-against-tiles"></a>**Blend Against Tiles** | **On** shows a checkerboard behind transparent parts of the picture. **Off (default)** uses the normal background. |
| <a id="general-background-transparency-light-tile-red"></a>**Light Tile Red** | Sets the red channel of the light squares used by Blend Against Tiles. Values are normalized from 0 to 1. |
| <a id="general-background-transparency-light-tile-green"></a>**Light Tile Green** | Sets the green channel of the light squares used by Blend Against Tiles. Values are normalized from 0 to 1. |
| <a id="general-background-transparency-light-tile-blue"></a>**Light Tile Blue** | Sets the blue channel of the light squares used by Blend Against Tiles. Values are normalized from 0 to 1. |
| <a id="general-background-transparency-dark-tile-red"></a>**Dark Tile Red** | Sets the red channel of the dark squares used by Blend Against Tiles. Values are normalized from 0 to 1. |
| <a id="general-background-transparency-dark-tile-green"></a>**Dark Tile Green** | Sets the green channel of the dark squares used by Blend Against Tiles. Values are normalized from 0 to 1. |
| <a id="general-background-transparency-dark-tile-blue"></a>**Dark Tile Blue** | Sets the blue channel of the dark squares used by Blend Against Tiles. Values are normalized from 0 to 1. |
| <a id="general-background-transparency-tile-size-pixels"></a>**Tile Size (pixels)** | Sets the size/repeat scale of the transparency checkerboard in output pixels. Smaller values create a fine pattern; larger values create larger squares. |

<a id="general-display-mode"></a>

## Display Mode

Use only modes your display supports over its current connection. Leaving fullscreen or stopping playback restores the previous mode.

| Setting | What it does |
| --- | --- |
| <a id="general-display-mode-switch-display-mode-in-fullscreen"></a>**Switch Display Mode in Fullscreen** | **On** uses a suitable mode from Allowed Modes in fullscreen and restores the previous mode afterwards. **Off** keeps the current display mode. |
| <a id="general-display-mode-allowed-modes"></a>**Allowed Modes** | Lists the display modes that automatic fullscreen switching may select. Examples are 2160p23, 2160p24, 2160p50 and 2160p60; use the resolution appropriate for your display. |
| <a id="general-display-mode-pause-after-switching-seconds"></a>**Pause After Switching (seconds)** | Allows the display time to resynchronise after a fullscreen mode change before playback continues. |

<a id="general-diagnostics"></a>

## Diagnostics

| Setting | What it does |
| --- | --- |
| <a id="general-diagnostics-diagnostic-log"></a>**Diagnostic Log** | **On** logs the next player start for troubleshooting, then resets to Off. **Off** leaves the next player start unlogged. |
| <a id="general-diagnostics-save-report"></a>**Save Report** | Saves a diagnostic text report on the desktop. It combines renderer settings, system/player information and the available session log. |

<a id="general-themes"></a>

## Appearance

- **Theme** Choose Indigo, Graphite, Midnight, Ocean or Light.
- **Accent Color** Choose a color or use the Windows system accent.
- **Language** Auto follows Windows. Available languages depend on the build.
- **Remember Last Page** With **On**, the menu reopens at the last page, tab and scroll position. With **Off**, it opens the start view.

---

[← Home](Home.md) · [Processing →](Processing.md)
