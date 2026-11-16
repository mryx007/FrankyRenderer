#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>

struct BlackBarSettings
{
    BOOL enabled = FALSE, crop = TRUE, keepSubtitles = TRUE;
    int zoom = 0;                       // 0=fit all active content, 1=fill output (may crop content)
    int position = 0;                   // 0=center, 1=top, 2=bottom: where a picture lower than the output sits
    float threshold = 2.f, delay = 2.f; // percent of decoded SDR white; seconds
    BOOL stableZoom = FALSE;            // no longer used; kept so saved settings and INI files still load
};

// Of the free height above and below a picture, the part above it: all of it
// puts the picture at the bottom (scope screens), none at the top.
inline float SpaceAbove(float free, int position)
{
    return position == 1 ? 0.f : position == 2 ? free : free * .5f;
}

struct BlackBarStatus
{
    RECT active = {};
    bool measured = false, failed = false, protectingSubtitles = false;
    uint64_t samples = 0;
};

inline bool ValidBlackBars(const BlackBarSettings& s)
{
    return (s.enabled == FALSE || s.enabled == TRUE) && (s.crop == FALSE || s.crop == TRUE) &&
           (s.keepSubtitles == FALSE || s.keepSubtitles == TRUE) && (s.stableZoom == FALSE || s.stableZoom == TRUE) &&
           s.zoom >= 0 && s.zoom <= 1 && s.position >= 0 && s.position <= 2 && std::isfinite(s.threshold) &&
           s.threshold >= 0 && s.threshold <= 10 && std::isfinite(s.delay) && s.delay >= 0 && s.delay <= 10;
}
