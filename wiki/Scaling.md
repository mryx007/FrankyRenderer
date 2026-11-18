**English** · [Deutsch](DE-Scaling.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Sources](Sources.md)

Filters for image size, color edges and motion.

**On this page**

- [Choosing a filter: a practical guide](#choosing-a-filter)
- [Filters](#scaling-filters)
- [RTX Video Super Resolution](#scaling-rtx-video-super-resolution)
- [Custom: Image Upscaler](#scaling-custom-image-upscaler)
- [Custom: Image Downscaler](#scaling-custom-image-downscaler)
- [Custom: Plane Upscaler (Chroma / Alpha)](#scaling-custom-plane-upscaler-chroma-alpha)
- [Custom: Plane Downscaler (Chroma / Alpha)](#scaling-custom-plane-downscaler-chroma-alpha)
- [Custom: Frame Mixer](#scaling-custom-frame-mixer)

---

<a id="choosing-a-filter"></a>

## Choosing a filter: a practical guide

**Start with auto.** To make a change, choose the field that fits what the video is doing.

### First, choose the correct field

| What is happening? | Which field matters? | Example |
| --- | --- | --- |
| The picture is enlarged | **Image Upscaler** | A 1920×1080 video filling a 3840×2160 video area. |
| The picture is reduced | **Image Downscaler** | A 3840×2160 video shown in a 1920×1080 video area. |
| Subsampled color is reconstructed | **Plane Upscaler (Chroma / Alpha)** | A 4:2:0 movie can need color reconstruction even at the picture's original size. |
| Frames are mixed across time | **Frame Mixer** | Smooth Motion adapting a 24 fps film to a 60 Hz display. This is separate from spatial sharpness. |

Use the **actual video area**. A small window on a 4K screen can still shrink the picture.

### Quick starting choices

| Situation | Start by comparing | What to inspect |
| --- | --- | --- |
| Ordinary live-action movie, 1080p → 4K | **auto**, then **lanczos** | Hair, clothing, faces and outlines around bright objects. |
| More GPU headroom; looking for a crisper enlargement | **ewa_lanczossharp** against **lanczos** | Whether useful fine structure improves, or noise and outlines merely become stronger. |
| Low-power GPU, or expensive processing already enabled | **bilinear** or **bicubic** | Whether playback stays smooth and the extra softness is acceptable. |
| Grainy, compressed or already over-sharpened material | **bicubic** against **lanczos** | Whether the sharper choice emphasizes grain, block edges or existing halos. A scaler is not a denoiser. |
| 4K → 1080p reduction | **mitchell** against **lanczos** | Watch roof tiles and railings during motion. Leave **Skip Downscale Anti-Aliasing** set to **Off**. |
| Deliberately pixelated artwork or old game recordings | **nearest** or **oversample** | Whether you want hard pixel blocks or slightly blended block boundaries. |
| Chroma quality comparison on an ordinary 4:2:0 movie | **auto**, then **ewa_lanczos** | Colored edges and fine red/blue detail. **bilinear** is a useful low-cost comparison. |

<a id="approximate-gpu-cost"></a>

### Approximate GPU cost

| Filter | Approximate cost | What it means |
| --- | --- | --- |
| nearest, bilinear, box, oversample, hermite, bicubic, gaussian | Very low to low | Compare these if playback struggles. |
| catmull_rom, mitchell, lanczos | Medium | Compare these for fine detail. |
| ewa_lanczos, ewa_lanczossharp | High | Check render time with a large video area. |
| ewa_lanczos4sharpest | Very high | The most demanding filter in this comparison. |
| auto, none, custom | Depends on the chosen method | Cost follows the filter that actually runs. |

Window size, frame rate and shaders affect load. Compare render time with **Ctrl+J** using the same scene and window size. [libplacebo filter reference](https://libplacebo.org/options/#scaling)

### What the individual spatial choices look like

Expand a filter to see its effect.

<details>
<summary>auto — let the renderer choose</summary>

A good baseline when you do not want to tune filters. It does not analyze each film and decide which artistic appearance you prefer.

</details>

<details>
<summary>none — role-dependent bypass or inheritance</summary>

This is not a “maximum fidelity” preset. For Image Upscaler it leaves basic GPU sampling; for Image Downscaler it inherits the upscaler; for the plane roles it inherits the corresponding image filter.

</details>

<details>
<summary>custom — build your own filter</summary>

Custom is an editing mode, not an automatically superior algorithm. Start from a known preset and change one supported parameter.

</details>

<details>
<summary>nearest — preserve hard pixel blocks</summary>

Each output position takes a nearby source sample rather than smoothly reconstructing a photographic surface. **Use it for:** intentionally pixelated graphics.

</details>

<details>
<summary>bilinear — inexpensive and smooth, with softer detail</summary>

A simple choice when processing cost matters more than crisp reconstruction. **For example:** enlarge a 720p video in a laptop window and compare small lettering with lanczos.

</details>

<details>
<summary>oversample — a pixel-oriented compromise</summary>

In spatial scaling it keeps much of nearest's pixel-grid character, with blending at boundaries when the grids do not line up. **For example:** enlarge pixel art to a window size that is not an exact whole-number multiple.

</details>

<details>
<summary>bicubic — a smooth, relatively inexpensive comparison</summary>

Useful when a more aggressive reconstruction looks too harsh. The exact cubic preset matters; “bicubic” does not mean every cubic algorithm has the same appearance.

</details>

<details>
<summary>gaussian — deliberately smooth</summary>

Choose it when you want to explore a softer reconstruction rather than maximize apparent edge crispness. **For example:** compare a poor compressed source with gaussian and lanczos.

</details>

<details>
<summary>catmull_rom — a crisp cubic alternative</summary>

Worth comparing when a smoother cubic choice looks dull, but check high-contrast outlines. **For example:** inspect black lettering against a white sign.

</details>

<details>
<summary>lanczos — a useful detailed-image baseline</summary>

A practical first manual comparison for photographic material. **For example:** enlarge a 1080p scene with hair, brickwork and a bright window to a 4K video area.

</details>

<details>
<summary>ewa_lanczos — a more demanding two-dimensional reconstruction</summary>

An alternative for enlargement or color-plane reconstruction when you have processing headroom. **For example:** compare it with lanczos on a slowly moving sloped roof and colored diagonal lines.

</details>

<details>
<summary>ewa_lanczossharp — the crisper EWA comparison</summary>

Try this when ordinary EWA looks softer than you prefer. **For example:** enlarge a clean 1080p live-action shot to 4K and inspect hair and fine clothing.

</details>

<details>
<summary>ewa_lanczos4sharpest — an aggressive option to evaluate carefully</summary>

This preset includes anti-ringing in libplacebo, but its name is not a promise of the most faithful picture. **For example:** compare it with ewa_lanczossharp on a clean, detailed source and on a grainy one.

</details>

<details>
<summary>box — a simple reduction reference</summary>

Useful as a low-complexity comparison when making an image smaller. **For example:** reduce a 4K scene with fine railings to 1080p, then compare box with mitchell.

</details>

<details>
<summary>hermite — a fast downscaling baseline</summary>

A useful option when reducing the image and keeping cost low. **For example:** show a 4K video in a smaller window, then compare hermite with mitchell on fine diagonal detail.

</details>

<details>
<summary>mitchell — a balanced reduction comparison</summary>

Often a useful starting experiment for 4K → 1080p when you want a compromise between crispness and visible scaling artifacts. **For example:** compare with lanczos on a checked jacket during a camera pan.

</details>

### Frame Mixer: a separate decision

Smooth Motion blends neighboring frames. It can smooth camera pans and introduce double edges.

| Choice | When to compare it | Example / trade-off |
| --- | --- | --- |
| **auto** | You want the renderer's temporal choice. | Start here before experimenting. |
| **none** | You deliberately want the nearest source frame instead of a blend. | Compare a 24 fps pan on a 60 Hz screen: cadence judder may be more visible, while blended contours are avoided. |
| **custom** | You are intentionally designing a temporal filter. | Leave this for advanced comparisons; spatial presets and temporal support are not interchangeable. |
| **oversample** | A first manual Smooth Motion comparison. | On 24 fps → 60 Hz, check the transition moments of a slow pan for smoother cadence and any blended edge. |
| **hermite** | You want to compare another weighted transition. | Watch a moving face or a sign; reject it if blended contours distract you. |
| **linear** | You want a straightforward neighboring-frame blend to compare. | Follow a moving hand: a smoother transition can also make double contours easier to notice. |
| **cubic** | You want to evaluate a broader temporal blend. | Compare motion smoothness against softness and overlapping contours; more blending is not automatically better. |

**Clamped Mitchell** appears as **mitchell_clamp**. [Oversample Threshold](#scaling-filters-oversample-threshold) controls when a whole frame is preferred. [Present in Advance](#scaling-filters-present-in-advance) adjusts how far ahead frames are queued.

### A five-minute comparison you can repeat

1. Use the same detailed scene with a camera pan. Switch off additional sharpening and RTX VSR for the comparison.
2. Compare **auto** with a suitable filter at the same window size.
3. Check for shimmer, halos and smooth playback. Use **Ctrl+J** to see render time.

---

**Custom** lets you adjust a starting filter. **none** is available for supported primary filters and as a window function.

<a id="scaling-filters"></a>

## Filters

| Setting | What it does |
| --- | --- |
| <a id="scaling-filters-image-upscaler"></a>**Image Upscaler** | Selects the filter used when the visible video image is enlarged. It estimates output pixels from the original pixel grid; it does not change the resolution stored in the file. |
| <a id="scaling-filters-image-downscaler"></a>**Image Downscaler** | Selects the filter for reducing the visible image size. Good downscaling removes detail that cannot be represented by the smaller output grid, instead of letting it turn into shimmering patterns. |
| <a id="scaling-filters-plane-upscaler-chroma-alpha"></a>**Plane Upscaler (Chroma / Alpha)** | Selects how lower-resolution chroma or alpha planes are reconstructed relative to the image. Many videos have full-resolution brightness but reduced-resolution color, such as 4:2:0. |
| <a id="scaling-filters-plane-downscaler-chroma-alpha"></a>**Plane Downscaler (Chroma / Alpha)** | Selects filtering for chroma or alpha planes when those planes must be reduced in resolution. It is distinct from shrinking the complete picture, and its visibility depends on the source/output plane layout and processing path. |
| <a id="scaling-filters-frame-mixer"></a>**Frame Mixer** | Selects Smooth Motion's temporal filter. It blends existing source pictures across time; it does not generate motion-compensated or AI intermediate frames. |
| <a id="scaling-filters-global-anti-ringing"></a>**Global Anti-Ringing** | Reduces overshoot caused by reconstruction filters. Overshoot can look like a bright outline beside a dark edge, or a dark outline beside a bright one. |
| <a id="scaling-filters-enable-smooth-motion"></a>**Enable Smooth Motion** | **On** blends neighboring frames to reduce judder from a frame-rate mismatch. Moving edges can show double contours. **Off** presents unmixed frames. |
| <a id="scaling-filters-skip-downscale-anti-aliasing"></a>**Skip Downscale Anti-Aliasing** | **Off (default)** keeps anti-aliasing enabled when shrinking the picture. **On** skips it to save GPU time. Fine lines and patterns can then shimmer or show jagged edges. |
| <a id="scaling-filters-preserve-frame-mixer-cache"></a>**Preserve Frame Mixer Cache** | **Off (default)** recalculates mixed frames after size or setting changes. **On** briefly reuses old frames. Resizing can look smoother with a short drop in sharpness. |
| <a id="scaling-filters-present-in-advance"></a>**Present in Advance** | Controls how many display refreshes ahead Smooth Motion may hand pictures to the display. More advance can absorb rendering spikes but adds latency. |
| <a id="scaling-filters-oversample-threshold"></a>**Oversample Threshold** | Avoids blending a second source picture when it contributes only a small share of one display refresh. A value of 0 retains all contributions; larger values can reduce double contours at the cost of a less exact blend. |

<a id="scaling-rtx-video-super-resolution"></a>

## RTX Video Super Resolution

| Setting | What it does |
| --- | --- |
| <a id="scaling-rtx-video-super-resolution-rtx-video-super-resolution"></a>**RTX Video Super Resolution** | **On** requests AI upscaling from the NVIDIA driver when enlarging video. Super Resolution must also be enabled in the NVIDIA app. **Off** uses the normal scaling filter. |

<a id="scaling-custom-image-upscaler"></a>

## Custom: Image Upscaler

| Setting | What it does |
| --- | --- |
| <a id="scaling-custom-image-upscaler-enable-custom-settings"></a>**Enable Custom Settings** | **On** lets you adjust the selected starting filter. **Off** switches back to the starting filter itself. |
| <a id="scaling-custom-image-upscaler-starting-filter"></a>**Starting Filter** | Selects the preset whose properties are inherited by the custom filter until you override them. It is a base configuration, not an extra filter pass added in front of the custom one. |
| <a id="scaling-custom-image-upscaler-kernel"></a>**Kernel** | Chooses the mathematical reconstruction function that assigns weights to samples. Auto inherits the starting preset. |
| <a id="scaling-custom-image-upscaler-window"></a>**Window** | Selects a weighting envelope applied to a reconstruction kernel. A window can taper the outer samples of a kernel, changing sharpness and ringing. |
| <a id="scaling-custom-image-upscaler-radius"></a>**Radius** | Sets the support of a resizable reconstruction kernel: how far from the target position samples contribute. Larger support can retain or reconstruct different detail but costs more sampling and can increase ringing. |
| <a id="scaling-custom-image-upscaler-clamp"></a>**Clamp** | Reduces the contribution of negative filter weights. Negative weights help some sharp filters reconstruct edges, but can also create overshoot. |
| <a id="scaling-custom-image-upscaler-blur"></a>**Blur** | Multiplies the filter width. Below 1 usually makes the reconstruction narrower/crisper; above 1 makes it broader/softer. |
| <a id="scaling-custom-image-upscaler-taper"></a>**Taper** | Adjusts the flattened central region of a reconstruction kernel. It changes how the innermost samples are weighted relative to the outer part of the filter. |
| <a id="scaling-custom-image-upscaler-anti-ringing"></a>**Anti-Ringing** | Overrides anti-ringing for this custom filter. A nonzero value applies that filter-specific strength; zero inherits the global anti-ringing behavior in this implementation. |
| <a id="scaling-custom-image-upscaler-kernel-parameter-1"></a>**Kernel Parameter 1** | Exposes the first parameter declared tunable by the selected kernel. The meaning is kernel-specific, so it is not a generic sharpness percentage. |
| <a id="scaling-custom-image-upscaler-kernel-parameter-2"></a>**Kernel Parameter 2** | Exposes the second tunable parameter of the selected kernel, if it has one. It works together with parameter 1 to define that function’s shape. |
| <a id="scaling-custom-image-upscaler-window-parameter-1"></a>**Window Parameter 1** | Exposes the first tunable parameter of the selected window function. Its interpretation comes from that function; it may alter how quickly the outer kernel weights taper. |
| <a id="scaling-custom-image-upscaler-window-parameter-2"></a>**Window Parameter 2** | Exposes a second tunable window parameter only when the selected function actually provides one. It is separate from the two kernel parameters. |
| <a id="scaling-custom-image-upscaler-polar-ewa"></a>**Polar (EWA)** | **On** weights nearby pixels in both directions together (EWA). **Off** filters horizontally and vertically in separate passes. **Inherit Starting Filter** uses the preset’s method. |

<a id="scaling-custom-image-downscaler"></a>

## Custom: Image Downscaler

| Setting | What it does |
| --- | --- |
| <a id="scaling-custom-image-downscaler-enable-custom-settings"></a>**Enable Custom Settings** | **On** lets you adjust the selected starting filter. **Off** switches back to the starting filter itself. |
| <a id="scaling-custom-image-downscaler-starting-filter"></a>**Starting Filter** | Selects the preset whose properties are inherited by the custom filter until you override them. It is a base configuration, not an extra filter pass added in front of the custom one. |
| <a id="scaling-custom-image-downscaler-kernel"></a>**Kernel** | Chooses the mathematical reconstruction function that assigns weights to samples. Auto inherits the starting preset. |
| <a id="scaling-custom-image-downscaler-window"></a>**Window** | Selects a weighting envelope applied to a reconstruction kernel. A window can taper the outer samples of a kernel, changing sharpness and ringing. |
| <a id="scaling-custom-image-downscaler-radius"></a>**Radius** | Sets the support of a resizable reconstruction kernel: how far from the target position samples contribute. Larger support can retain or reconstruct different detail but costs more sampling and can increase ringing. |
| <a id="scaling-custom-image-downscaler-clamp"></a>**Clamp** | Reduces the contribution of negative filter weights. Negative weights help some sharp filters reconstruct edges, but can also create overshoot. |
| <a id="scaling-custom-image-downscaler-blur"></a>**Blur** | Multiplies the filter width. Below 1 usually makes the reconstruction narrower/crisper; above 1 makes it broader/softer. |
| <a id="scaling-custom-image-downscaler-taper"></a>**Taper** | Adjusts the flattened central region of a reconstruction kernel. It changes how the innermost samples are weighted relative to the outer part of the filter. |
| <a id="scaling-custom-image-downscaler-anti-ringing"></a>**Anti-Ringing** | Overrides anti-ringing for this custom filter. A nonzero value applies that filter-specific strength; zero inherits the global anti-ringing behavior in this implementation. |
| <a id="scaling-custom-image-downscaler-kernel-parameter-1"></a>**Kernel Parameter 1** | Exposes the first parameter declared tunable by the selected kernel. The meaning is kernel-specific, so it is not a generic sharpness percentage. |
| <a id="scaling-custom-image-downscaler-kernel-parameter-2"></a>**Kernel Parameter 2** | Exposes the second tunable parameter of the selected kernel, if it has one. It works together with parameter 1 to define that function’s shape. |
| <a id="scaling-custom-image-downscaler-window-parameter-1"></a>**Window Parameter 1** | Exposes the first tunable parameter of the selected window function. Its interpretation comes from that function; it may alter how quickly the outer kernel weights taper. |
| <a id="scaling-custom-image-downscaler-window-parameter-2"></a>**Window Parameter 2** | Exposes a second tunable window parameter only when the selected function actually provides one. It is separate from the two kernel parameters. |
| <a id="scaling-custom-image-downscaler-polar-ewa"></a>**Polar (EWA)** | **On** weights nearby pixels in both directions together (EWA). **Off** filters horizontally and vertically in separate passes. **Inherit Starting Filter** uses the preset’s method. |

<a id="scaling-custom-plane-upscaler-chroma-alpha"></a>

## Custom: Plane Upscaler (Chroma / Alpha)

| Setting | What it does |
| --- | --- |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-enable-custom-settings"></a>**Enable Custom Settings** | **On** lets you adjust the selected starting filter. **Off** switches back to the starting filter itself. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-starting-filter"></a>**Starting Filter** | Selects the preset whose properties are inherited by the custom filter until you override them. It is a base configuration, not an extra filter pass added in front of the custom one. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-kernel"></a>**Kernel** | Chooses the mathematical reconstruction function that assigns weights to samples. Auto inherits the starting preset. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-window"></a>**Window** | Selects a weighting envelope applied to a reconstruction kernel. A window can taper the outer samples of a kernel, changing sharpness and ringing. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-radius"></a>**Radius** | Sets the support of a resizable reconstruction kernel: how far from the target position samples contribute. Larger support can retain or reconstruct different detail but costs more sampling and can increase ringing. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-clamp"></a>**Clamp** | Reduces the contribution of negative filter weights. Negative weights help some sharp filters reconstruct edges, but can also create overshoot. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-blur"></a>**Blur** | Multiplies the filter width. Below 1 usually makes the reconstruction narrower/crisper; above 1 makes it broader/softer. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-taper"></a>**Taper** | Adjusts the flattened central region of a reconstruction kernel. It changes how the innermost samples are weighted relative to the outer part of the filter. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-anti-ringing"></a>**Anti-Ringing** | Overrides anti-ringing for this custom filter. A nonzero value applies that filter-specific strength; zero inherits the global anti-ringing behavior in this implementation. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-kernel-parameter-1"></a>**Kernel Parameter 1** | Exposes the first parameter declared tunable by the selected kernel. The meaning is kernel-specific, so it is not a generic sharpness percentage. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-kernel-parameter-2"></a>**Kernel Parameter 2** | Exposes the second tunable parameter of the selected kernel, if it has one. It works together with parameter 1 to define that function’s shape. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-window-parameter-1"></a>**Window Parameter 1** | Exposes the first tunable parameter of the selected window function. Its interpretation comes from that function; it may alter how quickly the outer kernel weights taper. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-window-parameter-2"></a>**Window Parameter 2** | Exposes a second tunable window parameter only when the selected function actually provides one. It is separate from the two kernel parameters. |
| <a id="scaling-custom-plane-upscaler-chroma-alpha-polar-ewa"></a>**Polar (EWA)** | **On** weights nearby pixels in both directions together (EWA). **Off** filters horizontally and vertically in separate passes. **Inherit Starting Filter** uses the preset’s method. |

<a id="scaling-custom-plane-downscaler-chroma-alpha"></a>

## Custom: Plane Downscaler (Chroma / Alpha)

| Setting | What it does |
| --- | --- |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-enable-custom-settings"></a>**Enable Custom Settings** | **On** lets you adjust the selected starting filter. **Off** switches back to the starting filter itself. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-starting-filter"></a>**Starting Filter** | Selects the preset whose properties are inherited by the custom filter until you override them. It is a base configuration, not an extra filter pass added in front of the custom one. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-kernel"></a>**Kernel** | Chooses the mathematical reconstruction function that assigns weights to samples. Auto inherits the starting preset. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-window"></a>**Window** | Selects a weighting envelope applied to a reconstruction kernel. A window can taper the outer samples of a kernel, changing sharpness and ringing. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-radius"></a>**Radius** | Sets the support of a resizable reconstruction kernel: how far from the target position samples contribute. Larger support can retain or reconstruct different detail but costs more sampling and can increase ringing. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-clamp"></a>**Clamp** | Reduces the contribution of negative filter weights. Negative weights help some sharp filters reconstruct edges, but can also create overshoot. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-blur"></a>**Blur** | Multiplies the filter width. Below 1 usually makes the reconstruction narrower/crisper; above 1 makes it broader/softer. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-taper"></a>**Taper** | Adjusts the flattened central region of a reconstruction kernel. It changes how the innermost samples are weighted relative to the outer part of the filter. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-anti-ringing"></a>**Anti-Ringing** | Overrides anti-ringing for this custom filter. A nonzero value applies that filter-specific strength; zero inherits the global anti-ringing behavior in this implementation. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-kernel-parameter-1"></a>**Kernel Parameter 1** | Exposes the first parameter declared tunable by the selected kernel. The meaning is kernel-specific, so it is not a generic sharpness percentage. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-kernel-parameter-2"></a>**Kernel Parameter 2** | Exposes the second tunable parameter of the selected kernel, if it has one. It works together with parameter 1 to define that function’s shape. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-window-parameter-1"></a>**Window Parameter 1** | Exposes the first tunable parameter of the selected window function. Its interpretation comes from that function; it may alter how quickly the outer kernel weights taper. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-window-parameter-2"></a>**Window Parameter 2** | Exposes a second tunable window parameter only when the selected function actually provides one. It is separate from the two kernel parameters. |
| <a id="scaling-custom-plane-downscaler-chroma-alpha-polar-ewa"></a>**Polar (EWA)** | **On** weights nearby pixels in both directions together (EWA). **Off** filters horizontally and vertically in separate passes. **Inherit Starting Filter** uses the preset’s method. |

<a id="scaling-custom-frame-mixer"></a>

## Custom: Frame Mixer

| Setting | What it does |
| --- | --- |
| <a id="scaling-custom-frame-mixer-enable-custom-settings"></a>**Enable Custom Settings** | **On** lets you adjust the selected starting filter. **Off** switches back to the starting filter itself. |
| <a id="scaling-custom-frame-mixer-starting-filter"></a>**Starting Filter** | Selects the temporal preset from which the custom Frame Mixer settings start. Parameters below override compatible parts of that preset. |
| <a id="scaling-custom-frame-mixer-kernel"></a>**Kernel** | Chooses the mathematical reconstruction function that assigns weights to samples. Auto inherits the starting preset. |
| <a id="scaling-custom-frame-mixer-window"></a>**Window** | Selects a weighting envelope applied to a reconstruction kernel. A window can taper the outer samples of a kernel, changing sharpness and ringing. |
| <a id="scaling-custom-frame-mixer-radius"></a>**Radius** | Sets the support of a resizable reconstruction kernel: how far from the target position samples contribute. Larger support can retain or reconstruct different detail but costs more sampling and can increase ringing. |
| <a id="scaling-custom-frame-mixer-clamp"></a>**Clamp** | Reduces the contribution of negative filter weights. Negative weights help some sharp filters reconstruct edges, but can also create overshoot. |
| <a id="scaling-custom-frame-mixer-blur"></a>**Blur** | Multiplies the filter width. Below 1 usually makes the reconstruction narrower/crisper; above 1 makes it broader/softer. |
| <a id="scaling-custom-frame-mixer-taper"></a>**Taper** | Adjusts the flattened central region of a reconstruction kernel. It changes how the innermost samples are weighted relative to the outer part of the filter. |
| <a id="scaling-custom-frame-mixer-anti-ringing"></a>**Anti-Ringing** | Overrides anti-ringing for this custom filter. A nonzero value applies that filter-specific strength; zero inherits the global anti-ringing behavior in this implementation. |
| <a id="scaling-custom-frame-mixer-kernel-parameter-1"></a>**Kernel Parameter 1** | Exposes the first parameter declared tunable by the selected kernel. The meaning is kernel-specific, so it is not a generic sharpness percentage. |
| <a id="scaling-custom-frame-mixer-kernel-parameter-2"></a>**Kernel Parameter 2** | Exposes the second tunable parameter of the selected kernel, if it has one. It works together with parameter 1 to define that function’s shape. |
| <a id="scaling-custom-frame-mixer-window-parameter-1"></a>**Window Parameter 1** | Exposes the first tunable parameter of the selected window function. Its interpretation comes from that function; it may alter how quickly the outer kernel weights taper. |
| <a id="scaling-custom-frame-mixer-window-parameter-2"></a>**Window Parameter 2** | Exposes a second tunable window parameter only when the selected function actually provides one. It is separate from the two kernel parameters. |
| <a id="scaling-custom-frame-mixer-polar-ewa"></a>**Polar (EWA)** | This choice is disabled for the frame mixer. It remains one-dimensional and blends frames over time. |

---

[← Processing](Processing.md) · [Black Bars / Zoom →](Black-Bars-and-Zoom.md)
