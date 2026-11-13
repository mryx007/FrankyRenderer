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

interface __declspec(uuid("87179598-ABF6-43F8-8D02-226AC504AB27")) IPlaceboDiagnostics : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDiagnostics(PlaceboDiagnostics*) = 0;
};
