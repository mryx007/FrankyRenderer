#pragma once
#include <cmath>

// Shared bounds for the COM settings and the direct backend API.
template <class Settings> bool ValidHdrTuning(const Settings& s)
{
    const auto range = [](float v, float low, float high) { return std::isfinite(v) && v >= low && v <= high; };
    return range(s.kneeAdaptation, 0, 1) && range(s.splineContrast, 0, 1.5f) && std::isfinite(s.kneeMinimum) &&
           s.kneeMinimum > 0 && s.kneeMinimum < .5f && std::isfinite(s.kneeMaximum) && s.kneeMaximum > .5f &&
           s.kneeMaximum < 1 && range(s.blackCutoff, 0, 100) && range(s.perceptualStrength, 0, 1) &&
           range(s.softclipKnee, 0, 1);
}
