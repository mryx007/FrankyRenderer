#pragma once
#include <cstddef>
enum class PixelFormat
{
    NV12,
    P010,
    P012,
    P016,
    NV16,
    P210,
    P216,
    YV12,
    I420,
    YV16,
    Y42B,
    YV24,
    YUV444P8,
    YUV420P10,
    YUV420P16,
    YUV422P10,
    YUV422P16,
    YUV444P10,
    YUV444P16,
    GBRP8,
    GBRP10,
    GBRP16,
    YUY2,
    UYVY,
    Y210,
    Y216,
    BGR24,
    BGRX32,
    BGRA32,
    RGB48,
    BGR48,
    BGRA64,
    Y410,
    Y416,
    GRAY8,
    GRAY10,
    GRAY16,
};
enum class PixelPacking
{
    SemiPlanar,
    Planar,
    Packed422,
    Packed444,
    PackedRGB,
    Gray
};

struct PixelLayout
{
    const char* name = "";
    PixelPacking packing = PixelPacking::SemiPlanar;
    int planes = 2, chromaX = 1, chromaY = 1, bytes = 1, depth = 8, shift = 0;
    bool vu = false, rgb = false, alpha = false;
    int channels = 0;
};

PixelLayout Layout(PixelFormat format);
int MinimumStride(PixelFormat format, int width);
size_t ImageBytes(PixelFormat format, int stride, int height);
