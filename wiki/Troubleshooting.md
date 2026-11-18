**English** · [Deutsch](DE-Troubleshooting.md)

[Home](Home.md) · [Using the menu](Using-the-menu.md) · [Troubleshooting](Troubleshooting.md)

<a id="troubleshooting-examples"></a>

## Everyday examples

Examples from everyday viewing. Change one setting at a time and compare the same scene.

| Situation | Setting | What to check |
| --- | --- | --- |
| The video stutters and the drops in Ctrl+J keep rising. | Set **Quality** to **Fast** or turn shaders off. | **Render** should stay below the time budget per frame. |
| Black is gray and colors look flat. | Check [RGB Output Levels](General.md#general-output-rgb-output-levels). | Display and driver must use the same range. |
| An HDR movie looks pale and gray. | Check [HDR Mode](HDR-and-Color-Mapping.md#hdr-color-mapping-hdr-output-hdr-mode) and Windows HDR. | Ctrl+J shows the active HDR path. |
| RTX VSR has no effect and Ctrl+J shows **Image: native size**. | Change nothing. | The video already has the size of the screen. |
| The picture stays black after seeking. | Test the same seek without upstream filters. | This shows whether the renderer or a filter is the cause. |

## Check the active path with Ctrl+J

Statistics are grouped into Display, Video, Processing and Performance. Check the actual source pixel format, dimensions, HDR metadata, active profile and transfer path before comparing settings. The overlay also identifies the renderer's GPU and shows the source gamma and output gamma where applicable.

- **CPU memory → GPU upload** means the renderer receives CPU pixels and uploads them. **D3D11 texture (GPU direct)** means it receives a GPU texture. The decoder and upstream filters determine this route.
- **RTX Video** distinguishes HDR/VSR requests, prepared or decoder pictures, NVIDIA-app-off states and failures. **Image: native size** means no enlargement is needed. HDR conversion can still run at native size.
- **Source HDR/Dolby Vision metadata** describes the input. It does not alone prove an HDR or native Dolby Vision signal reaches the television. Check Windows HDR, the output route and the display's own indicator.

## Rendering time and drops

**Render** shows recent average render time. **Present** and **Queue** describe presentation and pending work. The render budget follows the display refresh interval when Smooth Motion is active or the source is faster than the display.

For a 120 fps video on a 60 Hz display without Smooth Motion, the renderer shows one frame per refresh. Ctrl+J reports the displayed rate. Frames omitted to match the display are excluded from the drop count. Watch whether drops continue to rise after resetting the statistics.

**Clock** shows the audio-clock deviation and the estimated interval between repeats or drops caused by drift. The reading takes time to settle and starts over after a seek, playback restart, clock step or a change to Smooth Motion.

## RTX VSR works but RTX Video HDR does not

1. Check the RTX GPU, driver and the **RTX Video HDR** switch in the NVIDIA app.
2. Turn on Windows HDR for the correct display and play an SDR clip. Check **RTX Video HDR** in the renderer’s HDR menu.
3. Read the full **RTX Video** line in Ctrl+J. **HDR extension** identifies a failed driver request. **HDR white validation** identifies a failed startup check. VSR can keep working. Try a second clip if the message points to input size or format.

## Black screen, seeking or upstream filters

Test the same clip, decoder and filter chain at startup and after seeking. If the issue follows a script or filter change, record that step and compare with the upstream filter disabled. If GPU load is high, compare with **All Off** in Shaders after shader compilation finishes.

## Save a diagnostic report

Under **General > Diagnostics**, enable **Diagnostic Log**. Restart the player, reproduce the problem and choose **Save Report**. Logging applies to one player start. Check the report for private paths before attaching it. Include the build ID, player version, decoder, GPU and steps to reproduce. [Report a bug](https://github.com/mryx007/FrankyRenderer/issues)
