#pragma once
#include <libplacebo/common.h>
#include <windows.h>
#include <algorithm>
#include <cmath>

// Move the picture, preserving its scale and the player's horizontal geometry.
inline void AlignImageVertically(
    pl_rect2df& target, const pl_rect2df& source, const RECT& active, int rotation, int position, float outputHeight)
{
    if (!position || source.x1 <= source.x0 || source.y1 <= source.y0)
        return;
    float a, b;
    if (rotation % 180)
    {
        a = (active.left - source.x0) / (source.x1 - source.x0);
        b = (active.right - source.x0) / (source.x1 - source.x0);
    }
    else
    {
        a = (active.top - source.y0) / (source.y1 - source.y0);
        b = (active.bottom - source.y0) / (source.y1 - source.y0);
    }
    a = std::clamp(a, 0.f, 1.f);
    b = std::clamp(b, 0.f, 1.f);
    if (rotation == 90 || rotation == 180)
    {
        const float first = a;
        a = 1.f - b;
        b = 1.f - first;
    }
    const float height = target.y1 - target.y0;
    if (height <= 0 || b <= a || (b - a) * height > outputHeight + .5f)
        return; // A player zoom must not cut off a different part of the movie.
    const float shift = position == 1 ? outputHeight - (target.y0 + b * height) : -(target.y0 + a * height);
    target.y0 += shift;
    target.y1 += shift;
}

// Reproduce a cropped or off-screen subtitle bar at its original screen edge.
// Source coordinates always refer to the original, uncropped decoded frame.
inline bool HiddenSubtitleBar(const pl_rect2df& original,
                              const pl_rect2df& cropped,
                              const pl_rect2df& target,
                              const RECT& content,
                              float outputHeight,
                              pl_rect2df& sourceBar,
                              pl_rect2df& targetBar,
                              bool top = false)
{
    const float sourceHeight = cropped.y1 - cropped.y0;
    if (sourceHeight <= 0)
        return false;
    const float scale = (target.y1 - target.y0) / sourceHeight;
    const float edge = top ? float(content.top) : float(content.bottom);
    const float barHeight = (top ? edge - original.y0 : original.y1 - edge) * scale;
    // Even a bar whose natural position fits the window must be restored
    // when those source rows were removed by automatic crop (16:9 TVs).
    const bool sourceCropped = top ? cropped.y0 > original.y0 + .5f : cropped.y1 < original.y1 - .5f;
    const float naturalEdge = target.y0 + ((top ? original.y0 : original.y1) - cropped.y0) * scale;
    const bool offscreen = top ? naturalEdge < -.5f : naturalEdge > outputHeight + .5f;
    if (scale <= 0 || barHeight <= 0 || barHeight >= outputHeight || (!sourceCropped && !offscreen))
        return false;
    sourceBar = {original.x0, top ? original.y0 : edge, original.x1, top ? edge : original.y1};
    targetBar = {target.x0, top ? 0.f : outputHeight - barHeight, target.x1, top ? barHeight : outputHeight};
    return true;
}
