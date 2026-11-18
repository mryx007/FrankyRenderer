**English** · [Deutsch](DE-Shaders.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

Bundled and custom shaders and the order they run in.

**On this page**

- [Chain Controls](#shaders-chain-controls)
- [AMD CAS](#shaders-amd-cas)
- [FineDehalo](#shaders-finedehalo)
- [Anti-Ringing](#shaders-anti-ringing)
- [PixelClipper](#shaders-pixelclipper)
- [KrigBilateral](#shaders-krigbilateral)
- [CfL Prediction](#shaders-cfl-prediction)
- [NLMeans](#shaders-nlmeans)
- [Bilateral Denoise](#shaders-bilateral-denoise)
- [Chroma Denoise](#shaders-chroma-denoise)
- [hdeband](#shaders-hdeband)
- [neo-f3kdb](#shaders-neo-f3kdb)
- [Guided Detail Enhance](#shaders-guided-detail-enhance)
- [Clarity](#shaders-clarity)
- [FSRCNNX 2x](#shaders-fsrcnnx-2x)
- [AiUpscale Photo 2x](#shaders-aiupscale-photo-2x)
- [RAVU Zoom AR R2](#shaders-ravu-zoom-ar-r2)
- [Anime4K CNN 2x M](#shaders-anime4k-cnn-2x-m)
- [ArtCNN C4F16](#shaders-artcnn-c4f16)
- [CuNNy 2x12 DS](#shaders-cunny-2x12-ds)
- [SSimDownscaler](#shaders-ssimdownscaler)
- [SSimDownscaler_AR](#shaders-ssimdownscaler-ar)
- [DPID](#shaders-dpid)
- [RAVU Zoom R3](#shaders-ravu-zoom-r3)
- [RAVU Zoom AR R3](#shaders-ravu-zoom-ar-r3)
- [Shader list and custom files](#shaders-shader-list-and-custom-files)

<a id="shaders-chain-controls"></a>

## Chain Controls

The processing stage determines when a shader runs. Some run only when enlarging or reducing the picture. Compare one effect at a time.

| Setting | What it does |
| --- | --- |
| <a id="shaders-chain-controls-disable-all-shaders"></a>**Disable All Shaders** | Turns off the whole bundled shader chain while retaining stored parameter overrides and the order. In the ordinary shader page this action is presented as All Off. |

<a id="shaders-chain-controls-automatic-order"></a>

### Automatic Order

**On** orders compatible shaders within their processing stage. **Off** lets you choose the positions of enabled shaders manually.

<a id="shaders-amd-cas"></a>

## AMD CAS

<a id="shaders-amd-cas-enabled"></a>

Contrast-adaptive sharpening. Compare fine texture and hard edges.

| Setting | What it does |
| --- | --- |
| <a id="shaders-amd-cas-strength-str"></a>**Strength (STR)** | Higher strength makes edges and textures clearer. Too much accentuates noise and creates halos. |

<a id="shaders-amd-cas-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-finedehalo"></a>

## FineDehalo

<a id="shaders-finedehalo-enabled"></a>

Reduces halos at edges. Check light outlines around dark objects.

| Setting | What it does |
| --- | --- |
| <a id="shaders-finedehalo-horizontal-radius-rx"></a>**Horizontal Radius (RX)** | Sets the horizontal neighborhood used to identify and reduce halos around edges. Larger values address wider halos and can affect a wider area of genuine detail. |
| <a id="shaders-finedehalo-vertical-radius-ry"></a>**Vertical Radius (RY)** | Sets the vertical neighborhood used to identify and reduce halos around edges. Larger values address wider halos and can affect a wider area of genuine detail. |
| <a id="shaders-finedehalo-strong-edge-low-thmi"></a>**Strong Edge Low (THMI)** | Lower threshold of the strong-edge mask. Values below this part of the transition are treated as weaker edges; the upper threshold completes the transition. |
| <a id="shaders-finedehalo-strong-edge-high-thma"></a>**Strong Edge High (THMA)** | Upper threshold of the strong-edge mask. Together with Strong Edge Low it controls which edges receive strong-edge treatment. |
| <a id="shaders-finedehalo-light-edge-low-thlimi"></a>**Light Edge Low (THLIMI)** | Lower threshold in the lighter-edge mask used to separate/protect edge structures during halo removal. It changes mask selection rather than globally increasing brightness. |
| <a id="shaders-finedehalo-light-edge-high-thlima"></a>**Light Edge High (THLIMA)** | Upper threshold of the lighter-edge mask’s transition. Use it together with Light Edge Low; a reversed or collapsed interval is not a useful normal setup. |
| <a id="shaders-finedehalo-dark-halo-strength-str-d"></a>**Dark Halo Strength (STR_D)** | Amount of correction for dark halos beside an edge. A dark halo is an unnaturally dark outline outside the original boundary. |
| <a id="shaders-finedehalo-bright-halo-strength-str-b"></a>**Bright Halo Strength (STR_B)** | Amount of correction for bright halos. These can appear as light outlines after excessive sharpening or in already processed sources. |
| <a id="shaders-finedehalo-low-sensitivity-sens-l"></a>**Low Sensitivity (SENS_L)** | Adjusts the shader’s low-side sensitivity in the edge/mask processing. This is a mask-shaping control, not a general sharpening strength. |
| <a id="shaders-finedehalo-high-sensitivity-sens-h"></a>**High Sensitivity (SENS_H)** | Adjusts the high-side sensitivity of the edge/mask processing. It interacts with the mask thresholds, so its effect cannot be described as a fixed percentage of halo removal. |
| <a id="shaders-finedehalo-debug-view-debug"></a>**Debug View (DEBUG)** | Replaces normal output with intermediate information: 0 is the picture; 1 shows the outside mask; 2 the shrink/smooth intermediate; 3 edge detection; 4 the strong mask. These are diagnostic images, not viewing modes. |

<a id="shaders-finedehalo-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-anti-ringing"></a>

## Anti-Ringing

<a id="shaders-anti-ringing-enabled"></a>

Reduces bright or dark outlines left by scaling.

| Setting | What it does |
| --- | --- |
| <a id="shaders-anti-ringing-sample-taps-tap"></a>**Sample Taps (TAP)** | Changes the neighborhood sampled to estimate local image limits. A larger neighborhood changes what counts as an acceptable edge value and increases sampling work. |
| <a id="shaders-anti-ringing-strength-str"></a>**Strength (STR)** | Blends the anti-ringing correction into the image. Higher values constrain overshoot more strongly, potentially reducing the crispness produced by the scaler. |

<a id="shaders-anti-ringing-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-pixelclipper"></a>

## PixelClipper

<a id="shaders-pixelclipper-enabled"></a>

Limits local pixel overshoot. Check fine edges and highlights.

| Setting | What it does |
| --- | --- |
| <a id="shaders-pixelclipper-image-sampling-mode-taps-scale"></a>**Image Sampling Mode (TAPS_scale)** | **Off** uses four samples to estimate image-edge limits. **On** uses twelve to cover a larger area at extra GPU cost. |
| <a id="shaders-pixelclipper-chroma-sampling-mode-taps-cscale"></a>**Chroma Sampling Mode (TAPS_cscale)** | **Off** uses four samples to estimate color-edge limits. **On** uses twelve and costs more GPU time. |
| <a id="shaders-pixelclipper-upscaling-limit-clip-scale"></a>**Upscaling Limit (CLIP_scale)** | Controls the clipping correction for image enlargement. It constrains scaling overshoot relative to a sampled local neighborhood. |
| <a id="shaders-pixelclipper-chroma-limit-clip-cscale"></a>**Chroma Limit (CLIP_cscale)** | Controls the clipping correction for chroma enlargement. It constrains scaling overshoot relative to a sampled local neighborhood. |
| <a id="shaders-pixelclipper-downscaling-limit-clip-dscale"></a>**Downscaling Limit (CLIP_dscale)** | Controls the clipping correction for image reduction. It constrains scaling overshoot relative to a sampled local neighborhood. |

<a id="shaders-pixelclipper-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-krigbilateral"></a>

## KrigBilateral

<a id="shaders-krigbilateral-enabled"></a>

Reconstructs color resolution using edges as a guide.

<a id="shaders-krigbilateral-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-cfl-prediction"></a>

## CfL Prediction

<a id="shaders-cfl-prediction-enabled"></a>

Uses brightness as a guide when reconstructing color resolution at edges.

| Setting | What it does |
| --- | --- |
| <a id="shaders-cfl-prediction-chroma-offset-x-chroma-offset-x"></a>**chroma offset x (chroma_offset_x)** | Offsets the luma reference in the horizontal direction when predicting chroma. This can compensate for a real alignment mismatch, but also introduces a mismatch if the source was already correct. |
| <a id="shaders-cfl-prediction-chroma-offset-y-chroma-offset-y"></a>**chroma offset y (chroma_offset_y)** | Offsets the luma reference in the vertical direction when predicting chroma. This can compensate for a real alignment mismatch, but also introduces a mismatch if the source was already correct. |
| <a id="shaders-cfl-prediction-distance-coeff-distance-coeff"></a>**distance coeff (distance_coeff)** | Controls how quickly reference weight falls with spatial distance. Higher values favor nearby samples more strongly; lower values let more distant samples contribute. |
| <a id="shaders-cfl-prediction-intensity-coeff-intensity-coeff"></a>**intensity coeff (intensity_coeff)** | Controls how strongly a brightness mismatch reduces reference weight. Higher values make the prediction more selective about luma similarity, which can help protect boundaries but changes how much evidence is averaged. |

<a id="shaders-cfl-prediction-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-nlmeans"></a>

## NLMeans

<a id="shaders-nlmeans-enabled"></a>

Reduces noise using similar image areas. Check whether grain and fine textures survive.

| Setting | What it does |
| --- | --- |
| <a id="shaders-nlmeans-reference-mode-ref"></a>**Reference Mode (REF)** | **Off** uses the basic NLMeans method. **On** adds an internal reference calculation to guide denoising. |

<a id="shaders-nlmeans-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-bilateral-denoise"></a>

## Bilateral Denoise

<a id="shaders-bilateral-denoise-enabled"></a>

Reduces noise while accounting for differences in brightness or color across edges.

| Setting | What it does |
| --- | --- |
| <a id="shaders-bilateral-denoise-sample-taps-tap"></a>**Sample Taps (TAP)** | Controls the reach of the sampled neighborhood. The shader loops around the center in both directions; a value of 5 does not mean only five total texture samples. |
| <a id="shaders-bilateral-denoise-ss-ss"></a>**SS (SS)** | Spatial similarity width. Higher values allow farther samples to retain more weight; lower values concentrate the effect near the center. |
| <a id="shaders-bilateral-denoise-sr-sr"></a>**SR (SR)** | Range/intensity similarity width. Higher values let samples with larger brightness differences mix together. |

<a id="shaders-bilateral-denoise-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-chroma-denoise"></a>

## Chroma Denoise

<a id="shaders-chroma-denoise-enabled"></a>

Reduces color noise. Check fine color transitions.

| Setting | What it does |
| --- | --- |
| <a id="shaders-chroma-denoise-threshold-thr"></a>**Threshold (THR)** | Controls the chroma-noise threshold in the shader’s 0–255 convention. It is intended for unwanted color speckles or color variation, rather than primarily removing monochrome grain. |

<a id="shaders-chroma-denoise-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-hdeband"></a>

## hdeband

<a id="shaders-hdeband-enabled"></a>

Smooths steps in gradients. Check dark transitions and film grain.

| Setting | What it does |
| --- | --- |
| <a id="shaders-hdeband-s-s"></a>**S (S)** | Sets the spatial falloff of reference weights. Larger values reduce the contribution of distant samples more strongly. |
| <a id="shaders-hdeband-si-si"></a>**SI (SI)** | Sets the intensity-difference falloff. Higher values are more selective about brightness similarity and reduce mixing between unlike samples. |
| <a id="shaders-hdeband-sr-sr"></a>**SR (SR)** | Sets a floor for continuation of the directional search. Higher values allow the search to continue more readily beyond samples that fail the run test. |
| <a id="shaders-hdeband-sw-sw"></a>**SW (SW)** | Sets the center/self sample’s weight. Higher values anchor the result more strongly to the original pixel, usually reducing how much surrounding samples can change it. |
| <a id="shaders-hdeband-radius-radius"></a>**Radius (RADIUS)** | Controls how many outward search steps the shader can take. Sparsity changes the spacing of those steps, so this number is not by itself the maximum physical distance in pixels. |
| <a id="shaders-hdeband-sparsity-sparsity"></a>**SPARSITY (SPARSITY)** | Controls gaps between successive reference positions. Larger values cover a wider area with the same number of search steps, rather than adding more densely spaced samples. |
| <a id="shaders-hdeband-directions-directions"></a>**DIRECTIONS (DIRECTIONS)** | Selects how many search directions are used. The shader has up to eight cardinal/diagonal directions. |
| <a id="shaders-hdeband-run-start-run-start"></a>**RUN START (RUN_START)** | **On** starts the search by assuming a smooth run. **Off** makes the shader establish it from the first samples. |
| <a id="shaders-hdeband-tolerance-tolerance"></a>**TOLERANCE (TOLERANCE)** | Sets how much adjacent sample variation can still count as a flat run. A larger tolerance accepts more small variation, potentially treating texture or noise as part of a band. |

<a id="shaders-hdeband-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-neo-f3kdb"></a>

## neo-f3kdb

<a id="shaders-neo-f3kdb-enabled"></a>

Another way to reduce banding, with optional grain.

| Setting | What it does |
| --- | --- |
| <a id="shaders-neo-f3kdb-range-range"></a>**RANGE (RANGE)** | Sets the spatial reach of randomly chosen reference samples. Larger ranges can address broader band structures but may cross real image boundaries. |
| <a id="shaders-neo-f3kdb-grain-grain"></a>**Grain (GRAIN)** | Adds synthetic grain as part of this shader. It can disguise remaining gradient steps, but it also changes a clean image and is not restored original film texture. |
| <a id="shaders-neo-f3kdb-grad-grad"></a>**GRAD (GRAD)** | **On** adapts the thresholds to the local gradient. **Off** uses the configured thresholds directly. |
| <a id="shaders-neo-f3kdb-average-threshold-thr-avg"></a>**Average Threshold (THR_AVG)** | Threshold for the difference between the center and the averaged references. Larger thresholds admit more differences, increasing correction and the risk of smoothing texture. |
| <a id="shaders-neo-f3kdb-maximum-threshold-thr-max"></a>**Maximum Threshold (THR_MAX)** | Threshold involving the most different reference sample. It helps prevent a very different neighbor from contributing to the correction. |
| <a id="shaders-neo-f3kdb-middle-threshold-thr-mid"></a>**Middle Threshold (THR_MID)** | Threshold for a paired-reference/second-difference test. It helps distinguish a smooth local relationship from a structure that should be preserved. |
| <a id="shaders-neo-f3kdb-combined-threshold-thr-all"></a>**Combined Threshold (THR_ALL)** | **On** uses Average Threshold for the maximum and paired checks too. **Off** gives each check its own threshold. |
| <a id="shaders-neo-f3kdb-ab-ab"></a>**AB (AB)** | Multiplier for the gradient-aware threshold boost. With gradient adaptation active, it changes how permissive the shader becomes where gradients meet its consistency test. |
| <a id="shaders-neo-f3kdb-am-am"></a>**AM (AM)** | Tolerance used when comparing gradient directions for the adaptive boost. It is an internal normalized comparison, not an angle entered in degrees. |

<a id="shaders-neo-f3kdb-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-guided-detail-enhance"></a>

## Guided Detail Enhance

<a id="shaders-guided-detail-enhance-enabled"></a>

Brings out local detail. Watch for changes to textures and outlines.

| Setting | What it does |
| --- | --- |
| <a id="shaders-guided-detail-enhance-radius-rad"></a>**Radius (RAD)** | Sets the neighborhood of the guided-filter base. A wider base changes the scale of detail separated from it and increases the region considered. |
| <a id="shaders-guided-detail-enhance-regularization-eps"></a>**Regularization (EPS)** | Controls how strongly the guided base follows local edges. A larger regularization makes the base smoother and less responsive to small variations, changing the extracted detail. |
| <a id="shaders-guided-detail-enhance-subsampling-subsam"></a>**Subsampling (SUBSAM)** | Changes the subsampling used in the guided-filter computation. Higher values trade spatial precision for less work in parts of the calculation. |
| <a id="shaders-guided-detail-enhance-strength-str"></a>**Strength (STR)** | Controls how much extracted detail is added back. Higher values can emphasize texture, noise and halos together. |

<a id="shaders-guided-detail-enhance-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-clarity"></a>

## Clarity

<a id="shaders-clarity-enabled"></a>

Enhances local contrast. Shadows and highlights have separate strength controls.

| Setting | What it does |
| --- | --- |
| <a id="shaders-clarity-radius-rad"></a>**Radius (RAD)** | Selects the blur-neighborhood preset used to isolate local contrast detail. The integer is a shader preset rather than a direct physical pixel radius. |
| <a id="shaders-clarity-shift-shift"></a>**SHIFT (SHIFT)** | Multiplies sample offsets in the blur calculation, changing the spatial scale of the local-contrast effect. Larger offsets reach farther from each pixel. |
| <a id="shaders-clarity-strength-str"></a>**Strength (STR)** | Overall amount of local contrast enhancement. Clarity can make shapes and texture stand out, but strong settings can alter the intended soft appearance and emphasize compression. |
| <a id="shaders-clarity-shadow-strength-strd"></a>**Shadow Strength (STRD)** | Controls the clarity contribution in darker regions. More correction can expose noise and disturb smooth dark gradients. |
| <a id="shaders-clarity-highlight-strength-strl"></a>**Highlight Strength (STRL)** | Controls the contribution in bright regions. Highlights can show halos or excessive texture if local contrast is pushed too far. |
| <a id="shaders-clarity-bm-bm"></a>**BM (BM)** | Selects the blend formula: 1 Soft Light, 2 Overlay, 3 Hard Light, 4 Multiply, 5 Vivid Light, 6 Linear Light, 7 Addition. The numbers are modes, not levels of quality or strength. |
| <a id="shaders-clarity-cd-cd"></a>**CD (CD)** | Dark-side tonal-protection boundary in the shader’s 0–255 convention. The mask fades the effect in dark values relative to this boundary. |
| <a id="shaders-clarity-cl-cl"></a>**CL (CL)** | Light-side tonal-protection boundary. Values toward the bright end receive a faded effect relative to this boundary. |
| <a id="shaders-clarity-debug-view-debug"></a>**Debug View (DEBUG)** | Selects 0 for the normal picture, 1 for the extracted clarity/detail intermediate, or 2 for the protection mask. A gray or unusual image in these modes is expected. |

<a id="shaders-clarity-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-fsrcnnx-2x"></a>

## FSRCNNX 2x

<a id="shaders-fsrcnnx-2x-enabled"></a>

Neural upscaling for 2× enlargement.

<a id="shaders-fsrcnnx-2x-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-aiupscale-photo-2x"></a>

## AiUpscale Photo 2x

<a id="shaders-aiupscale-photo-2x-enabled"></a>

2× upscaling for photographic material, with optional sharpening.

| Setting | What it does |
| --- | --- |
| <a id="shaders-aiupscale-photo-2x-sharpen-sharp"></a>**Sharpen (SHARP)** | **On** adds sharpening during upscaling. Text or skin can look harsher. **Off** uses the normal upscaling variant. |

<a id="shaders-aiupscale-photo-2x-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-ravu-zoom-ar-r2"></a>

## RAVU Zoom AR R2

<a id="shaders-ravu-zoom-enabled"></a>

<a id="shaders-ravu-zoom"></a>

Upscales with edge protection. Check fine outlines for halos.

<a id="shaders-ravu-zoom-ar-r2-chain-position"></a>

<a id="shaders-ravu-zoom-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-anime4k-cnn-2x-m"></a>

## Anime4K CNN 2x M

<a id="shaders-anime4k-cnn-2x-m-enabled"></a>

Neural upscaling for drawn or animated material.

<a id="shaders-anime4k-cnn-2x-m-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-artcnn-c4f16"></a>

## ArtCNN C4F16

<a id="shaders-artcnn-c4f16-enabled"></a>

Neural upscaling. Check line art, color edges and GPU time.

<a id="shaders-artcnn-c4f16-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-cunny-2x12-ds"></a>

## CuNNy 2x12 DS

<a id="shaders-cunny-2x12-ds-enabled"></a>

Neural upscaling. Check fine lines and GPU time.

<a id="shaders-cunny-2x12-ds-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-ssimdownscaler"></a>

## SSimDownscaler

<a id="shaders-ssimdownscaler-enabled"></a>

Uses local image detail when reducing size. Try Mitchell as a starting point. Oversharp (THR) adds sharpening.

| Setting | What it does |
| --- | --- |
| <a id="shaders-ssimdownscaler-oversharp-thr"></a><a id="shaders-ssimdownscaler-threshold-thr"></a>**Oversharp (THR)** | Adjusts the oversharpening-related term in the structural correction and the threshold used by its debug view. It is not a generic noise threshold. |
| <a id="shaders-ssimdownscaler-debug-view-debug"></a>**Debug View (DEBUG)** | **On** shows the correction in red and blue for diagnostics. **Off** shows the normally scaled video. |

<a id="shaders-ssimdownscaler-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-ssimdownscaler-ar"></a>

## SSimDownscaler_AR

This bundled SSimDownscaler variant reduces halos at hard edges. Its edge protection uses [Global Anti-Ringing](Scaling.md#scaling-filters-global-anti-ringing). **Oversharp (THR)** adds sharpening. Compare at the same output size.

<a id="shaders-dpid"></a>

## DPID

<a id="shaders-dpid-enabled"></a>

Preserves detail when reducing the image. Check patterns in motion.

| Setting | What it does |
| --- | --- |
| <a id="shaders-dpid-lbd-lbd"></a>**LBD (LBD)** | Exponent controlling how strongly sample differences affect the detail-preserving weights. Zero gives uniform weights in this part of the calculation; larger values emphasize samples that differ more from the local reference. |

<a id="shaders-dpid-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-ravu-zoom-r3"></a>

## RAVU Zoom R3

RAVU upscaling with a radius of 3.

<a id="shaders-ravu-zoom-r3-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-ravu-zoom-ar-r3"></a>

## RAVU Zoom AR R3

RAVU upscaling with a radius of 3 and anti-ringing.

<a id="shaders-ravu-zoom-ar-r3-chain-position"></a>

The [chain order](#shaders-chain-controls-automatic-order) sets where the enabled shader runs within its processing stage.

<a id="shaders-shader-list-and-custom-files"></a>

## Shader list and custom files

<a id="eigene-shader"></a>

Switch shaders on and off in the list. **All Off** helps you compare. **Reset Shader** restores the selected shader’s settings. **Ctrl+J** shows shaders that are still preparing.

## Adding your own shaders

Place mpv-compatible **.glsl** or **.hook** files in **shaders\Custom** beside FrankyRenderer.ax. For packs with an **input.conf**, unpack the whole archive and keep its folder structure. The renderer shows the available parameters. The shader must support the current video and processing stage.

Bundled shaders are in **shaders\PlayKit**. Removing a file from that folder removes it from the list. A package update can restore bundled files.

---

[← Color Management](Color-Management.md) · [Sources →](Sources.md)
