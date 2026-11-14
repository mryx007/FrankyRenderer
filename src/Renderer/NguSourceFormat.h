#pragma once

#include "Renderer/PixelLayout.h"
#include "Renderer/NguScaler.h"
#include <dxgi.h>
#include <cstdint>
#include <vector>

// Common format adapter for the native scaler boundary. Pixel-format names are
// translated here once; Backend and NguScaler consume only this canonical
// layout. Unlisted formats stay unsupported until their source path has
// Evidence and a same-frame gate.
struct NguSourceFormatLayout
{
    int lumaBits = 0;
    int chromaBits = 0;
    int chromaWidth = 0;
    int chromaHeight = 0;
    NguChromaLayout chromaLayout = NguChromaLayout::None;
    DXGI_FORMAT decoderFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT cpuLumaFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT cpuChromaFormat = DXGI_FORMAT_UNKNOWN;
    bool planarChroma = false;
};

inline bool DescribeNguSourceFormat(PixelFormat format, int width, int height, NguSourceFormatLayout& out)
{
    out = {};
    const PixelLayout pixelLayout = Layout(format);
    if (width <= 0 || height <= 0 || ((width & 1) && pixelLayout.chromaX) || ((height & 1) && pixelLayout.chromaY))
        return false;

    out.chromaWidth = width / 2;
    out.chromaHeight = height / 2;
    switch (format)
    {
    case PixelFormat::NV12:
        out.lumaBits = out.chromaBits = 8;
        out.chromaLayout = NguChromaLayout::SemiPlanar420;
        out.decoderFormat = DXGI_FORMAT_NV12;
        out.cpuLumaFormat = DXGI_FORMAT_R8_UNORM;
        out.cpuChromaFormat = DXGI_FORMAT_R8G8_UNORM;
        return true;
    case PixelFormat::I420:
        out.lumaBits = out.chromaBits = 8;
        out.chromaLayout = NguChromaLayout::SemiPlanar420;
        out.cpuLumaFormat = DXGI_FORMAT_R8_UNORM;
        out.cpuChromaFormat = DXGI_FORMAT_R8G8_UNORM;
        out.planarChroma = true;
        return true;
    case PixelFormat::P010:
        out.lumaBits = out.chromaBits = 10;
        out.chromaLayout = NguChromaLayout::SemiPlanar420;
        out.decoderFormat = DXGI_FORMAT_P010;
        out.cpuLumaFormat = DXGI_FORMAT_R16_UNORM;
        out.cpuChromaFormat = DXGI_FORMAT_R16G16_UNORM;
        return true;
    case PixelFormat::P016:
        out.lumaBits = out.chromaBits = 16;
        out.chromaLayout = NguChromaLayout::SemiPlanar420;
        out.decoderFormat = DXGI_FORMAT_P016;
        out.cpuLumaFormat = DXGI_FORMAT_R16_UNORM;
        out.cpuChromaFormat = DXGI_FORMAT_R16G16_UNORM;
        return true;
    case PixelFormat::P210:
        out.lumaBits = out.chromaBits = 10;
        out.chromaHeight = height;
        out.chromaLayout = NguChromaLayout::SemiPlanar422;
        out.cpuLumaFormat = DXGI_FORMAT_R16_UNORM;
        out.cpuChromaFormat = DXGI_FORMAT_R16G16_UNORM;
        return true;
    case PixelFormat::P216:
        out.lumaBits = out.chromaBits = 16;
        out.chromaHeight = height;
        out.chromaLayout = NguChromaLayout::SemiPlanar422;
        out.cpuLumaFormat = DXGI_FORMAT_R16_UNORM;
        out.cpuChromaFormat = DXGI_FORMAT_R16G16_UNORM;
        return true;
    case PixelFormat::RGB48:
        out.lumaBits = out.chromaBits = 16;
        out.chromaWidth = out.chromaHeight = 0;
        out.chromaLayout = NguChromaLayout::None;
        return true;
    default:
        out = {};
        return false;
    }
}

inline bool PackNguCpuChroma(const NguSourceFormatLayout& layout,
                             const uint8_t* data,
                             int stride,
                             int lumaHeight,
                             std::vector<uint8_t>& planarScratch,
                             const uint8_t*& uvData,
                             UINT& uvPitch)
{
    if (!data || stride <= 0 || lumaHeight <= 0 || layout.chromaWidth <= 0 || layout.chromaHeight <= 0 ||
        layout.chromaLayout == NguChromaLayout::None)
        return false;

    uvData = data + size_t(stride) * lumaHeight;
    uvPitch = UINT(stride);
    if (!layout.planarChroma)
        return true;

    const int chromaStride = stride >> 1;
    const uint8_t* uPlane = data + size_t(stride) * lumaHeight;
    const uint8_t* vPlane = uPlane + size_t(chromaStride) * layout.chromaHeight;
    const size_t packedStride = size_t(layout.chromaWidth) * 2;
    planarScratch.resize(packedStride * layout.chromaHeight);
    for (int row = 0; row < layout.chromaHeight; ++row)
    {
        auto* destination = planarScratch.data() + size_t(row) * packedStride;
        const auto* u = uPlane + size_t(row) * chromaStride;
        const auto* v = vPlane + size_t(row) * chromaStride;
        for (int column = 0; column < layout.chromaWidth; ++column)
        {
            destination[column * 2] = u[column];
            destination[column * 2 + 1] = v[column];
        }
    }
    uvData = planarScratch.data();
    uvPitch = UINT(packedStride);
    return true;
}
