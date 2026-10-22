#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>

struct BlackBarSettings
{
    BOOL enabled = FALSE, crop = TRUE, keepSubtitles = TRUE;
    int zoom = 0;                       // 0=fit all active content, 1=fill output (may crop content)
    float threshold = 2.f, delay = 2.f; // percent of decoded SDR white; seconds
    BOOL stableZoom = FALSE;
};

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
           s.zoom >= 0 && s.zoom <= 1 && std::isfinite(s.threshold) && s.threshold >= 0 && s.threshold <= 10 &&
           std::isfinite(s.delay) && s.delay >= 0 && s.delay <= 10;
}
