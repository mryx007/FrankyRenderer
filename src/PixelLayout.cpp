#include "PixelLayout.h"

PixelLayout Layout(PixelFormat format)
{
    PixelLayout p;
    switch (format)
    {
    case PixelFormat::NV12:
        p.name = "NV12";
        break;
    case PixelFormat::P010:
        p.name = "P010";
        p.bytes = 2;
        p.depth = 10;
        p.shift = 6;
        break;
    case PixelFormat::P012:
        p.name = "P012";
        p.bytes = 2;
        p.depth = 12;
        p.shift = 4;
        break;
    case PixelFormat::P016:
        p.name = "P016";
        p.bytes = 2;
        p.depth = 16;
        break;
    case PixelFormat::NV16:
        p.name = "NV16";
        p.chromaY = 0;
        break;
    case PixelFormat::P210:
        p.name = "P210";
        p.chromaY = 0;
        p.bytes = 2;
        p.depth = 10;
        p.shift = 6;
        break;
    case PixelFormat::P216:
        p.name = "P216";
        p.chromaY = 0;
        p.bytes = 2;
        p.depth = 16;
        break;
    case PixelFormat::YV12:
        p.name = "YV12";
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        p.vu = true;
        break;
    case PixelFormat::I420:
        p.name = "I420";
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        break;
    case PixelFormat::YV16:
        p.name = "YV16";
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        p.chromaY = 0;
        p.vu = true;
        break;
    case PixelFormat::Y42B:
        p.name = "Y42B";
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        p.chromaY = 0;
        break;
    case PixelFormat::YV24:
        p.name = "YV24";
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        p.chromaX = p.chromaY = 0;
        p.vu = true;
        break;
    case PixelFormat::YUV444P8:
        p.name = "444P";
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        p.chromaX = p.chromaY = 0;
        break;
    case PixelFormat::YUV420P10:
    case PixelFormat::YUV420P16:
    case PixelFormat::YUV422P10:
    case PixelFormat::YUV422P16:
    case PixelFormat::YUV444P10:
    case PixelFormat::YUV444P16:
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        p.bytes = 2;
        p.depth =
            (format == PixelFormat::YUV420P10 || format == PixelFormat::YUV422P10 || format == PixelFormat::YUV444P10)
                ? 10
                : 16;
        if (format == PixelFormat::YUV422P10 || format == PixelFormat::YUV422P16)
            p.chromaY = 0;
        if (format == PixelFormat::YUV444P10 || format == PixelFormat::YUV444P16)
            p.chromaX = p.chromaY = 0;
        p.name = p.chromaY   ? (p.depth == 10 ? "420P10" : "420P16")
                 : p.chromaX ? (p.depth == 10 ? "422P10" : "422P16")
                             : (p.depth == 10 ? "444P10" : "444P16");
        break;
    case PixelFormat::GBRP8:
    case PixelFormat::GBRP10:
    case PixelFormat::GBRP16:
        p.name = format == PixelFormat::GBRP8 ? "GBRP8" : format == PixelFormat::GBRP10 ? "GBRP10" : "GBRP16";
        p.packing = PixelPacking::Planar;
        p.planes = 3;
        p.chromaX = p.chromaY = 0;
        p.rgb = true;
        p.bytes = format == PixelFormat::GBRP8 ? 1 : 2;
        p.depth = format == PixelFormat::GBRP8 ? 8 : format == PixelFormat::GBRP10 ? 10 : 16;
        break;
    case PixelFormat::YUY2:
    case PixelFormat::UYVY:
    case PixelFormat::Y210:
    case PixelFormat::Y216:
        p.name = format == PixelFormat::YUY2   ? "YUY2"
                 : format == PixelFormat::UYVY ? "UYVY"
                 : format == PixelFormat::Y210 ? "Y210"
                                               : "Y216";
        p.packing = PixelPacking::Packed422;
        p.planes = 2;
        p.chromaY = 0;
        p.bytes = (format == PixelFormat::Y210 || format == PixelFormat::Y216) ? 2 : 1;
        p.depth = format == PixelFormat::Y210 ? 10 : format == PixelFormat::Y216 ? 16 : 8;
        p.shift = format == PixelFormat::Y210 ? 6 : 0;
        p.vu = format == PixelFormat::UYVY;
        break;
    case PixelFormat::BGR24:
    case PixelFormat::BGRX32:
    case PixelFormat::BGRA32:
    case PixelFormat::RGB48:
    case PixelFormat::BGR48:
    case PixelFormat::BGRA64:
        p.packing = PixelPacking::PackedRGB;
        p.rgb = true;
        p.planes = 1;
        p.chromaX = p.chromaY = 0;
        p.bytes =
            (format == PixelFormat::RGB48 || format == PixelFormat::BGR48 || format == PixelFormat::BGRA64) ? 2 : 1;
        p.depth = p.bytes * 8;
        p.alpha = format == PixelFormat::BGRA32 || format == PixelFormat::BGRA64;
        p.channels =
            (format == PixelFormat::BGR24 || format == PixelFormat::RGB48 || format == PixelFormat::BGR48) ? 3 : 4;
        p.vu = format != PixelFormat::RGB48;
        p.name = format == PixelFormat::BGR24    ? "RGB24"
                 : format == PixelFormat::BGRX32 ? "RGB32"
                 : format == PixelFormat::BGRA32 ? "ARGB32"
                 : format == PixelFormat::RGB48  ? "RGB48"
                 : format == PixelFormat::BGR48  ? "BGR48"
                                                 : "BGRA64";
        break;
    case PixelFormat::Y410:
    case PixelFormat::Y416:
        p.name = format == PixelFormat::Y410 ? "Y410" : "Y416";
        p.packing = PixelPacking::Packed444;
        p.planes = 1;
        p.bytes = 2;
        p.depth = format == PixelFormat::Y410 ? 10 : 16;
        p.shift = format == PixelFormat::Y410 ? 6 : 0;
        p.chromaX = p.chromaY = 0;
        p.channels = 4;
        p.alpha = true;
        break;
    case PixelFormat::GRAY8:
    case PixelFormat::GRAY10:
    case PixelFormat::GRAY16:
        p.name = format == PixelFormat::GRAY8 ? "Y8" : format == PixelFormat::GRAY10 ? "Y10" : "Y16";
        p.packing = PixelPacking::Gray;
        p.planes = 1;
        p.bytes = format == PixelFormat::GRAY8 ? 1 : 2;
        p.depth = format == PixelFormat::GRAY8 ? 8 : format == PixelFormat::GRAY10 ? 10 : 16;
        p.chromaX = p.chromaY = 0;
        break;
    }
    return p;
}

int MinimumStride(PixelFormat format, int width)
{
    auto p = Layout(format);
    int stride = width * p.bytes;
    if (format == PixelFormat::Y410)
        return width * 4;
    if (p.packing == PixelPacking::Packed422)
        stride *= 2;
    if (p.packing == PixelPacking::PackedRGB || p.packing == PixelPacking::Packed444)
        stride *= p.channels;
    if (format == PixelFormat::BGR24 || format == PixelFormat::BGRX32 || format == PixelFormat::BGRA32)
        stride = (stride + 3) & ~3;
    return stride;
}

size_t ImageBytes(PixelFormat format, int stride, int height)
{
    auto p = Layout(format);
    size_t size = static_cast<size_t>(stride) * height;
    if (p.packing == PixelPacking::SemiPlanar)
        size += static_cast<size_t>(stride) * (height >> p.chromaY);
    else if (p.packing == PixelPacking::Planar)
        size += 2 * static_cast<size_t>(stride >> p.chromaX) * (height >> p.chromaY);
    return size;
}
