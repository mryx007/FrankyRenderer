#pragma once
#include <windows.h>
#include <algorithm>

namespace SettingsLayout
{
inline constexpr float SidebarWidth = 200.f;
inline constexpr float RowHeight = 70.f;

// Work area is in physical pixels; preferred is measured at the initial DPI.
inline SIZE ContentSize(SIZE preferred, UINT initialDpi, UINT dpi)
{
    return {std::min(MulDiv(preferred.cx, dpi, initialDpi), MulDiv(960, dpi, 96)),
            std::min(MulDiv(preferred.cy, dpi, initialDpi), MulDiv(620, dpi, 96))};
}

inline SIZE FitFrame(SIZE frame, SIZE work, UINT dpi)
{
    const int margin = MulDiv(24, dpi, 96);
    return {std::max(1L, std::min(frame.cx, std::min(work.cx - 2 * margin, work.cx * 9 / 10))),
            std::max(1L, std::min(frame.cy, std::min(work.cy - 2 * margin, work.cy * 9 / 10)))};
}
}
