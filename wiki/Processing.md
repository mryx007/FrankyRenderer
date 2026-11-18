**English** · [Deutsch](DE-Processing.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

**Menu path** Processing. To adjust the crop, see [Black Bars and Zoom](Black-Bars-and-Zoom.md).

**On this page**

- [Everyday examples](#processing-examples)
- [Debanding](#processing-debanding)
- [Adaptive Sharpen](#processing-adaptive-sharpen)
- [Deinterlacing](#processing-deinterlacing)
- [Dithering](#processing-dithering)

<a id="processing-examples"></a>

## Everyday examples

Examples from everyday viewing. Change one setting at a time and compare the same scene.

| Situation | Setting | What to check |
| --- | --- | --- |
| A blue sky or sunset shows rings instead of a smooth gradient. | [Debanding](#processing-debanding-debanding) **On**, keep the defaults. | Do skin, hair and film grain stay intact? |
| Dark scenes in streaming files look blotchy and stepped. | Debanding **On**, raise [Iterations](#processing-debanding-iterations) by 1. | Too much makes dark areas look waxy. |
| Old DVD or TV recording: edges show comb-like jaggies during motion. | Deinterlacing [Algorithm](#processing-deinterlacing-algorithm) **Bwdif**. | Scrolling text and motion should look smooth. |
| After deinterlacing, motion jitters back and forth. | Set [Field Order](#processing-deinterlacing-field-order) to the other order. | Only change it when **Automatic** is visibly wrong. |
| The picture looks a little soft on a large TV. | [Enhance Edges and Details](#processing-adaptive-sharpen-enhance-edges-and-details) **On**, [Strength](#processing-adaptive-sharpen-strength) around 0.3. | Bright halos around text or more noise mean it is too much. |
| A grainy old film gets noisier after sharpening. | Lower the strength or turn sharpening **Off**. | Film grain is intended and should not stand out. |
| A fine pattern flickers in dark areas. | [Change Dither for Every Frame](#processing-dithering-change-dither-for-every-frame) **Off**. | The pattern then stays still. |

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
