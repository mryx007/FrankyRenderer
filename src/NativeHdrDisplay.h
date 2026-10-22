#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <libplacebo/colorspace.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include "Trace.h"

// Minimal public NVAPI V1 ABI. See deps/nvapi/LICENSE.txt and source.json.
// Driver-provided NVAPI is loaded from System32; no private API or DLL is shipped.
namespace NativeNvHdr
{
struct Metadata
{
    unsigned short rx, ry, gx, gy, bx, by, wx, wy, maximum, minimum, cll, fall;
};

struct ColorData
{
    unsigned version = sizeof(ColorData) | (1u << 16), command = 0, mode = 0, descriptor = 0;
    Metadata metadata = {};
};

struct Capabilities
{
    unsigned version = sizeof(Capabilities) | (1u << 16), flags = 0, descriptor = 0;
    unsigned short data[11] = {};
};

static_assert(sizeof(ColorData) == 40 && sizeof(Capabilities) == 36);
using Query = void*(__cdecl*)(unsigned);
using Initialize = int(__cdecl*)();
using DisplayId = int(__cdecl*)(const char*, unsigned*);
using Caps = int(__cdecl*)(unsigned, Capabilities*);
using Control = int(__cdecl*)(unsigned, ColorData*);
}

// Temporary driver HDR10/PQ session. No DisplayConfig setters are used.
// Activation is separate from read-only probing. NVIDIA mode 5 is documented
// as experimental; it accepts already PQ/BT.2020-coded RGB, unlike scRGB mode 2.
// Real TV signaling and the Windows HDR preference require hardware validation.
class NativeHdrDisplay
{
    friend struct NativeHdrDisplayTest;

  public:
    enum class Vendor
    {
        None,
        Nvidia
    };

    struct Capabilities
    {
        Vendor vendor = Vendor::None;
        float peak = 0;
        unsigned nvDisplay = 0;
        NativeNvHdr::Metadata displayMetadata = {};

        bool Supported() const
        {
            return vendor != Vendor::None;
        }
    };

  private:
    HMODULE nvModule = nullptr;
    NativeNvHdr::Initialize nvUnload = nullptr;
    NativeNvHdr::DisplayId nvDisplayId = nullptr;
    NativeNvHdr::Caps nvCaps = nullptr;
    NativeNvHdr::Control nvControl = nullptr;
    bool injectedTestApi = false;
    bool initialized = false, active = false, attempted = false;
    HMONITOR monitor = nullptr;
    Capabilities caps;
    NativeNvHdr::ColorData savedNv, appliedNv;
    std::string message;

    void Initialize()
    {
        if (initialized)
            return;
        initialized = true;
        nvModule = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto query =
            nvModule ? reinterpret_cast<NativeNvHdr::Query>(GetProcAddress(nvModule, "nvapi_QueryInterface")) : nullptr;
        if (query)
        {
            const auto init = reinterpret_cast<NativeNvHdr::Initialize>(query(0x0150e828));
            if (init && init() == 0)
            {
                nvUnload = reinterpret_cast<NativeNvHdr::Initialize>(query(0xd22bdd7e));
                nvDisplayId = reinterpret_cast<NativeNvHdr::DisplayId>(query(0xae457190));
                nvCaps = reinterpret_cast<NativeNvHdr::Caps>(query(0x84f2a8df));
                nvControl = reinterpret_cast<NativeNvHdr::Control>(query(0x351da224));
            }
        }
    }

    static unsigned short Number(double value, double scale = 1.)
    {
        return static_cast<unsigned short>(
            std::clamp(std::isfinite(value) ? std::round(value * scale) : 0., 0., 65535.));
    }

  public:
    NativeHdrDisplay() = default;
    NativeHdrDisplay(const NativeHdrDisplay&) = delete;
    NativeHdrDisplay& operator=(const NativeHdrDisplay&) = delete;

    ~NativeHdrDisplay()
    {
        Restore();
        if (nvUnload)
            nvUnload();
        if (nvModule)
            FreeLibrary(nvModule);
    }

    Capabilities Probe(HMONITOR display)
    {
        Initialize();
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (!display || !GetMonitorInfoW(display, &info))
            return {};
        char name[128] = {};
        if (!WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, name, sizeof(name), nullptr, nullptr))
            return {};
        unsigned id = 0;
        NativeNvHdr::Capabilities data;
        if (nvDisplayId && nvCaps && nvControl && nvDisplayId(name, &id) == 0 && nvCaps(id, &data) == 0 &&
            (data.flags & 1))
        {
            Capabilities result{Vendor::Nvidia, float(data.data[8]), id};
            memcpy(&result.displayMetadata.rx, data.data, 8 * sizeof(unsigned short));
            result.displayMetadata.maximum = result.displayMetadata.cll = data.data[8];
            result.displayMetadata.minimum = data.data[9];
            result.displayMetadata.fall = data.data[10];
            return result;
        }
        return {};
    }

    bool Active() const
    {
        return active;
    }

    HMONITOR Monitor() const
    {
        return monitor;
    }

    float Peak() const
    {
        return caps.peak;
    }

    const std::string& Status() const
    {
        return message;
    }

    const Capabilities& Select(HMONITOR display)
    {
        if (monitor != display)
        {
            if (!Restore())
            {
                static const Capabilities unavailable;
                return unavailable;
            }
            monitor = display;
            caps = Probe(display);
        }
        return caps;
    }

    bool Activate(HMONITOR display, const pl_color_space& source)
    {
        // Private test graphs must never switch a physical display.
        if (!injectedTestApi && GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0))
        {
            message = "Direct HDR activation disabled in isolated tests";
            return false;
        }
        if (!Select(display).Supported())
            return false;
        if (attempted && !active)
            return false;
        if (!caps.Supported())
        {
            message = "Direct HDR unavailable on this display";
            return false;
        }
        attempted = true;
        const auto& hdr = source.hdr;
        const auto* primaries = &hdr.prim;
        if (!std::isfinite(primaries->red.x) || primaries->red.x <= 0)
            primaries = pl_raw_primaries_get(source.primaries);
        if (!primaries)
            primaries = pl_raw_primaries_get(PL_COLOR_PRIM_BT_2020);
        if (caps.vendor == Vendor::Nvidia)
        {
            NativeNvHdr::ColorData next;
            next.command = 1;
            next.mode = 5; // PQ/BT.2020 passthrough; no driver scRGB conversion.
            next.metadata = {Number(primaries->red.x, 50000),
                             Number(primaries->red.y, 50000),
                             Number(primaries->green.x, 50000),
                             Number(primaries->green.y, 50000),
                             Number(primaries->blue.x, 50000),
                             Number(primaries->blue.y, 50000),
                             Number(primaries->white.x, 50000),
                             Number(primaries->white.y, 50000),
                             Number(hdr.max_luma),
                             Number(hdr.min_luma, 10000),
                             Number(hdr.max_cll),
                             Number(hdr.max_fall)};
            if (active && !memcmp(&next.metadata, &appliedNv.metadata, sizeof(next.metadata)))
                return true;
            if (!active)
            {
                savedNv = {};
                if (nvControl(caps.nvDisplay, &savedNv) != 0 || savedNv.mode != 0)
                {
                    message = "Direct HDR state unavailable or already owned by another application";
                    return false;
                }
            }
            const int result = nvControl(caps.nvDisplay, &next);
            if (result != 0)
            {
                message = "NVIDIA direct HDR request failed (" + std::to_string(result) + ")";
                return false;
            }
            // SET is asynchronous. An immediate GET may still report OFF.
            // Keep the accepted request, not this transient old driver state.
            appliedNv = next;
            Trace("NVIDIA HDR request accepted display=%u mode=%u max=%u CLL=%u FALL=%u",
                  caps.nvDisplay,
                  next.mode,
                  next.metadata.maximum,
                  next.metadata.cll,
                  next.metadata.fall);
            active = true;
            message = "NVIDIA HDR10/PQ passthrough";
        }
        return active;
    }

    bool Restore()
    {
        if (active && caps.vendor == Vendor::Nvidia)
        {
            NativeNvHdr::ColorData current;
            const int query = nvControl(caps.nvDisplay, &current);
            Trace("HDR restore display=%u query=%d currentMode=%u appliedMode=%u metadataMatch=%d savedMode=%u",
                  caps.nvDisplay,
                  query,
                  current.mode,
                  appliedNv.mode,
                  !memcmp(&current.metadata, &appliedNv.metadata, sizeof(current.metadata)),
                  savedNv.mode);
            if (query != 0)
                return false; // Retain the session so a later redraw can retry.
            // Mode 5 is reported as 2 by current drivers. OFF can also be the
            // still-pending activation; cancel our accepted request then too.
            // Different active metadata belongs to a later external change.
            // Some monitors return EDID/display metadata instead of the source
            // metadata accepted by SET. This is a driver normalization, not
            // evidence that another application replaced our HDR session.
            const bool sourceMetadata = !memcmp(&current.metadata, &appliedNv.metadata, sizeof(current.metadata));
            const bool displayMetadata = caps.displayMetadata.maximum &&
                                         !memcmp(&current.metadata, &caps.displayMetadata, sizeof(current.metadata));
            const bool ownRequest =
                current.mode == 0 || ((current.mode == 2 || current.mode == 5) && (sourceMetadata || displayMetadata));
            if (ownRequest)
            {
                savedNv.command = 1;
                const int result = nvControl(caps.nvDisplay, &savedNv);
                NativeNvHdr::ColorData after;
                const int afterQuery = nvControl(caps.nvDisplay, &after);
                Trace("HDR restore result=%d afterQuery=%d afterMode=%u", result, afterQuery, after.mode);
                if (result != 0)
                    return false;
            }
        }
        active = attempted = false;
        monitor = nullptr;
        caps = {};
        return true;
    }
};
