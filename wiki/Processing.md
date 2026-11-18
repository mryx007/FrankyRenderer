**English** · [Deutsch](DE-Processing.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

**Menu path** Processing. To adjust the crop, see [Black Bars and Zoom](Black-Bars-and-Zoom.md).

**On this page**

- [Debanding](#processing-debanding)
- [Adaptive Sharpen](#processing-adaptive-sharpen)
- [Deinterlacing](#processing-deinterlacing)
- [Dithering](#processing-dithering)

<a id="processing-debanding"></a>

## Debanding

Debanding helps with visible steps in skies. [Dithering](#processing-dithering) helps with steps introduced by the output.

| Setting | What it does |
| --- | --- |
| <a id="processing-debanding-debanding"></a>**Debanding** | **On** smooths visible steps in gradients such as skies. Strong settings can soften texture. **Off** skips this smoothing. |
| <a id="processing-debanding-iterations"></a>**Iterations** | Controls how many debanding passes are used. Additional passes can attack stronger or broader contouring, but increase processing work and can soften small details. |
| <a id="processing-debanding-threshold"></a>**Threshold** | Raise it to smooth stronger gradient steps. Too high can blur real color boundaries or fine textures. |
| <a id="processing-debanding-radius"></a>**Radius** | Raise it to include more distant pixels. This can smooth wider bands and soften fine structures. |
| <a id="processing-debanding-grain"></a>**Grain** | Adds noise after smoothing to mask remaining steps. Higher values make the grain more visible. |

<a id="processing-adaptive-sharpen"></a>

## Adaptive Sharpen

| Setting | What it does |
| --- | --- |
| <a id="processing-adaptive-sharpen-enhance-edges-and-details"></a>**Enhance Edges and Details** | **On** makes edges and details clearer. Too much strength can emphasize noise and halos. **Off** skips this sharpening step. |
| <a id="processing-adaptive-sharpen-strength"></a>**Strength** | Sets the amount of built-in adaptive sharpening. Zero removes the sharpening effect. |

<a id="processing-deinterlacing"></a>

## Deinterlacing

| Setting | What it does |
| --- | --- |
| <a id="processing-deinterlacing-algorithm"></a>**Algorithm** | Bwdif and Yadif turn interlaced fields into complete frames. Try Bwdif when movement in old TV recordings shows comb-like edges. Off skips this processing. |
| <a id="processing-deinterlacing-field-order"></a>**Field Order** | Determines which field is considered earlier in time. Auto follows the source’s field flags. |

<a id="processing-dithering"></a>

## Dithering

| Setting | What it does |
| --- | --- |
| <a id="processing-dithering-algorithm"></a>**Algorithm** | Blue Noise masks gradient steps with a fine pattern. Ordered Dithering can show a grid. Error diffusion uses more GPU time and can fall back to Blue Noise at high resolutions. **Off** skips dithering. |
| <a id="processing-dithering-change-dither-for-every-frame"></a>**Change Dither for Every Frame** | **On** changes the pattern every frame. This hides a fixed pattern and can cause shimmer. **Off** keeps the pattern fixed. Available with Blue Noise, Ordered and Random Dithering. |

---

[← General](General.md) · [Scaling →](Scaling.md)
