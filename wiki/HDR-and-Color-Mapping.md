**English** · [Deutsch](DE-HDR-and-Color-Mapping.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

**Menu path** HDR. Tone mapping fits brightness into the target range. Gamut mapping fits colors.

**On this page**

- [HDR Output](#hdr-color-mapping-hdr-output)
  - [Which tone mapping function should I choose?](#choosing-a-tone-mapping-function)
- [HDR Peak Detection](#hdr-color-mapping-hdr-peak-detection)
- [Tone Mapping Knee](#hdr-color-mapping-tone-mapping-knee)
- [Tone Mapping Constants](#hdr-color-mapping-tone-mapping-constants)
- [Contrast Recovery](#hdr-color-mapping-contrast-recovery)
- [Gamut Mapping Constants](#hdr-color-mapping-gamut-mapping-constants)
- [Mapping LUT Quality](#hdr-color-mapping-mapping-lut-quality)
- [Mapping Visualization](#hdr-color-mapping-mapping-visualization)
- [Expansion & Metadata](#hdr-color-mapping-expansion-metadata)
- [Shadows](#hdr-color-mapping-shadows)
- [Midtones](#hdr-color-mapping-midtones)
- [Highlights](#hdr-color-mapping-highlights)
- [Tone Controls](#hdr-color-mapping-tone-controls)

<a id="hdr-color-mapping-hdr-output"></a>

## HDR Output

| Goal | Mode |
| --- | --- |
| Let the display handle HDR | Pass HDR Through |
| Fit HDR highlights to the display’s peak | Tone Map HDR to Display Peak |
| View HDR on an SDR screen | Tone Map HDR to SDR |

**Ctrl+J** shows the active HDR path and the selected tone mapping curve.

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-hdr-output-hdr-mode"></a>**HDR Mode** | Passes HDR to the display, fits highlights to the display’s peak brightness, or converts HDR to SDR. |
| <a id="hdr-color-mapping-hdr-output-clipping-protection"></a><a id="hdr-color-mapping-hdr-output-sdr-target-brightness-nits"></a>**SDR Target Brightness (nits)** | Sets the nominal target/reference luminance used for SDR rendering and HDR-to-SDR mapping. A nit is one candela per square metre. |
| <a id="hdr-color-mapping-hdr-output-tone-mapping-function"></a>**Tone Mapping Function** | Controls how bright areas fit into the target brightness range. The [guide below](#choosing-a-tone-mapping-function) shows when to choose each curve. |
| <a id="hdr-color-mapping-hdr-output-gamut-mapping-function"></a>**Gamut Mapping Function** | Fits source colors into the range the output display can reproduce. |
| <a id="hdr-color-mapping-hdr-output-hdr-target-peak-0-auto"></a>**HDR Target Peak (0 = Auto)** | Sets the peak brightness used by HDR tone mapping. 0 uses the value reported by Windows. The display’s own brightness setting is separate. |
| <a id="hdr-color-mapping-hdr-output-switch-windows-hdr"></a>**Switch Windows HDR** | **Do Not Change** leaves Windows HDR as set. The Turn On modes can enable it for HDR playback. The Turn On/Off modes can also disable it for SDR. The fullscreen variants only turn it on in fullscreen. The original state is restored after playback. |
| <a id="hdr-color-mapping-hdr-output-rtx-video-hdr"></a>**RTX Video HDR** | **On** requests SDR-to-HDR conversion from the NVIDIA driver. Windows HDR and RTX Video HDR in the NVIDIA app must be enabled. **Off** skips this driver conversion. |
| <a id="hdr-color-mapping-hdr-output-use-dolby-vision"></a>**Use Dolby Vision** | **On** processes Dolby Vision. **Off** plays the compatible HDR10 or HLG base layer of Profiles 7 and 8. Profile 5 uses Dolby Vision in both states. |

<a id="choosing-a-tone-mapping-function"></a>

### Which tone mapping function should I choose?

**Start with Auto (Metadata).** For HDR on an SDR display, compare it with **BT.2446A**. With HDR passthrough, the display handles tone mapping.

| Function | When to use it and what changes |
| --- | --- |
| **Auto (Metadata)** | First choice. Usually uses Spline and switches to ST 2094-40 when suitable HDR10+ metadata arrives. Check **Ctrl+J → Tone mapping** for the selected curve. |
| **Spline** | To use one curve across sources, including HDR10+. It smooths highlight transitions. **Spline Contrast** trades stronger midtones against highlight and shadow detail. |
| **BT.2390** | When clouds or reflections are too bright on an HDR screen and the rest already looks right. It rolls off bright peaks gently. |
| **BT.2446A** | For HDR→SDR. Compare with Auto if faces look too dark or bright surfaces lose detail. |
| **Reinhard** | For gradual compression. It softens peaks and can subdue midtones. |
| **Mobius** | If Reinhard changes too much of the image. It largely preserves darker values up to an adjustable knee. |
| **Hable** | For a filmic brightness curve. It can darken faces and shadows noticeably. |
| **ST 2094-40 (HDR10+)** | Uses the supplied HDR10+ curve and adapts it to the target peak. libplacebo uses a fallback when that curve is missing. |
| **ST 2094-10** | An alternative for scene comparisons. It uses scene-average brightness to shape the curve. |
| **Clip** | To see what detail is lost without a smooth roll-off. Everything above the target limit is capped at the same brightness. |
| **Gamma** | An older comparison curve. It has a more abrupt transition and can make the image look flatter. |
| **Linear** | Scales the whole range on the perceptual HDR scale (PQ). This can noticeably change midtones too. |
| **Linear Light** | For small target-peak changes. It scales luminance proportionally and can make HDR-to-SDR output very dark. |

**Quick test:** Pause on a face next to a bright window. Change only the curve. Do skin tones look natural, and can you still see detail in the window? [libplacebo tone mapping documentation](https://github.com/haasn/libplacebo/blob/29a56f9290bacc3f7c6bd899ac7a64ec4e68c8e2/docs/options.md#tone_mappingfunction)

<a id="hdr-color-mapping-hdr-peak-detection"></a>

## HDR Peak Detection

Smoothing and scene thresholds control how quickly the measured peak reacts to brightness changes.

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-hdr-peak-detection-show-clipping"></a><a id="hdr-color-mapping-hdr-output-show-clipping"></a>**Show Clipping** | **On** marks pixels outside the brightness or color range as a diagnostic overlay. **Off** shows the normal picture. |
| <a id="hdr-color-mapping-hdr-peak-detection-enable-peak-detection"></a>**Enable Peak Detection** | **On** lets the renderer measure scene brightness for tone mapping. **Off** skips measurement and uses the available metadata. |
| <a id="hdr-color-mapping-hdr-peak-detection-allow-delayed-peak-detection"></a>**Allow Delayed Peak Detection** | **On** lets the measurement arrive one frame late. That can reduce GPU waiting but briefly use the old brightness after a hard cut. **Off** uses the current frame’s measurement. |
| <a id="hdr-color-mapping-hdr-peak-detection-smoothing-period"></a>**Smoothing Period** | Smooths frame-to-frame changes in measured brightness so small variations do not continually change tone mapping. The library expresses its smoothing period in frames/filter terms, not seconds and not a fixed waiting delay. |
| <a id="hdr-color-mapping-hdr-peak-detection-scene-threshold-low"></a>**Scene Threshold Low** | Sets the lower boundary at which a difference between current and averaged brightness begins to disable peak-measurement smoothing. The units are percentage points in PQ encoding, not a percentage of ordinary RGB pixel values. |
| <a id="hdr-color-mapping-hdr-peak-detection-scene-threshold-high"></a>**Scene Threshold High** | Sets the upper boundary of the smooth transition into fast scene-change response. Above this brightness difference, the normal smoothing is bypassed more fully. |
| <a id="hdr-color-mapping-hdr-peak-detection-percentile"></a>**Percentile** | Chooses which upper brightness percentile represents the scene peak. At 100 the brightest measured pixel is used; the library also treats 0 as maximum rather than “no peak”. |
| <a id="hdr-color-mapping-hdr-peak-detection-black-cutoff"></a>**Black Cutoff** | Smoothly excludes values below a very low PQ-encoded brightness threshold from the peak-analysis statistics. It helps prevent almost-black areas and encoded bars from distorting adaptation. |

<a id="hdr-color-mapping-tone-mapping-knee"></a>

## Tone Mapping Knee

The knee is where the curve changes shape. Spline and the adaptive ST 2094 curves can move it between scenes. If you are losing bright detail, start by [comparing the curves](#choosing-a-tone-mapping-function).

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-adaptation"></a>**Knee Adaptation** | Higher values adapt the brightness around the transition more strongly to the target range. Lower values keep it closer to the source. |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-minimum"></a>**Knee Minimum** | Limits how far down the adaptive knee can move. Raise it if you want to keep the transition from moving deep into the shadows in dark scenes. |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-maximum"></a>**Knee Maximum** | Limits how far up the adaptive knee can move. Lower it if you want to keep the transition from moving far into the highlights in bright scenes. |
| <a id="hdr-color-mapping-tone-mapping-knee-knee-default"></a>**Knee Default** | Places the knee when scene-average brightness is unavailable. A higher value moves this fallback point toward brighter tones. |
| <a id="hdr-color-mapping-tone-mapping-knee-bt-2390-knee-offset"></a>**BT.2390 Knee Offset** | Works with BT.2390. Raise it to start compressing at lower brightness levels. |

<a id="hdr-color-mapping-tone-mapping-constants"></a>

## Tone Mapping Constants

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-tone-mapping-constants-spline-slope-tuning"></a>**Spline Slope Tuning** | Adjusts how strongly Spline changes its slope in response to scene characteristics. This changes the shape/balance of the Spline curve rather than applying a simple global brightness offset. |
| <a id="hdr-color-mapping-tone-mapping-constants-spline-slope-offset"></a>**Spline Slope Offset** | Adds a baseline contribution to the Spline slope adjustment. It works with Spline Slope Tuning and the scene statistics, so its visual effect is context-dependent. |
| <a id="hdr-color-mapping-tone-mapping-constants-spline-contrast"></a>**Spline Contrast** | Controls Spline’s contrast around the knee. Higher values make the curve steeper and closer to clipping, preserving more midtone contrast at the expense of shadow/highlight detail; lower values make compression gentler. |
| <a id="hdr-color-mapping-tone-mapping-constants-reinhard-contrast"></a>**Reinhard Contrast** | Controls Reinhard’s local contrast coefficient around the display peak. It changes how this particular curve distributes brightness, rather than changing the general Contrast control. |
| <a id="hdr-color-mapping-tone-mapping-constants-mobius-gamma-knee"></a>**Mobius / Gamma Knee** | Sets the transition point for the Mobius/Gamma tone-mapping family. It determines where the curve changes behavior toward fitting the brighter range. |
| <a id="hdr-color-mapping-tone-mapping-constants-linear-exposure"></a>**Linear Exposure** | Sets a gain multiplier for Linear and Linear Light tone mapping. One is unchanged for that multiplier; two doubles it. |

<a id="hdr-color-mapping-contrast-recovery"></a>

## Contrast Recovery

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-contrast-recovery-contrast-recovery"></a>**Contrast Recovery** | Reintroduces local contrast that can be reduced by tone mapping. Instead of simply steepening the entire image, it attempts to retain more local separation. |
| <a id="hdr-color-mapping-contrast-recovery-contrast-smoothness"></a>**Contrast Smoothness** | Sets the spatial scale used by Contrast Recovery. It changes which size of local brightness structure participates in the recovery process. |

<a id="hdr-color-mapping-gamut-mapping-constants"></a>

## Gamut Mapping Constants

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-gamut-mapping-constants-perceptual-deadzone"></a>**Perceptual Deadzone** | Defines a chromaticity protection region for perceptual gamut mapping. It helps keep colors that do not need strong remapping from being changed unnecessarily, while more extreme colors can still be compressed. |
| <a id="hdr-color-mapping-gamut-mapping-constants-perceptual-strength"></a>**Perceptual Strength** | Controls the perceptual saturation-mapping component. It changes the balance of the perceptual gamut-compression behavior rather than directly scaling every color’s saturation. |
| <a id="hdr-color-mapping-gamut-mapping-constants-colorimetric-gamma"></a>**Colorimetric Gamma** | Adjusts the intensity/chroma relationship used for colorimetric clipping. It is an expert gamut-boundary-shape parameter, not the display’s EOTF or the Color Adjustment Gamma control. |
| <a id="hdr-color-mapping-gamut-mapping-constants-softclip-knee"></a>**Softclip Knee** | Defines where compatible soft-clipping/perceptual gamut compression starts. It governs the transition near the gamut boundary, not the luminance tone-mapping knee. |
| <a id="hdr-color-mapping-gamut-mapping-constants-softclip-desaturation"></a>**Softclip Desaturation** | Controls how much saturation is reduced near the gamut boundary by the Softclip mapper. It can trade a more muted extreme color for a smoother fit into the available output range. |

<a id="hdr-color-mapping-mapping-lut-quality"></a>

## Mapping LUT Quality

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-mapping-lut-quality-gamut-lut-intensity-size"></a>**Gamut LUT Intensity Size** | Sets the resolution of the internal gamut-mapping lookup table along its brightness/intensity axis. More samples can approximate a complex mapping more finely, but increase table size, setup cost and memory. |
| <a id="hdr-color-mapping-mapping-lut-quality-gamut-lut-chroma-size"></a>**Gamut LUT Chroma Size** | Sets the resolution of the internal gamut-mapping lookup table along its color intensity/chroma axis. More samples can approximate a complex mapping more finely, but increase table size, setup cost and memory. |
| <a id="hdr-color-mapping-mapping-lut-quality-gamut-lut-hue-size"></a>**Gamut LUT Hue Size** | Sets the resolution of the internal gamut-mapping lookup table along its hue/color angle axis. More samples can approximate a complex mapping more finely, but increase table size, setup cost and memory. |
| <a id="hdr-color-mapping-mapping-lut-quality-tricubic-gamut-interpolation"></a>**Tricubic Gamut Interpolation** | **On** uses a more detailed calculation between color LUT entries. This can smooth color transitions at extra GPU cost. **Off** uses the simpler calculation. |
| <a id="hdr-color-mapping-mapping-lut-quality-tone-mapping-lut-size"></a>**Tone Mapping LUT Size** | Sets the number of samples in the internal one-dimensional tone-curve lookup table. A larger table can represent a curve more finely, but can increase setup work. |

<a id="hdr-color-mapping-mapping-visualization"></a>

## Mapping Visualization

Use these overlays to check the mapping. Switch them off for normal playback.

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-mapping-visualization-force-tone-mapping-lut"></a>**Force Tone Mapping LUT** | **On** uses a lookup table (LUT) even for simple tone curves. **Off** allows direct calculation where possible. Use this to compare the two calculation paths. |
| <a id="hdr-color-mapping-mapping-visualization-visualize-mapping-lut"></a>**Visualize Mapping LUT** | **On** overlays a graph of the brightness and color mapping. **Off** hides the graph. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-left"></a>**Visualization Left** | Sets the left edge of the mapping-visualization rectangle in normalized output coordinates. Zero is the corresponding start of the screen dimension and one its far end. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-top"></a>**Visualization Top** | Sets the top edge of the mapping-visualization rectangle in normalized output coordinates. Zero is the corresponding start of the screen dimension and one its far end. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-right"></a>**Visualization Right** | Sets the right edge of the mapping-visualization rectangle in normalized output coordinates. Zero is the corresponding start of the screen dimension and one its far end. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-bottom"></a>**Visualization Bottom** | Sets the bottom edge of the mapping-visualization rectangle in normalized output coordinates. Zero is the corresponding start of the screen dimension and one its far end. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-hue-radians"></a>**Visualization Hue (radians)** | Chooses the hue slice shown by the gamut visualization. Radians measure angle; π radians corresponds to 180 degrees. |
| <a id="hdr-color-mapping-mapping-visualization-visualization-rotation-radians"></a>**Visualization Rotation (radians)** | Rotates the diagnostic mapping view within its allowed angular range. It changes how you inspect the visualization, not the orientation of the video frame. |

<a id="hdr-color-mapping-expansion-metadata"></a>

## Expansion & Metadata

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-expansion-metadata-inverse-tone-mapping"></a>**Inverse Tone Mapping** | **On** allows compatible curves to expand brightness into a larger output range. **Off** disables that expansion. |
| <a id="hdr-color-mapping-expansion-metadata-gamut-expansion"></a>**Gamut Expansion** | **On** allows a compatible method to stretch colors into a wider gamut. **Off** disables that expansion. |
| <a id="hdr-color-mapping-expansion-metadata-tone-mapping-metadata"></a>**Tone Mapping Metadata** | Controls which available metadata/statistics are used for tone mapping. Auto keeps renderer selection; None suppresses that metadata source; HDR10 selects static mastering/content-light information; HDR10+ selects available dynamic information; CIE Y uses applicable luminance statistics. |

<a id="hdr-color-mapping-shadows"></a>

## Shadows

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-shadows-exposure"></a>**Exposure** | Changes exposure primarily in dark image regions, using smoothly overlapping HDR luminance masks. The number is in stops: +1 doubles that zone’s light contribution; −1 halves it. |
| <a id="hdr-color-mapping-shadows-contrast"></a>**Contrast** | Changes contrast mainly within dark image regions. Zero is neutral in this HDR-zone control. |
| <a id="hdr-color-mapping-shadows-saturation"></a>**Saturation** | Changes color intensity primarily in dark image regions. Zero is neutral; a positive value intensifies color and a negative value moves it toward gray. |

<a id="hdr-color-mapping-midtones"></a>

## Midtones

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-midtones-exposure"></a>**Exposure** | Changes exposure primarily in middle-brightness regions such as many faces, using smoothly overlapping HDR luminance masks. The number is in stops: +1 doubles that zone’s light contribution; −1 halves it. |
| <a id="hdr-color-mapping-midtones-contrast"></a>**Contrast** | Changes contrast mainly within middle-brightness regions such as many faces. Zero is neutral in this HDR-zone control. |
| <a id="hdr-color-mapping-midtones-saturation"></a>**Saturation** | Changes color intensity primarily in middle-brightness regions such as many faces. Zero is neutral; a positive value intensifies color and a negative value moves it toward gray. |

<a id="hdr-color-mapping-highlights"></a>

## Highlights

| Setting | What it does |
| --- | --- |
| <a id="hdr-color-mapping-highlights-exposure"></a>**Exposure** | Changes exposure primarily in bright regions such as windows and reflections, using smoothly overlapping HDR luminance masks. The number is in stops: +1 doubles that zone’s light contribution; −1 halves it. |
| <a id="hdr-color-mapping-highlights-contrast"></a>**Contrast** | Changes contrast mainly within bright regions such as windows and reflections. Zero is neutral in this HDR-zone control. |
| <a id="hdr-color-mapping-highlights-saturation"></a>**Saturation** | Changes color intensity primarily in bright regions such as windows and reflections. Zero is neutral; a positive value intensifies color and a negative value moves it toward gray. |

<a id="hdr-color-mapping-tone-controls"></a>

## Tone Controls

The [shadows](#hdr-color-mapping-shadows), [midtones](#hdr-color-mapping-midtones) and [highlights](#hdr-color-mapping-highlights) controls each affect a different part of the brightness range.

---

[← Color Adjustment](Color-Adjustment.md) · [Color Management →](Color-Management.md)
