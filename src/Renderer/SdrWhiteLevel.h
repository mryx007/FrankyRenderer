#pragma once
// Windows' "SDR content brightness" of a display in HDR mode, in nits: how
// bright Windows shows SDR white there (DISPLAYCONFIG_SDR_WHITE_LEVEL, 1000 =
// 80 nits). 0 when unknown.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>

namespace SdrWhiteLevel
{
inline float Nits(const wchar_t* gdiDevice)
{
    if (!gdiDevice || !*gdiDevice)
        return 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result = ERROR_INSUFFICIENT_BUFFER;
    for (int attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER; ++attempt)
    {
        UINT32 pathCount = 0, modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
            return 0;
        paths.resize(pathCount);
        modes.resize(modeCount);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        paths.resize(pathCount);
    }
    if (result != ERROR_SUCCESS)
        return 0;
    for (const auto& path : paths)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME name = {};
        name.header = {
            DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(name), path.sourceInfo.adapterId, path.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS || wcscmp(name.viewGdiDeviceName, gdiDevice))
            continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white = {};
        white.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,
                        sizeof(white),
                        path.targetInfo.adapterId,
                        path.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS || white.SDRWhiteLevel < 1000)
            return 0;
        return float(white.SDRWhiteLevel) / 1000.f * 80.f;
    }
    return 0;
}
} // namespace SdrWhiteLevel
