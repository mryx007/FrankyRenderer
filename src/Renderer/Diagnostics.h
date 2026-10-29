#pragma once
#include <windows.h>
#include <unknwn.h>
#include <cstdint>
enum class SourceSignal : unsigned
{
    SDR,
    HDR10,
    HLG,
    HDR10Plus,
    DolbyVision
};

struct PlaceboDiagnostics
{
    unsigned size = sizeof(PlaceboDiagnostics);
    BOOL haveVideo = FALSE, directGpu = FALSE;
    int width = 0, height = 0, bitDepth = 0, deinterlacing = 0;
    SourceSignal signal = SourceSignal::SDR;
    char pixelFormat[32] = {};
    float sourcePeak = 0, scenePeak = 0;
    uint64_t frames = 0;
    HRESULT result = S_OK;
    int outputTextureFormat = 0;
    BOOL smoothMotion = FALSE;
    float displayHz = 0;
    int outputLevels = 0, ditherDepth = 0;
    int sharpenState = 0;
};

inline const wchar_t* SignalName(SourceSignal signal)
{
    switch (signal)
    {
    case SourceSignal::HDR10:
        return L"HDR10 / PQ";
    case SourceSignal::HLG:
        return L"HLG";
    case SourceSignal::HDR10Plus:
        return L"HDR10+";
    case SourceSignal::DolbyVision:
        return L"Dolby Vision";
    default:
        return L"SDR";
    }
}

interface __declspec(uuid("87179598-ABF6-43F8-8D02-226AC504AB27")) IPlaceboDiagnostics : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDiagnostics(PlaceboDiagnostics*) = 0;
};
