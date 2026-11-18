**English** · [Deutsch](DE-Color-Management.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

**Menu path** Color > Calibration. Match colors to your display.

**On this page**

- [Everyday examples](#color-management-examples)
- [ICC Profile](#color-management-icc-profile)
- [3D LUT](#color-management-3d-lut)
- [Display](#color-management-display)

<a id="color-management-examples"></a>

## Everyday examples

Examples from everyday viewing. Change one setting at a time and compare the same scene.

| Situation | Setting | What to check |
| --- | --- | --- |
| You calibrated your monitor with a meter, for example with DisplayCAL. | Choose the [ICC Profile File](#color-management-icc-profile-icc-profile-file) and set [Enable ICC Profile](#color-management-icc-profile-enable-icc-profile) **On**. | Do not apply the same correction again with a 3D LUT or in the GPU driver. |
| Wide-gamut monitor: faces look red and grass glows neon green. | Set [Display Gamut](#color-management-display-display-gamut) to match, for example **P3-D65**. | Only in the monitor's native mode, not in its sRGB mode. |
| The TV is set to gamma 2.4 for a dark room. | [Display Gamma](#color-management-display-display-gamma) **2.40**. | The value must match the TV's setting. |
| OLED TV with Display Gamma **BT.1886**. | [Display Contrast](#color-management-display-display-contrast) **0**. | 0 stands for infinite contrast. |
| The monitor uses gamma 2.2, but movies should look like on a 2.4 display. | [Display Gamma](#color-management-display-display-gamma) **2.20**, [SDR Gamma Processing](#color-management-display-sdr-gamma-processing) **2.40**. | Dark scenes become slightly richer. |
| A movie should play with its own color LUT. | [LUT from Video Folder](#color-management-3d-lut-lut-from-video-folder) **On**, put **Movie.cube** next to **Movie.mkv**. | [Enable 3D LUT](#color-management-3d-lut-enable-3d-lut) must be on. |

<a id="color-management-icc-profile"></a>

## ICC Profile

An ICC profile describes how the display reproduces color. If several active corrections address the same issue, the effect can become too strong.

| Setting | What it does |
| --- | --- |
| <a id="color-management-icc-profile-icc-profile-file"></a>**ICC Profile File** | Selects an ICC/ICM profile describing the actual display/color space you want to render to. The profile is a description of color behavior, not an enhancement preset. |
| <a id="color-management-icc-profile-enable-icc-profile"></a>**Enable ICC Profile** | **On** uses a valid ICC profile to correct colors on the SDR output path. **Off** bypasses that ICC correction. |
| <a id="color-management-icc-profile-icc-rendering-intent"></a>**ICC Rendering Intent** | Chooses how the profile’s conversion handles colors near or outside the display gamut. Relative Colorimetric aims to retain in-gamut color relationships and fit out-of-gamut colors to a boundary; Perceptual uses the profile’s perceptual treatment of the overall image. |

<a id="color-management-3d-lut"></a>

## 3D LUT

A 3D LUT maps input colors to new colors. With **LUT from Video Folder**, a file such as **Film.cube** matches **Film.mkv**.

| Setting | What it does |
| --- | --- |
| <a id="color-management-3d-lut-lut-file"></a>**LUT File** | Selects an external three-dimensional .cube lookup table. A 3D LUT maps combinations of red, green and blue to new combinations, for calibration or an intentional look. |
| <a id="color-management-3d-lut-enable-3d-lut"></a>**Enable 3D LUT** | **On** applies the 3D LUT at the stage selected by LUT Type. **Off** bypasses the 3D LUT. |
| <a id="color-management-3d-lut-lut-type"></a>**LUT Type** | Determines where and in which RGB representation a manually typed LUT is applied. Default (SDR Calibration) keeps FrankyRenderer’s normal output-calibration slot. |
| <a id="color-management-3d-lut-lut-from-video-folder"></a>**LUT from Video Folder** | **On** looks for a matching .cube file beside the video, then for a single .cube file in the folder. **Off**, or no clear match, uses the chosen LUT file. **Enable 3D LUT** must be on. |

<a id="color-management-display"></a>

## Display

**Ctrl+J** shows the source gamma and, when available, the output gamma.

| Setting | What it does |
| --- | --- |
| <a id="color-management-display-display-gamut"></a>**Display Gamut** | Describes the display's gamut for renderer colour conversion. Supported choices include standard video gamuts, BT.2020 and P3 variants. |
| <a id="color-management-display-display-gamma"></a>**Display Gamma** | Describes how the display decodes the SDR signal. The menu includes Auto, gamma values from 1.80 to 2.80 and BT.1886. |
| <a id="color-management-display-display-contrast"></a>**Display Contrast** | Supplies the display contrast ratio used by the BT.1886 calibration curve. The black level affects the shape of that curve. |
| <a id="color-management-display-sdr-gamma-processing"></a>**SDR Gamma Processing** | Optionally gives SDR material a chosen gamma appearance on the calibrated display. Off retains the ordinary SDR processing route. |

---

[← HDR & Color Mapping](HDR-and-Color-Mapping.md) · [Shaders →](Shaders.md)
