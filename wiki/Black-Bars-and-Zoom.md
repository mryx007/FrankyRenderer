**English** · [Deutsch](DE-Black-Bars-and-Zoom.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

**Menu path** Processing > Black Bars / Zoom.

Adjust the crop and keep burned-in subtitles visible.

**On this page**

- [Everyday examples](#black-bars-zoom-examples)
- [Detection & Zoom](#black-bars-zoom-detection-zoom)
- [Detection and changing aspect ratios](#black-bars-zoom-detection-and-changing-aspect-ratios)

<a id="black-bars-zoom-examples"></a>

## Everyday examples

Examples from everyday viewing. Change one setting at a time and compare the same scene.

| Situation | Setting | What to check |
| --- | --- | --- |
| Widescreen movie (2.39:1) on a 16:9 TV. | [Zoom Mode](#black-bars-zoom-detection-zoom-zoom-mode) **Fit**. | Bars remain at the top and bottom, but nothing is cut off. |
| You want to fill the whole screen without bars. | [Zoom Mode](#black-bars-zoom-detection-zoom-zoom-mode) **Fill**. | The left and right edges are lost, such as people at the edge. |
| 21:9 monitor or wide projection screen: the movie has bars inside the video and appears small in the middle. | [Detect Hardcoded Black Bars](#black-bars-zoom-detection-zoom-detect-hardcoded-black-bars) and [Crop Black Bars](#black-bars-zoom-detection-zoom-crop-black-bars) **On**. | The movie then fills the full width. |
| Subtitles sit in the bottom bar and disappear after cropping. | [Protect Burned-in Subtitles](#black-bars-zoom-detection-zoom-protect-burned-in-subtitles) **On**. | Separate subtitle tracks are drawn by the player. |
| You want one large bar at the bottom for subtitles instead of two small bars. | [Vertical Image Position](#black-bars-zoom-detection-zoom-vertical-image-position) **Top**. | The picture moves to the top edge. |
| Dark night scenes are cropped by mistake. | Lower [Black Threshold](#black-bars-zoom-detection-zoom-black-threshold), for example from 2 to 1. | Very dark picture areas should stay visible. |
| Dark gray bars are not detected. | Raise [Black Threshold](#black-bars-zoom-detection-zoom-black-threshold), for example to 4. | Too high treats dark picture areas as bars. |
| Movies with changing formats, such as IMAX scenes, keep jumping in zoom. | Raise [Detection Stability](#black-bars-zoom-detection-zoom-detection-stability-seconds), for example to 5 seconds. | Longer looks calmer but reacts later to a real format change. |

<a id="black-bars-zoom-detection-zoom"></a>

## Detection & Zoom

| Setting | What it does |
| --- | --- |
| <a id="black-bars-zoom-detection-zoom-vertical-image-position"></a>**Vertical Image Position** | Aligns the picture at the top or bottom. With **Protect Burned-in Subtitles** enabled, detected subtitles in the bars can stay visible. |
| <a id="black-bars-zoom-detection-zoom-detect-hardcoded-black-bars"></a>**Detect Hardcoded Black Bars** | **On** looks for black bars encoded in the video picture. **Off** leaves automatic bar detection disabled. |
| <a id="black-bars-zoom-detection-zoom-crop-black-bars"></a>**Crop Black Bars** | **On** crops detected bars. **Off** lets detection identify the picture without applying automatic cropping. Automatic cropping requires bar detection. |
| <a id="black-bars-zoom-detection-zoom-protect-burned-in-subtitles"></a>**Protect Burned-in Subtitles** | **On** can restore detected burned-in subtitles from hidden bars. **Off** leaves subtitles in hidden bars out of view. |
| <a id="black-bars-zoom-detection-zoom-keep-a-stable-zoom"></a><a id="black-bars-zoom-detection-zoom-zoom-mode"></a>**Zoom Mode** | **Fit** keeps the whole active picture. **Fill** covers the output area and can crop picture content. |
| <a id="black-bars-zoom-detection-zoom-black-threshold"></a>**Black Threshold** | Raise it to detect dark gray bars. Too high can mistake dark picture content for a bar. |
| <a id="black-bars-zoom-detection-zoom-detection-stability-seconds"></a>**Detection Stability (seconds)** | A longer wait reduces unwanted crop changes. A shorter wait reacts sooner to changing formats. |

<a id="black-bars-zoom-detection-and-changing-aspect-ratios"></a>

## Detection and changing aspect ratios

Detection looks for bars encoded in the video. When an opening logo gives way to the main feature, it waits briefly before changing the crop. The old **Keep a Stable Zoom** option has been removed.

With a widescreen film on a 16:9 screen, **Fit** keeps the whole picture. **Fill** can crop the sides. Subtitle protection can restore detected text at the edge of the screen. The player handles separate subtitle tracks.

---

[← Scaling](Scaling.md) · [Color Adjustment →](Color-Adjustment.md)
