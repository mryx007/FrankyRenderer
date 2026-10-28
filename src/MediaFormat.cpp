// Color mapping follows MPC Video Renderer Source/Helper.cpp (GPLv3+).
// HDR side-data interfaces are shared publicly by the decoder and renderer.
#include "MediaFormat.h"
#include <d3d9.h>
#include <dxva2api.h>
#include <mfobjects.h>
#include <wrl/client.h>
#include <initguid.h>
#include "DirectShow/Include/IMediaSideData.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>

static constexpr DWORD Code(unsigned char a, unsigned char b, unsigned char c, unsigned char d)
{
    return a | (DWORD(b) << 8) | (DWORD(c) << 16) | (DWORD(d) << 24);
}

static HRESULT MapPixelFormat(const AM_MEDIA_TYPE& type, const BITMAPINFOHEADER& bitmap, PixelFormat& format)
{
    if (type.subtype == MEDIASUBTYPE_RGB24)
    {
        format = PixelFormat::BGR24;
        return S_OK;
    }
    if (type.subtype == MEDIASUBTYPE_RGB32)
    {
        format = PixelFormat::BGRX32;
        return S_OK;
    }
    if (type.subtype == MEDIASUBTYPE_ARGB32)
    {
        format = PixelFormat::BGRA32;
        return S_OK;
    }
    constexpr GUID lavRaw = {0xd80fa03c, 0x35c1, 0x4fa1, {0x8c, 0x8e, 0x37, 0x5c, 0x86, 0x67, 0x16, 0x6e}};
    GUID standard = {type.subtype.Data1, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
    if (type.subtype != standard && type.subtype != lavRaw)
        return VFW_E_TYPE_NOT_ACCEPTED;
    const DWORD code = type.subtype == lavRaw ? bitmap.biCompression : type.subtype.Data1;
    switch (code)
    {
    case Code('N', 'V', '1', '2'):
        format = PixelFormat::NV12;
        break;
    case Code('P', '0', '1', '0'):
        format = PixelFormat::P010;
        break;
    case Code('P', '0', '1', '2'):
        format = PixelFormat::P012;
        break;
    case Code('P', '0', '1', '6'):
        format = PixelFormat::P016;
        break;
    case Code('N', 'V', '1', '6'):
        format = PixelFormat::NV16;
        break;
    case Code('P', '2', '1', '0'):
        format = PixelFormat::P210;
        break;
    case Code('P', '2', '1', '6'):
        format = PixelFormat::P216;
        break;
    case Code('Y', 'V', '1', '2'):
        format = PixelFormat::YV12;
        break;
    case Code('I', '4', '2', '0'):
    case Code('I', 'Y', 'U', 'V'):
        format = PixelFormat::I420;
        break;
    case Code('Y', 'V', '1', '6'):
        format = PixelFormat::YV16;
        break;
    case Code('Y', '4', '2', 'B'):
        format = PixelFormat::Y42B;
        break;
    case Code('Y', 'V', '2', '4'):
        format = PixelFormat::YV24;
        break;
    case Code('4', '4', '4', 'P'):
        format = PixelFormat::YUV444P8;
        break;
    case Code('Y', '3', 11, 10):
        format = PixelFormat::YUV420P10;
        break;
    case Code('Y', '3', 11, 16):
        format = PixelFormat::YUV420P16;
        break;
    case Code('Y', '3', 10, 10):
        format = PixelFormat::YUV422P10;
        break;
    case Code('Y', '3', 10, 16):
        format = PixelFormat::YUV422P16;
        break;
    case Code('Y', '3', 0, 10):
        format = PixelFormat::YUV444P10;
        break;
    case Code('Y', '3', 0, 16):
        format = PixelFormat::YUV444P16;
        break;
    case Code('G', '3', 0, 8):
        format = PixelFormat::GBRP8;
        break;
    case Code('G', '3', 0, 10):
        format = PixelFormat::GBRP10;
        break;
    case Code('G', '3', 0, 16):
        format = PixelFormat::GBRP16;
        break;
    case Code('Y', 'U', 'Y', '2'):
        format = PixelFormat::YUY2;
        break;
    case Code('U', 'Y', 'V', 'Y'):
        format = PixelFormat::UYVY;
        break;
    case Code('Y', '2', '1', '0'):
        format = PixelFormat::Y210;
        break;
    case Code('Y', '4', '1', '0'):
        format = PixelFormat::Y410;
        break;
    case Code('Y', '4', '1', '6'):
        format = PixelFormat::Y416;
        break;
    case Code('Y', '2', '1', '6'):
        format = PixelFormat::Y216;
        break;
    case Code('R', 'G', 'B', 48):
    case Code('b', '4', '8', 'r'):
        format = PixelFormat::RGB48;
        break;
    case Code('B', 'G', 'R', 48):
        format = PixelFormat::BGR48;
        break;
    case Code('B', 'R', 'A', 64):
        format = PixelFormat::BGRA64;
        break;
    case Code('Y', '8', '0', '0'):
    case Code('Y', '8', ' ', ' '):
        format = PixelFormat::GRAY8;
        break;
    case Code('Y', '1', 0, 10):
        format = PixelFormat::GRAY10;
        break;
    case Code('Y', '1', 0, 16):
        format = PixelFormat::GRAY16;
        break;
    default:
        return VFW_E_TYPE_NOT_ACCEPTED;
    }
    return S_OK;
}

HRESULT ParseFormat(const AM_MEDIA_TYPE& type, MediaFormat& out)
{
    if (type.majortype != MEDIATYPE_Video || !type.pbFormat)
        return VFW_E_TYPE_NOT_ACCEPTED;
    MediaFormat next;
    BITMAPINFOHEADER bitmap = {};
    DXVA2_ExtendedFormat color = {};
    if (type.formattype == FORMAT_VideoInfo2 && type.cbFormat >= sizeof(VIDEOINFOHEADER2))
    {
        const auto& v = *reinterpret_cast<const VIDEOINFOHEADER2*>(type.pbFormat);
        bitmap = v.bmiHeader;
        next.averageDuration = v.AvgTimePerFrame;
        next.source = v.rcSource;
        next.frame.aspectX = v.dwPictAspectRatioX;
        next.frame.aspectY = v.dwPictAspectRatioY;
        next.interlaced = (v.dwInterlaceFlags & AMINTERLACE_IsInterlaced) != 0;
        next.topFieldFirst = (v.dwInterlaceFlags & AMINTERLACE_Field1First) != 0;
        if (v.dwControlFlags & AMCONTROL_COLORINFO_PRESENT)
            color.value = v.dwControlFlags;
        next.basic.rcSource = v.rcSource;
        next.basic.rcTarget = v.rcTarget;
        next.basic.AvgTimePerFrame = v.AvgTimePerFrame;
        next.basic.bmiHeader = bitmap;
    }
    else if (type.formattype == FORMAT_VideoInfo && type.cbFormat >= sizeof(VIDEOINFOHEADER))
    {
        next.basic = *reinterpret_cast<const VIDEOINFOHEADER*>(type.pbFormat);
        bitmap = next.basic.bmiHeader;
        next.averageDuration = next.basic.AvgTimePerFrame;
        next.source = next.basic.rcSource;
    }
    else
        return VFW_E_TYPE_NOT_ACCEPTED;
    HRESULT mapped = MapPixelFormat(type, bitmap, next.frame.format);
    if (FAILED(mapped))
        return mapped;
    const auto layout = Layout(next.frame.format);
    if (bitmap.biWidth <= 0 || bitmap.biWidth > 16384 || bitmap.biHeight == 0 || bitmap.biHeight == LONG_MIN)
        return VFW_E_TYPE_NOT_ACCEPTED;
    next.frame.width = bitmap.biWidth;
    next.frame.height = std::abs(bitmap.biHeight);
    if (next.frame.height > 16384 || ((next.frame.width & 1) && layout.chromaX) ||
        ((next.frame.height & 1) && layout.chromaY))
        return VFW_E_TYPE_NOT_ACCEPTED;
    next.frame.stride = MinimumStride(next.frame.format, next.frame.width);
    next.frame.bottomUp = layout.rgb && bitmap.biCompression == BI_RGB && bitmap.biHeight > 0;
    const size_t minimum = ImageBytes(next.frame.format, next.frame.stride, next.frame.height);
    if (bitmap.biSizeImage >= minimum)
    {
        size_t padded = static_cast<size_t>(next.frame.stride) * bitmap.biSizeImage / minimum;
        if (padded < INT_MAX &&
            ImageBytes(next.frame.format, static_cast<int>(padded), next.frame.height) == bitmap.biSizeImage)
            next.frame.stride = static_cast<int>(padded);
    }
    if (IsRectEmpty(&next.source))
        next.source = {0, 0, next.frame.width, next.frame.height};
    if (next.source.left < 0 || next.source.top < 0 || next.source.right > next.frame.width ||
        next.source.bottom > next.frame.height)
        return VFW_E_TYPE_NOT_ACCEPTED;
    if (!next.frame.aspectX || !next.frame.aspectY)
    {
        next.frame.aspectX = next.source.right - next.source.left;
        next.frame.aspectY = next.source.bottom - next.source.top;
    }
    auto& frame = next.frame;
    frame.color.primaries = frame.height <= 576
                                ? (frame.height <= 480 ? PL_COLOR_PRIM_BT_601_525 : PL_COLOR_PRIM_BT_601_625)
                                : PL_COLOR_PRIM_BT_709;
    frame.matrix = layout.rgb            ? PL_COLOR_SYSTEM_RGB
                   : frame.height <= 576 ? PL_COLOR_SYSTEM_BT_601
                                         : PL_COLOR_SYSTEM_BT_709;
    frame.color.transfer = layout.rgb ? PL_COLOR_TRC_SRGB : PL_COLOR_TRC_BT_1886;
    if (layout.rgb)
        frame.levels = PL_COLOR_LEVELS_FULL;
    if (color.NominalRange == DXVA2_NominalRange_0_255)
        frame.levels = PL_COLOR_LEVELS_FULL;
    switch (color.VideoTransferMatrix)
    {
    case DXVA2_VideoTransferMatrix_BT709:
        frame.matrix = PL_COLOR_SYSTEM_BT_709;
        break;
    case DXVA2_VideoTransferMatrix_BT601:
        frame.matrix = PL_COLOR_SYSTEM_BT_601;
        break;
    case MFVideoTransferMatrix_BT2020_10:
    case MFVideoTransferMatrix_BT2020_12:
        frame.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
        break;
    }
    switch (color.VideoPrimaries)
    {
    case DXVA2_VideoPrimaries_BT709:
        frame.color.primaries = PL_COLOR_PRIM_BT_709;
        break;
    case DXVA2_VideoPrimaries_BT470_2_SysBG:
        frame.color.primaries = PL_COLOR_PRIM_BT_601_625;
        break;
    case DXVA2_VideoPrimaries_SMPTE170M:
    case DXVA2_VideoPrimaries_SMPTE240M:
        frame.color.primaries = PL_COLOR_PRIM_BT_601_525;
        break;
    case MFVideoPrimaries_BT2020:
        frame.color.primaries = PL_COLOR_PRIM_BT_2020;
        break;
    case MFVideoPrimaries_DCI_P3:
        frame.color.primaries = PL_COLOR_PRIM_DCI_P3;
        break;
    }
    switch (color.VideoTransferFunction)
    {
    case DXVA2_VideoTransFunc_10:
        frame.color.transfer = PL_COLOR_TRC_LINEAR;
        break;
    case DXVA2_VideoTransFunc_18:
        frame.color.transfer = PL_COLOR_TRC_GAMMA18;
        break;
    case DXVA2_VideoTransFunc_20:
        frame.color.transfer = PL_COLOR_TRC_GAMMA20;
        break;
    case DXVA2_VideoTransFunc_22:
        frame.color.transfer = PL_COLOR_TRC_GAMMA22;
        break;
    case DXVA2_VideoTransFunc_sRGB:
        frame.color.transfer = PL_COLOR_TRC_SRGB;
        break;
    case DXVA2_VideoTransFunc_28:
        frame.color.transfer = PL_COLOR_TRC_GAMMA28;
        break;
    case MFVideoTransFunc_2084:
        frame.color.transfer = PL_COLOR_TRC_PQ;
        break;
    case MFVideoTransFunc_HLG:
        frame.color.transfer = PL_COLOR_TRC_HLG;
        break;
    }
    out = next;
    return S_OK;
}

// Same normalized-value conversion as libplacebo's utils/libav_internal.h.
// Its public frame API supports the global window; localized windows and
// display peak grids have no corresponding fields there.
static bool MapDynamicHdr(const MediaSideDataHDR10Plus& in, pl_hdr_metadata& out)
{
    const auto unit = [](double v) { return std::isfinite(v) && v >= 0 && v <= 1; };
    if (in.itu_t_t35_country_code != 0xb5 || in.application_version > 1 || in.num_windows < 1 || in.num_windows > 3)
        return false;
    const auto& w = in.windows[0];
    if (w.num_distribution_maxrgb_percentiles > 15 || w.tone_mapping_flag > 1 || !unit(w.average_maxrgb))
        return false;
    for (double v : w.maxscl)
        if (!unit(v))
            return false;
    for (unsigned i = 0; i < w.num_distribution_maxrgb_percentiles; i++)
    {
        const auto& p = w.distribution_maxrgb_percentiles[i];
        if (p.percentage > 100 || !unit(p.percentile))
            return false;
        if (i && p.percentage <= w.distribution_maxrgb_percentiles[i - 1].percentage)
            return false;
    }
    if (w.tone_mapping_flag)
    {
        if (!std::isfinite(in.targeted_system_display_maximum_luminance) ||
            in.targeted_system_display_maximum_luminance <= 0 || in.targeted_system_display_maximum_luminance > 10000 ||
            !unit(w.knee_point_x) || !unit(w.knee_point_y) || w.num_bezier_curve_anchors > 15)
            return false;
        for (unsigned i = 0; i < w.num_bezier_curve_anchors; i++)
            if (!unit(w.bezier_curve_anchors[i]))
                return false;
    }
    const unsigned n = w.num_distribution_maxrgb_percentiles;
    const double fallback = n ? w.distribution_maxrgb_percentiles[n - 1].percentile : 0;
    for (unsigned i = 0; i < 3; i++)
        out.scene_max[i] = static_cast<float>(10000 * (w.maxscl[i] ? w.maxscl[i] : fallback));
    out.scene_avg = static_cast<float>(10000 * w.average_maxrgb);
    if (w.tone_mapping_flag)
    {
        out.ootf.target_luma = static_cast<float>(in.targeted_system_display_maximum_luminance);
        out.ootf.knee_x = static_cast<float>(w.knee_point_x);
        out.ootf.knee_y = static_cast<float>(w.knee_point_y);
        out.ootf.num_anchors = static_cast<uint8_t>(w.num_bezier_curve_anchors);
        for (unsigned i = 0; i < w.num_bezier_curve_anchors; i++)
            out.ootf.anchors[i] = static_cast<float>(w.bezier_curve_anchors[i]);
    }
    return true;
}

// Adapted from libplacebo utils/libav_internal.h (LGPL-2.1-or-later):
// map the public DirectShow fixed-point layout without an FFmpeg dependency.
static bool MapDovi(const BYTE* bytes, size_t count, bool extensions, VideoFrame& frame)
{
    constexpr size_t base = offsetof(MediaSideDataDOVIMetadata, Extensions);
    if (!bytes || count < base)
        return false;
    frame.dvSource.present = true;
    if (Layout(frame.format).rgb)
        return false;
    MediaSideDataDOVIMetadata in = {};
    std::memcpy(&in, bytes, std::min(count, sizeof(in)));
    const auto& h = in.Header;
    const auto& color = in.ColorMetadata;
    // This single-layer path cannot reconstruct enhancement-layer residuals.
    if (!h.disable_residual_flag || h.bl_bit_depth < 8 || h.bl_bit_depth > 16 || h.coef_log2_denom > 32 ||
        color.source_max_pq > 4095 || color.source_min_pq >= color.source_max_pq)
        return false;
    auto out = std::make_shared<pl_dovi_metadata>();
    const auto finite = [](double v) { return std::isfinite(v) && std::abs(v) <= 64; };
    for (int i = 0; i < 3; i++)
    {
        if (!finite(color.ycc_to_rgb_offset[i]))
            return false;
        out->nonlinear_offset[i] = static_cast<float>(color.ycc_to_rgb_offset[i]);
    }
    for (int i = 0; i < 9; i++)
    {
        if (!finite(color.ycc_to_rgb_matrix[i]) || !finite(color.rgb_to_lms_matrix[i]))
            return false;
        out->nonlinear.m[i / 3][i % 3] = static_cast<float>(color.ycc_to_rgb_matrix[i]);
        out->linear.m[i / 3][i % 3] = static_cast<float>(color.rgb_to_lms_matrix[i]);
    }
    const double coefficientScale = std::ldexp(1.0, -static_cast<int>(h.coef_log2_denom));
    const unsigned pivotScale = 1u << h.bl_bit_depth;
    const unsigned pivotMax = pivotScale - 1;
    for (int c = 0; c < 3; c++)
    {
        const auto& src = in.Mapping.curves[c];
        auto& dst = out->comp[c];
        if (src.num_pivots < 2 || src.num_pivots > 9)
            return false;
        dst.num_pivots = src.num_pivots;
        for (unsigned p = 0; p < src.num_pivots; p++)
        {
            if (src.pivots[p] > pivotMax || (p && src.pivots[p] <= src.pivots[p - 1]))
                return false;
            // libplacebo 7.387 uses ETSI code units: divide by 2^bit_depth,
            // matching pl_map_avdovi_metadata(), rather than the maximum code.
            dst.pivots[p] = static_cast<float>(double(src.pivots[p]) / pivotScale);
        }
        const auto convert = [&](int64_t value, float& dest)
        {
            double v = value * coefficientScale;
            if (!std::isfinite(v) || std::abs(v) > 1e6)
                return false;
            dest = static_cast<float>(v);
            return true;
        };
        for (unsigned p = 0; p + 1 < src.num_pivots; p++)
        {
            dst.method[p] = src.mapping_idc[p];
            if (dst.method[p] == 0)
            {
                if (src.poly_order[p] < 1 || src.poly_order[p] > 2)
                    return false;
                for (unsigned k = 0; k <= src.poly_order[p]; k++)
                    if (!convert(src.poly_coef[p][k], dst.poly_coeffs[p][k]))
                        return false;
            }
            else if (dst.method[p] == 1)
            {
                if (src.mmr_order[p] < 1 || src.mmr_order[p] > 3)
                    return false;
                dst.mmr_order[p] = src.mmr_order[p];
                if (!convert(src.mmr_constant[p], dst.mmr_constant[p]))
                    return false;
                for (unsigned j = 0; j < src.mmr_order[p]; j++)
                    for (int k = 0; k < 7; k++)
                        if (!convert(src.mmr_coef[p][j][k], dst.mmr_coeffs[p][j][k]))
                            return false;
            }
            else
                return false;
        }
    }
    auto hdr = frame.color.hdr;
    std::fill(std::begin(hdr.scene_max), std::end(hdr.scene_max), 0.f);
    hdr.scene_avg = 0;
    hdr.ootf = {};
    hdr.max_pq_y = hdr.avg_pq_y = 0;
    hdr.min_luma = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, color.source_min_pq / 4095.f);
    hdr.max_luma = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, color.source_max_pq / 4095.f);
    if (extensions)
    {
        const size_t blocks = std::min<size_t>(LAV_DOVI_MAX_EXTENSIONS, (count - base) / sizeof(in.Extensions[0]));
        for (size_t i = 0; i < blocks; i++)
            if (in.Extensions[i].level == 1)
            {
                const auto& l1 = in.Extensions[i].Level1;
                if (l1.max_pq > 4095 || l1.avg_pq > l1.max_pq || l1.min_pq > l1.max_pq)
                    return false;
                hdr.max_pq_y = l1.max_pq / 4095.f;
                hdr.avg_pq_y = l1.avg_pq / 4095.f;
                break;
            }
    }
    frame.dovi = std::move(out);
    frame.color.primaries = PL_COLOR_PRIM_BT_2020;
    frame.color.transfer = PL_COLOR_TRC_PQ;
    frame.color.hdr = hdr;
    return true;
}

HdrSideDataStatus ReadHdrSideData(IMediaSample* sample, VideoFrame& frame)
{
    // Dynamic data belongs to this sample only, never to the next scene.
    std::fill(std::begin(frame.color.hdr.scene_max), std::end(frame.color.hdr.scene_max), 0.f);
    frame.color.hdr.scene_avg = 0;
    frame.color.hdr.ootf = {};
    frame.color.hdr.max_pq_y = frame.color.hdr.avg_pq_y = 0;
    frame.dovi.reset();
    frame.dvSource = {};
    frame.dvLayered = false;
    HdrSideDataStatus status;
    Microsoft::WRL::ComPtr<IMediaSideData> side;
    status.interfaceResult = sample->QueryInterface(IID_PPV_ARGS(&side));
    if (FAILED(status.interfaceResult))
        return status;
    const BYTE* data = nullptr;
    size_t size = 0;
    status.masteringResult = side->GetSideData(IID_MediaSideDataHDR, &data, &size);
    status.masteringBytes = size;
    if (SUCCEEDED(status.masteringResult) && data && size == sizeof(MediaSideDataHDR))
    {
        const auto& hdr = *reinterpret_cast<const MediaSideDataHDR*>(data);
        auto& out = frame.color.hdr;
        out.prim.red = {static_cast<float>(hdr.display_primaries_x[2]), static_cast<float>(hdr.display_primaries_y[2])};
        out.prim.green = {static_cast<float>(hdr.display_primaries_x[0]),
                          static_cast<float>(hdr.display_primaries_y[0])};
        out.prim.blue = {static_cast<float>(hdr.display_primaries_x[1]),
                         static_cast<float>(hdr.display_primaries_y[1])};
        out.prim.white = {static_cast<float>(hdr.white_point_x), static_cast<float>(hdr.white_point_y)};
        out.max_luma = static_cast<float>(hdr.max_display_mastering_luminance);
        out.min_luma = static_cast<float>(hdr.min_display_mastering_luminance);
        status.mastering = true;
    }
    data = nullptr;
    size = 0;
    status.lightResult = side->GetSideData(IID_MediaSideDataHDRContentLightLevel, &data, &size);
    status.lightBytes = size;
    if (SUCCEEDED(status.lightResult) && data && size == sizeof(MediaSideDataHDRContentLightLevel))
    {
        const auto& hdr = *reinterpret_cast<const MediaSideDataHDRContentLightLevel*>(data);
        frame.color.hdr.max_cll = static_cast<float>(hdr.MaxCLL);
        frame.color.hdr.max_fall = static_cast<float>(hdr.MaxFALL);
        status.light = true;
    }
    data = nullptr;
    size = 0;
    status.dynamicResult = side->GetSideData(IID_MediaSideDataHDR10Plus, &data, &size);
    status.dynamicBytes = size;
    if (FAILED(status.dynamicResult))
    {
        // Documented predecessor: same payload without country/version words.
        constexpr GUID legacy = {0x183ed511, 0x8910, 0x4262, {0x88, 0xf6, 0x49, 0x46, 0xbc, 0x79, 0x9c, 0x84}};
        data = nullptr;
        size = 0;
        status.dynamicResult = side->GetSideData(legacy, &data, &size);
        status.dynamicBytes = size;
        status.dynamicLegacy = true;
    }
    if (SUCCEEDED(status.dynamicResult))
    {
        constexpr size_t prefix = offsetof(MediaSideDataHDR10Plus, num_windows);
        const size_t offset = status.dynamicLegacy ? prefix : 0;
        if (data && size == sizeof(MediaSideDataHDR10Plus) - offset)
        {
            MediaSideDataHDR10Plus hdr = {};
            hdr.itu_t_t35_country_code = 0xb5;
            std::memcpy(reinterpret_cast<BYTE*>(&hdr) + offset, data, size);
            status.dynamic = MapDynamicHdr(hdr, frame.color.hdr);
        }
        if (!status.dynamic)
            status.dynamicResult = E_INVALIDARG;
    }
    data = nullptr;
    size = 0;
    bool extensions = true;
    status.doviResult = side->GetSideData(IID_MediaSideDataDOVIMetadataV2, &data, &size);
    if (FAILED(status.doviResult))
    {
        constexpr GUID legacy = {0x277ee779, 0x13f4, 0x434e, {0xbd, 0xec, 0x3d, 0x6f, 0x8c, 0x0e, 0x15, 0xd2}};
        data = nullptr;
        size = 0;
        extensions = false;
        status.doviResult = side->GetSideData(legacy, &data, &size);
    }
    status.doviBytes = size;
    if (SUCCEEDED(status.doviResult))
    {
        status.dovi = MapDovi(data, size, extensions, frame);
        if (!status.dovi)
            status.doviResult = E_INVALIDARG;
    }
    return status;
}
