#pragma once
// What Windows sends to the display that shows a monitor: bits per colour
// (the GPU driver's output colour depth) and whether Windows HDR is on.
// Read from DXGI; bits is 0 when unknown.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdlib>
#pragma comment(lib, "dxgi.lib")

namespace DisplayBitDepth
{
struct Info
{
    int bits = 0;
    bool hdr = false;
};

inline Info Read(HMONITOR monitor)
{
    Info info;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (!monitor || FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return info;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) == S_OK; ++a, adapter.Reset())
    {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) == S_OK; ++o, output.Reset())
        {
            Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc = {};
            if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&desc)) || desc.Monitor != monitor)
                continue;
            info.bits = int(desc.BitsPerColor);
            info.hdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
#ifdef PLACEBO_TEST_PRESENT_PIXELS
            // Tests pretend a display with another bit depth.
            wchar_t bits[8] = {};
            if (GetEnvironmentVariableW(L"PLACEBO_TEST_DISPLAY_BITS", bits, 8))
                info.bits = _wtoi(bits);
#endif
            return info;
        }
    }
    return info;
}
} // namespace DisplayBitDepth
