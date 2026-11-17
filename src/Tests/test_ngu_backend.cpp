#define NOMINMAX
#include "Renderer/Backend.h"
#include "Renderer/NguScaler.h"
#include "Renderer/NguSourceFormat.h"
#include "Renderer/RenderOptions.h"
#include "Renderer/Version.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/colorspace.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path, size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open capture");
    Require(size_t(file.tellg()) == expected, "capture size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(expected))), "read capture");
    return bytes;
}

std::vector<uint8_t> ReadI420AsNv12(const std::filesystem::path& path, int width, int height)
{
    Require(width > 0 && height > 0 && !(width & 1) && !(height & 1), "I420 geometry");
    const size_t lumaBytes = size_t(width) * height;
    const size_t planeBytes = size_t(width / 2) * (height / 2);
    const auto i420 = ReadBytes(path, lumaBytes + planeBytes * 2);
    std::vector<uint8_t> nv12(lumaBytes + planeBytes * 2);
    std::memcpy(nv12.data(), i420.data(), lumaBytes);
    const uint8_t* u = i420.data() + lumaBytes;
    const uint8_t* v = u + planeBytes;
    uint8_t* uv = nv12.data() + lumaBytes;
    for (size_t row = 0; row < size_t(height / 2); ++row)
    {
        for (size_t column = 0; column < size_t(width / 2); ++column)
        {
            const size_t source = row * size_t(width / 2) + column;
            const size_t destination = source * 2;
            uv[destination] = u[source];
            uv[destination + 1] = v[source];
        }
    }
    return nv12;
}

std::vector<uint8_t> BgraToRgba(const std::vector<uint8_t>& bgra)
{
    Require(bgra.size() % 4 == 0, "BGRA payload");
    std::vector<uint8_t> rgba(bgra.size());
    for (size_t offset = 0; offset < bgra.size(); offset += 4)
    {
        rgba[offset] = bgra[offset + 2];
        rgba[offset + 1] = bgra[offset + 1];
        rgba[offset + 2] = bgra[offset];
        rgba[offset + 3] = 255;
    }
    return rgba;
}

std::vector<uint8_t> ReadBmp32AsTopDownRgba(const std::filesystem::path& path)
{
    const auto fileSize = std::filesystem::file_size(path);
    Require(fileSize >= 54 && fileSize <= UINT32_MAX, "BMP size");
    const auto bmp = ReadBytes(path, size_t(fileSize));
    Require(bmp[0] == 'B' && bmp[1] == 'M', "BMP signature");
    uint32_t offset = 0;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t bits = 0;
    uint32_t compression = 0;
    std::memcpy(&offset, bmp.data() + 10, sizeof(offset));
    std::memcpy(&width, bmp.data() + 18, sizeof(width));
    std::memcpy(&height, bmp.data() + 22, sizeof(height));
    std::memcpy(&bits, bmp.data() + 28, sizeof(bits));
    std::memcpy(&compression, bmp.data() + 30, sizeof(compression));
    Require(width > 0 && height != 0 && bits == 32 && compression == 0, "BMP format");
    const size_t absoluteHeight = size_t(height < 0 ? -int64_t(height) : height);
    const size_t stride = size_t(width) * 4;
    Require(offset <= bmp.size() && size_t(width) * absoluteHeight * 4 <= bmp.size() - offset, "BMP payload");
    std::vector<uint8_t> rgba(size_t(width) * absoluteHeight * 4);
    for (size_t y = 0; y < absoluteHeight; ++y)
    {
        const size_t sourceY = height > 0 ? absoluteHeight - 1 - y : y;
        const uint8_t* source = bmp.data() + offset + sourceY * stride;
        uint8_t* destination = rgba.data() + y * stride;
        for (int x = 0; x < width; ++x)
        {
            destination[size_t(x) * 4] = source[size_t(x) * 4 + 2];
            destination[size_t(x) * 4 + 1] = source[size_t(x) * 4 + 1];
            destination[size_t(x) * 4 + 2] = source[size_t(x) * 4];
            destination[size_t(x) * 4 + 3] = 255;
        }
    }
    return rgba;
}

std::filesystem::path FindFinalCapture(const std::filesystem::path& root)
{
    std::filesystem::path result;
    int matches = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.find("PS_1352_6992f6f0") == std::string::npos || name.find("3840x2160") == std::string::npos ||
            entry.file_size() != size_t(3840) * 2160 * 4)
            continue;
        result = entry.path();
        ++matches;
    }
    Require(matches > 0, "final capture");
    return result;
}

int CompareRgb(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int& maximum, int& overOne)
{
    Require(actual.size() == expected.size() && actual.size() % 4 == 0, "final comparison size");
    maximum = 0;
    overOne = 0;
    int different = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 4)
    {
        bool pixelOverOne = false;
        for (int channel = 0; channel < 3; ++channel)
        {
            const int delta = std::abs(int(actual[offset + channel]) - int(expected[offset + channel]));
            if (delta)
                ++different;
            maximum = std::max(maximum, delta);
            pixelOverOne = pixelOverOne || delta > 1;
        }
        overOne += pixelOverOne;
    }
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || argc == 5 || argc == 10 || argc == 14,
                "usage: test_ngu_backend <capture-root> "
                "[--nv12-oracle <i420> <madVR-bmp>] "
                "[p210-raw p210-capture p216-raw p216-capture "
                "p210-raw2 p210-capture2 p216-raw2 p216-capture2 "
                "p210-1080-raw p210-1080-capture "
                "p216-1080-raw p216-1080-capture]");
        const bool nv12Oracle = argc == 5;
        if (nv12Oracle)
            Require(std::strcmp(argv[2], "--nv12-oracle") == 0, "NV12 oracle option");
        Require(ResolveNativeScaler("ngu_sharp") == NativeScalerKind::NguSharp, "native scaler resolver NGU");
        Require(ResolveNativeScaler("ewa_lanczos") == NativeScalerKind::None, "native scaler resolver ordinary filter");
        NguSourceFormatLayout formatLayout;
        Require(DescribeNguSourceFormat(PixelFormat::NV12, 1280, 720, formatLayout) &&
                    formatLayout.chromaLayout == NguChromaLayout::SemiPlanar420 &&
                    formatLayout.decoderFormat == DXGI_FORMAT_NV12 && formatLayout.lumaBits == 8,
                "canonical NV12 adapter");
        Require(DescribeNguSourceFormat(PixelFormat::I420, 1280, 720, formatLayout) && formatLayout.planarChroma &&
                    formatLayout.chromaWidth == 640 && formatLayout.chromaHeight == 360,
                "canonical I420 adapter");
        Require(DescribeNguSourceFormat(PixelFormat::P010, 1920, 1080, formatLayout) &&
                    formatLayout.decoderFormat == DXGI_FORMAT_P010 && formatLayout.lumaBits == 10,
                "canonical P010 adapter");
        Require(DescribeNguSourceFormat(PixelFormat::P016, 1280, 720, formatLayout) &&
                    formatLayout.decoderFormat == DXGI_FORMAT_P016 && formatLayout.lumaBits == 16,
                "canonical P016 adapter");
        Require(DescribeNguSourceFormat(PixelFormat::P210, 1280, 720, formatLayout) &&
                    formatLayout.chromaLayout == NguChromaLayout::SemiPlanar422 && formatLayout.chromaHeight == 720,
                "canonical P210 adapter");
        Require(DescribeNguSourceFormat(PixelFormat::RGB48, 1920, 1080, formatLayout) &&
                    formatLayout.chromaLayout == NguChromaLayout::None && formatLayout.lumaBits == 16,
                "canonical RGB48 adapter");
        Require(!DescribeNguSourceFormat(PixelFormat::YV12, 1280, 720, formatLayout),
                "unobserved YV12 remains unsupported");

        const uint8_t planar420[] = {
            1,
            2,
            3,
            4, // Y row 0
            5,
            6,
            7,
            8, // Y row 1
            9,
            10, // U
            11,
            12, // V
        };
        const uint8_t* packedUv = nullptr;
        UINT packedPitch = 0;
        std::vector<uint8_t> planarScratch;
        Require(DescribeNguSourceFormat(PixelFormat::I420, 4, 2, formatLayout), "small I420 adapter");
        Require(PackNguCpuChroma(formatLayout, planar420, 4, 2, planarScratch, packedUv, packedPitch),
                "I420 CPU chroma interleave");
        Require(packedPitch == 4 && planarScratch.size() == 4 && packedUv == planarScratch.data() &&
                    planarScratch[0] == 9 && planarScratch[1] == 11 && planarScratch[2] == 10 && planarScratch[3] == 12,
                "I420 CPU chroma ordering");
        const auto root = std::filesystem::path(argv[1]);
        const auto rgba16 = ReadBytes(root / "input_00224_PS_460_bfe50921_s0.bin", size_t(1280) * 720 * 8);
        NguScaler planner;
        NguSourceFrame planSource;
        planSource.width = 1280;
        planSource.height = 720;
        planSource.lumaBits = 8;
        planSource.chromaBits = 8;
        planSource.chromaLayout = NguChromaLayout::SemiPlanar420;
        const auto doublePlan = planner.PlanNativeVeryHigh(planSource, 2560, 1440);
        Require(doublePlan.double720 && !doublePlan.noLuma && doublePlan.outputHook && doublePlan.outputWidth == 2560 &&
                    doublePlan.outputHeight == 1440,
                "canonical 720 Double plan");
        const auto windowPlan = planner.PlanNativeVeryHigh(planSource, 1600, 900);
        Require(!windowPlan.double720 && !windowPlan.noLuma && !windowPlan.outputHook &&
                    windowPlan.outputWidth == 3840 && windowPlan.outputHeight == 2160,
                "canonical 720 window plan");
        planSource.width = 1920;
        planSource.height = 1080;
        const auto noLumaPlan = planner.PlanNativeVeryHigh(planSource, 1920, 1080);
        Require(noLumaPlan.noLuma && noLumaPlan.outputHook && noLumaPlan.outputWidth == 1920 &&
                    noLumaPlan.outputHeight == 1080,
                "canonical 1080 no-luma plan");
        planSource.width = 3840;
        planSource.height = 2160;
        const auto chroma4KPlan = planner.PlanNativeVeryHigh(planSource, 3840, 2160);
        Require(chroma4KPlan.chromaOnly4K && chroma4KPlan.noLuma && chroma4KPlan.outputHook &&
                    chroma4KPlan.outputWidth == 3840 && chroma4KPlan.outputHeight == 2160,
                "canonical native 4K chroma-only plan");
        planSource.lumaBits = 10;
        planSource.chromaBits = 10;
        planSource.color = pl_color_space_hdr10;
        const auto hdr4KPlan = planner.PlanNativeVeryHigh(planSource, 3840, 2160);
        Require(hdr4KPlan.hdr && hdr4KPlan.chromaOnly4K && hdr4KPlan.noLuma &&
                    !hdr4KPlan.outputHook && hdr4KPlan.outputWidth == 3840 &&
                    hdr4KPlan.outputHeight == 2160,
                "canonical HDR 4K chroma-only plan");
        planSource.color = pl_color_space_srgb;
        planSource.lumaBits = 8;
        planSource.chromaBits = 8;
        planSource.width = 1280;
        planSource.height = 720;
        planSource.chromaLayout = NguChromaLayout::SemiPlanar422;
        const auto class3Plan = planner.PlanNativeVeryHigh(planSource, 1920, 1080);
        Require(class3Plan.outputHook, "canonical class-3 output hook");

        std::vector<uint8_t> rgb48(size_t(1280) * 720 * 6);
        for (size_t i = 0; i < size_t(1280) * 720; ++i)
            std::memcpy(rgb48.data() + i * 6, rgba16.data() + i * 8, 6);

        PlaceboBackend backend;
        RenderSettings settings;
        settings.ditherMethod = 3;
        settings.peakDetect = false;
        settings.colorAdjustment = false;
        settings.nativeScaler = NativeScalerKind::NguSharp;
        settings.upscaler = "ngu_sharp";
        settings.chromaUpscaler = "ngu_sharp";
        settings.downscaler = "mitchell";
        settings.scaling = ScalingSettings{};
        std::strcpy(settings.scaling->filters[0].filter, "ngu_sharp");
        std::strcpy(settings.scaling->filters[1].filter, "mitchell");
        std::strcpy(settings.scaling->filters[2].filter, "ngu_sharp");
        auto options = pl_options_alloc(nullptr);
        Require(options != nullptr, "NGU options");
        pl_options_reset(options, &pl_render_default_params);
        Require(ApplyScaling(options, *settings.scaling), "NGU scaling options");
        Require(SUCCEEDED(BuildRenderOptions(options, settings)), "NGU render options");
        auto fallbackParams = options->params;
        ApplyCommonRendererFallback(fallbackParams, settings, &options->dither_params);
        Require(fallbackParams.plane_upscaler == pl_render_default_params.plane_upscaler,
                "NGU fallback restores the actual plane scaler");
        pl_options_free(&options);

        RenderSettings ditherSettings;
        ditherSettings.ditherMethod = 0;
        ditherSettings.nativeScaler = NativeScalerKind::NguSharp;
        auto nativeDitherOptions = pl_options_alloc(nullptr);
        Require(nativeDitherOptions != nullptr, "native dither options");
        pl_options_reset(nativeDitherOptions, &pl_render_default_params);
        Require(SUCCEEDED(BuildRenderOptions(nativeDitherOptions, ditherSettings)), "native NGU dither options");
        Require(nativeDitherOptions->params.dither_params == nullptr, "native NGU must not add presentation dither");
        ditherSettings.nativeScaler = NativeScalerKind::None;
        Require(SUCCEEDED(BuildRenderOptions(nativeDitherOptions, ditherSettings)), "ordinary dither options");
        Require(nativeDitherOptions->params.dither_params != nullptr, "ordinary scaler retains presentation dither");
        pl_options_free(&nativeDitherOptions);

        auto native4kSettings = settings;
        native4kSettings.ditherMethod = 0;
        Require(SUCCEEDED(backend.SetSettings(native4kSettings)), "NGU native 4K chroma settings");

        VideoFrame native4k;
        native4k.width = 3840;
        native4k.height = 2160;
        native4k.stride = 3840;
        native4k.format = PixelFormat::NV12;
        native4k.matrix = PL_COLOR_SYSTEM_BT_709;
        native4k.levels = PL_COLOR_LEVELS_LIMITED;
        native4k.color = pl_color_space_srgb;
        std::vector<uint8_t> native4kBytes(size_t(3840) * 2160 + size_t(1920) * 1080 * 2);
        for (int y = 0; y < 2160; ++y)
            for (int x = 0; x < 3840; ++x)
                native4kBytes[size_t(y) * 3840 + x] = uint8_t(16 + (x * 219) / 3839);
        for (size_t i = size_t(3840) * 2160; i + 1 < native4kBytes.size(); i += 2)
        {
            native4kBytes[i] = 90;
            native4kBytes[i + 1] = 128;
        }
        Require(SUCCEEDED(backend.Upload(native4k, native4kBytes.data(), native4kBytes.size())),
                "NGU native 4K upload");
        bool native4kHook = false;
        pl_hook_stage native4kHookStage = static_cast<pl_hook_stage>(0);
        pl_color_repr native4kHookInputRepr = {};
        pl_color_repr native4kHookOutputRepr = {};
        int native4kHookInputComponents = 0;
        backend.SetNguHookProbeForTest(
            [&](const pl_hook_params& params, const pl_hook_res& result)
            {
                native4kHook = true;
                native4kHookStage = params.stage;
                native4kHookInputRepr = params.repr;
                native4kHookOutputRepr = result.repr;
                native4kHookInputComponents = params.components;
            });
        std::vector<uint8_t> native4kPixels;
        const HRESULT native4kRender = backend.RenderOffscreen(3840, 2160, native4kPixels);
        if (FAILED(native4kRender))
            throw std::runtime_error("NGU native 4K render: " + backend.LastError());
        Require(native4kPixels.size() == size_t(3840) * 2160 * 4, "NGU native 4K output size");
        Require(std::any_of(native4kPixels.begin(), native4kPixels.end(), [](uint8_t value) { return value != 0; }),
                "NGU native 4K chroma output is not black");
        Require(native4kHook && native4kHookStage == PL_HOOK_CHROMA_INPUT && native4kHookInputComponents == 2,
                "NGU native 4K hook metadata");
        std::printf("NGU native 4K hook input sys=%d levels=%d bits=%d/%d output sys=%d levels=%d bits=%d/%d\n",
                    int(native4kHookInputRepr.sys),
                    int(native4kHookInputRepr.levels),
                    native4kHookInputRepr.bits.sample_depth,
                    native4kHookInputRepr.bits.color_depth,
                    int(native4kHookOutputRepr.sys),
                    int(native4kHookOutputRepr.levels),
                    native4kHookOutputRepr.bits.sample_depth,
                    native4kHookOutputRepr.bits.color_depth);
        backend.SetNguHookProbeForTest({});

        VideoFrame native4kUnknown = native4k;
        native4kUnknown.matrix = PL_COLOR_SYSTEM_UNKNOWN;
        Require(SUCCEEDED(backend.Upload(native4kUnknown, native4kBytes.data(), native4kBytes.size())),
                "NGU native 4K unknown-matrix upload");
        std::vector<uint8_t> unknownMatrixPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, unknownMatrixPixels)),
                "NGU native 4K unknown-matrix fallback");
        uint64_t unknownRed = 0, unknownGreen = 0, unknownBlue = 0;
        for (size_t i = 0; i + 3 < unknownMatrixPixels.size(); i += 4)
        {
            unknownBlue += unknownMatrixPixels[i];
            unknownGreen += unknownMatrixPixels[i + 1];
            unknownRed += unknownMatrixPixels[i + 2];
        }
        Require(unknownRed > unknownBlue && unknownGreen > 0,
                "NGU native 4K unknown matrix preserves the source color orientation");
        std::puts("NGU native 4K unknown matrix remains visible");

        D3D11_TEXTURE2D_DESC native4kDecoderDesc = {};
        native4kDecoderDesc.Width = 3840;
        native4kDecoderDesc.Height = 2160;
        native4kDecoderDesc.MipLevels = 1;
        native4kDecoderDesc.ArraySize = 1;
        native4kDecoderDesc.Format = DXGI_FORMAT_NV12;
        native4kDecoderDesc.SampleDesc.Count = 1;
        native4kDecoderDesc.Usage = D3D11_USAGE_DEFAULT;
        native4kDecoderDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA native4kDecoderData = {};
        native4kDecoderData.pSysMem = native4kBytes.data();
        native4kDecoderData.SysMemPitch = 3840;
        ComPtr<ID3D11Texture2D> native4kDecoder;
        Require(SUCCEEDED(backend.Device()->CreateTexture2D(
                    &native4kDecoderDesc, &native4kDecoderData, &native4kDecoder)),
                "NGU native 4K decoder texture");
        Require(SUCCEEDED(backend.Import(native4k, native4kDecoder.Get(), 0)),
                "NGU native 4K decoder import");
        std::vector<uint8_t> native4kImportedPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, native4kImportedPixels)),
                "NGU native 4K imported render");
        int native4kImportMaximum = 0, native4kImportOverOne = 0;
        const int native4kImportDifferent =
            CompareRgb(native4kPixels, native4kImportedPixels, native4kImportMaximum, native4kImportOverOne);
        std::printf("NGU native 4K Upload/Import different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    native4kImportDifferent,
                    native4kImportMaximum,
                    native4kImportOverOne);
        Require(native4kImportMaximum <= 1 && native4kImportOverOne == 0,
                "NGU native 4K decoder import matches CPU upload");

        backend.ShowStatistics(true);
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, native4kPixels)),
                "NGU native 4K chroma statistics render");
        const auto native4kStatistics = backend.StatisticsSnapshot();
        Require(native4kStatistics.find(L"NGU Sharp VeryHigh (native hook)") != std::wstring::npos,
                "NGU native 4K statistics identify active chroma upscaling");
        Require(native4kStatistics.ends_with(std::wstring(L"Version\t") + FRANKY_VERSION_WSTRING),
                "NGU statistics end with the FrankyRenderer version");
        bool native4kWindowHook = false;
        pl_hook_stage native4kWindowHookStage = static_cast<pl_hook_stage>(0);
        pl_rect2df native4kWindowHookRect = {};
        backend.SetNguHookProbeForTest(
            [&](const pl_hook_params& params, const pl_hook_res& result)
            {
                native4kWindowHook = true;
                native4kWindowHookStage = params.stage;
                native4kWindowHookRect = result.rect;
            });
        std::vector<uint8_t> native4kWindowPixels;
        const HRESULT native4kWindowRender = backend.RenderOffscreen(1920, 1080, native4kWindowPixels);
        if (FAILED(native4kWindowRender))
            throw std::runtime_error("NGU native 4K window render: " + backend.LastError());
        Require(native4kWindowPixels.size() == size_t(1920) * 1080 * 4,
                "NGU native 4K window output size");
        Require(std::any_of(native4kWindowPixels.begin(), native4kWindowPixels.end(),
                            [](uint8_t value) { return value != 0; }),
                "NGU native 4K window output is not black");
        Require(native4kWindowHook && native4kWindowHookStage == PL_HOOK_CHROMA_INPUT,
                "NGU native 4K window executes the chroma hook");
        Require(native4kWindowHookRect.x1 == 3840.0f && native4kWindowHookRect.y1 == 2160.0f,
                "NGU native 4K window hook returns full-chroma geometry");
        backend.SetNguHookProbeForTest({});
        Sleep(300);
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, native4kWindowPixels)),
                "NGU native 4K window statistics render");
        const auto native4kWindowStatistics = backend.StatisticsSnapshot();
        Require(native4kWindowStatistics.find(L"NGU Sharp VeryHigh (native hook)") != std::wstring::npos,
                "NGU native 4K window statistics identify active chroma upscaling");
        Require(native4kWindowStatistics.find(L"nearest") == std::wstring::npos,
                "NGU native 4K window does not report its inactive placeholder");
        std::puts("NGU native 4K chroma pre-scales then common renderer reduces the window");
        backend.ShowStatistics(false);

        VideoFrame native4kHdr = native4k;
        native4kHdr.format = PixelFormat::P010;
        native4kHdr.stride = 3840 * 2;
        native4kHdr.color = pl_color_space_hdr10;
        native4kHdr.color.hdr.min_luma = .005f;
        native4kHdr.color.hdr.max_luma = 4000;
        native4kHdr.color.hdr.max_cll = 1000;
        native4kHdr.color.hdr.max_fall = 200;
        native4kHdr.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
        native4kHdr.levels = PL_COLOR_LEVELS_LIMITED;
        const size_t native4kHdrBytes = size_t(3840) * 2160 * 2 + size_t(1920) * 1080 * 4;
        std::vector<uint8_t> native4kHdrData(native4kHdrBytes);
        auto native4kHdrY = reinterpret_cast<uint16_t*>(native4kHdrData.data());
        for (int y = 0; y < 2160; ++y)
            for (int x = 0; x < 3840; ++x)
                native4kHdrY[size_t(y) * 3840 + x] =
                    uint16_t((16 + (x * 219) / 3839) << 6);
        auto native4kHdrUv = reinterpret_cast<uint16_t*>(
            native4kHdrData.data() + size_t(3840) * 2160 * 2);
        for (size_t i = 0; i < size_t(1920) * 1080; ++i)
        {
            native4kHdrUv[i * 2] = uint16_t(512 << 6);
            native4kHdrUv[i * 2 + 1] = uint16_t(512 << 6);
        }
        Require(SUCCEEDED(backend.Upload(native4kHdr, native4kHdrData.data(), native4kHdrData.size())),
                "NGU native 4K HDR upload");
        bool native4kHdrHook = false;
        pl_hook_stage native4kHdrHookStage = static_cast<pl_hook_stage>(0);
        pl_rect2df native4kHdrHookRect = {};
        pl_color_repr native4kHdrHookRepr = {};
        pl_color_space native4kHdrHookColor = {};
        pl_color_space native4kHdrHookInputColor = {};
        int native4kHdrHookComponents = 0;
        backend.SetNguHookProbeForTest(
            [&](const pl_hook_params& params, const pl_hook_res& result)
            {
                native4kHdrHook = true;
                native4kHdrHookStage = params.stage;
                native4kHdrHookRect = result.rect;
                native4kHdrHookRepr = result.repr;
                native4kHdrHookColor = result.color;
                native4kHdrHookInputColor = params.color;
                native4kHdrHookComponents = result.components;
            });
        std::vector<uint8_t> native4kHdrPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, native4kHdrPixels)),
                "NGU native 4K HDR render");
        Require(native4kHdrHook && native4kHdrHookStage == PL_HOOK_CHROMA_INPUT &&
                    native4kHdrHookRect.x1 == 3840.0f && native4kHdrHookRect.y1 == 2160.0f,
                "NGU native 4K HDR executes the shared chroma hook");
        Require(native4kHdrHookComponents == 2 && native4kHdrHookRepr.bits.sample_depth == 16 &&
                    native4kHdrHookRepr.bits.color_depth == 16,
                "NGU native 4K HDR chroma hook returns two 16-bit components");
        Require(native4kHdrHookInputColor.transfer == native4kHdr.color.transfer &&
                    native4kHdrHookInputColor.primaries == native4kHdr.color.primaries &&
                    native4kHdrHookColor.transfer == native4kHdr.color.transfer &&
                    native4kHdrHookColor.primaries == native4kHdr.color.primaries &&
                    native4kHdrHookColor.hdr.max_luma == native4kHdr.color.hdr.max_luma &&
                    native4kHdrHookColor.hdr.max_cll == native4kHdr.color.hdr.max_cll &&
                    native4kHdrHookColor.hdr.max_fall == native4kHdr.color.hdr.max_fall,
                "NGU native 4K HDR hook preserves source color and peak metadata");
        Require(std::any_of(native4kHdrPixels.begin(), native4kHdrPixels.end(),
                            [](uint8_t value) { return value != 0; }),
                "NGU native 4K HDR output is not blue/black");
        uint64_t hdrRed = 0, hdrGreen = 0, hdrBlue = 0;
        for (size_t i = 0; i + 3 < native4kHdrPixels.size(); i += 4)
        {
            hdrBlue += native4kHdrPixels[i];
            hdrGreen += native4kHdrPixels[i + 1];
            hdrRed += native4kHdrPixels[i + 2];
        }
        Require(hdrRed > 0 && hdrGreen > 0, "NGU native 4K HDR output is not a blue-only frame");
        std::vector<uint8_t> native4kHdrWindowPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, native4kHdrWindowPixels)),
                "NGU native 4K HDR window render");
        Require(native4kHdrWindowPixels.size() == size_t(1920) * 1080 * 4,
                "NGU native 4K HDR window output size");
        Require(native4kHdrHook && native4kHdrHookStage == PL_HOOK_CHROMA_INPUT &&
                    native4kHdrHookRect.x1 == 3840.0f && native4kHdrHookRect.y1 == 2160.0f,
                "NGU native 4K HDR window uses the source-resolution chroma hook");
        Require(std::any_of(native4kHdrWindowPixels.begin(), native4kHdrWindowPixels.end(),
                            [](uint8_t value) { return value != 0; }),
                "NGU native 4K HDR window output is not black");
        uint64_t windowHdrRed = 0, windowHdrGreen = 0, windowHdrBlue = 0;
        for (size_t i = 0; i + 3 < native4kHdrWindowPixels.size(); i += 4)
        {
            windowHdrBlue += native4kHdrWindowPixels[i];
            windowHdrGreen += native4kHdrWindowPixels[i + 1];
            windowHdrRed += native4kHdrWindowPixels[i + 2];
        }
        Require(windowHdrRed > 0 && windowHdrGreen > 0,
                "NGU native 4K HDR window output is not a blue-only frame");

        VideoFrame native4kP016Hdr = native4kHdr;
        native4kP016Hdr.format = PixelFormat::P016;
        std::vector<uint8_t> native4kP016HdrData(native4kHdrBytes);
        auto native4kP016HdrY = reinterpret_cast<uint16_t*>(native4kP016HdrData.data());
        for (int y = 0; y < 2160; ++y)
            for (int x = 0; x < 3840; ++x)
                native4kP016HdrY[size_t(y) * 3840 + x] =
                    uint16_t((16 + (x * 219) / 3839) << 8);
        auto native4kP016HdrUv = reinterpret_cast<uint16_t*>(
            native4kP016HdrData.data() + size_t(3840) * 2160 * 2);
        for (size_t i = 0; i < size_t(1920) * 1080; ++i)
        {
            native4kP016HdrUv[i * 2] = uint16_t(512 << 6);
            native4kP016HdrUv[i * 2 + 1] = uint16_t(512 << 6);
        }
        bool native4kP016HdrHook = false;
        pl_hook_stage native4kP016HdrHookStage = static_cast<pl_hook_stage>(0);
        pl_color_space native4kP016HdrHookColor = {};
        backend.SetNguHookProbeForTest(
            [&](const pl_hook_params& params, const pl_hook_res& result)
            {
                native4kP016HdrHook = true;
                native4kP016HdrHookStage = params.stage;
                native4kP016HdrHookColor = result.color;
            });
        Require(SUCCEEDED(backend.Upload(
                    native4kP016Hdr, native4kP016HdrData.data(), native4kP016HdrData.size())),
                "NGU native 4K P016 HDR upload");
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, native4kHdrWindowPixels)),
                "NGU native 4K P016 HDR window render");
        Require(native4kP016HdrHook && native4kP016HdrHookStage == PL_HOOK_CHROMA_INPUT &&
                    native4kP016HdrHookColor.transfer == native4kP016Hdr.color.transfer &&
                    native4kP016HdrHookColor.primaries == native4kP016Hdr.color.primaries,
                "NGU native 4K P016 HDR uses the shared chroma hook with metadata");
        auto render4KHdrWindowProfile = [&](const VideoFrame& frame, const char* label)
        {
            bool sawHook = false;
            pl_hook_stage hookStage = static_cast<pl_hook_stage>(0);
            pl_color_space hookColor = {};
            backend.SetNguHookProbeForTest(
                [&](const pl_hook_params& params, const pl_hook_res& result)
                {
                    sawHook = true;
                    hookStage = params.stage;
                    hookColor = result.color;
                });
            Require(SUCCEEDED(backend.Upload(frame, native4kHdrData.data(), native4kHdrData.size())),
                    label);
            std::vector<uint8_t> profilePixels;
            Require(SUCCEEDED(backend.RenderOffscreen(1600, 900, profilePixels)), label);
            Require(sawHook && hookStage == PL_HOOK_CHROMA_INPUT, label);
            Require(hookColor.transfer == frame.color.transfer && hookColor.primaries == frame.color.primaries,
                    label);
            Require(std::any_of(profilePixels.begin(), profilePixels.end(), [](uint8_t value) { return value != 0; }),
                    label);
        };
        VideoFrame native4kHlg = native4kHdr;
        native4kHlg.color.transfer = PL_COLOR_TRC_HLG;
        render4KHdrWindowProfile(native4kHlg, "NGU native 4K HLG window");
        VideoFrame native4kBt709Pq = native4kHdr;
        native4kBt709Pq.color.primaries = PL_COLOR_PRIM_BT_709;
        native4kBt709Pq.matrix = PL_COLOR_SYSTEM_BT_709;
        render4KHdrWindowProfile(native4kBt709Pq, "NGU native 4K BT.709/PQ window");
        backend.SetNguHookProbeForTest({});
        std::puts("NGU native 4K HDR PQ/HLG/BT.709-PQ and P016 use the shared chroma hook under Franky HDR");

        std::puts("NGU native 4K executes the all-quality chroma hook without a black frame");
        Require(SUCCEEDED(backend.SetSettings(settings)), "NGU backend settings");

        VideoFrame frame;
        frame.width = 1280;
        frame.height = 720;
        frame.stride = 1280 * 6;
        frame.format = PixelFormat::RGB48;
        frame.matrix = PL_COLOR_SYSTEM_RGB;
        frame.levels = PL_COLOR_LEVELS_FULL;
        frame.color = pl_color_space_srgb;
        Require(SUCCEEDED(backend.Upload(frame, rgb48.data(), rgb48.size())), "NGU RGB48 upload");

        std::vector<uint8_t> pixels;
        const auto firstRenderStart = std::chrono::steady_clock::now();
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, pixels)), "NGU backend render");
        const double firstRenderMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - firstRenderStart).count();
        std::printf("NGU backend first-frame setup+render %.3f ms\n", firstRenderMs);
        Require(pixels.size() == size_t(3840) * 2160 * 4, "NGU output size");
        const auto expected =
            BgraToRgba(ReadBytes(root / "draw_00266_PS_1352_6992f6f0_rt0_3840x2160_f21.bin", size_t(3840) * 2160 * 4));
        int maximum = 0, overOne = 0;
        const int different = CompareRgb(pixels, expected, maximum, overOne);
        std::printf("NGU backend RGB different=%d maximum=%d diff_gt_1_pixels=%d\n", different, maximum, overOne);
        if (maximum > 1)
        {
            std::printf("  actual=%u,%u,%u expected=%u,%u,%u\n",
                        unsigned(pixels[0]),
                        unsigned(pixels[1]),
                        unsigned(pixels[2]),
                        unsigned(expected[0]),
                        unsigned(expected[1]),
                        unsigned(expected[2]));
        }
        Require(maximum <= 1 && overOne == 0, "NGU backend RGB acceptance");

        uint64_t capturedId = 0;
        const HRESULT captured = backend.CaptureUpload(frame, rgb48.data(), rgb48.size(), capturedId);
        if (FAILED(captured))
            throw std::runtime_error("NGU stored-frame capture: " + backend.LastError());
        std::vector<uint8_t> storedPixels;
        const HRESULT storedRender = backend.RenderOffscreen(3840, 2160, storedPixels);
        if (FAILED(storedRender))
            throw std::runtime_error("NGU stored-frame render: " + backend.LastError());
        int storedMaximum = 0, storedOverOne = 0;
        const int storedDifferent = CompareRgb(storedPixels, expected, storedMaximum, storedOverOne);
        std::printf("NGU backend stored RGB different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    storedDifferent,
                    storedMaximum,
                    storedOverOne);
        Require(storedMaximum <= 1 && storedOverOne == 0, "NGU stored-frame RGB acceptance");

        std::vector<uint8_t> pattern(rgb48.size());
        for (size_t i = 0; i < size_t(1280) * 720; ++i)
        {
            const uint16_t red = uint16_t(30000 + (i % 1280) * 20);
            const uint16_t green = uint16_t(40000 + ((i / 1280) % 720) * 20);
            std::memcpy(pattern.data() + i * 6, &red, sizeof(red));
            std::memcpy(pattern.data() + i * 6 + 2, &green, sizeof(green));
        }
        Require(SUCCEEDED(backend.Upload(frame, pattern.data(), pattern.size())), "NGU pattern upload");
        std::vector<uint8_t> patternPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, patternPixels)), "NGU pattern render");
        Require(patternPixels[0] || patternPixels[1] || patternPixels[2], "NGU pattern output is not black");

        const auto capture1080 = root.parent_path() / "luma_1080_full_v17";
        const auto rgba1080 = ReadBytes(capture1080 / "input_31703_PS_460_bfe50921_s0.bin", size_t(1920) * 1080 * 8);
        std::vector<uint8_t> rgb48_1080(size_t(1920) * 1080 * 6);
        for (size_t i = 0; i < size_t(1920) * 1080; ++i)
            std::memcpy(rgb48_1080.data() + i * 6, rgba1080.data() + i * 8, 6);
        VideoFrame frame1080 = frame;
        frame1080.width = 1920;
        frame1080.height = 1080;
        frame1080.stride = 1920 * 6;
        Require(SUCCEEDED(backend.Upload(frame1080, rgb48_1080.data(), rgb48_1080.size())), "NGU 1080 RGB48 upload");
        std::vector<uint8_t> pixels1080;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, pixels1080)), "NGU 1080 render");
        const auto expected1080 = BgraToRgba(
            ReadBytes(capture1080 / "draw_31727_PS_1352_6992f6f0_rt0_3840x2160_f21.bin", size_t(3840) * 2160 * 4));
        int maximum1080 = 0, overOne1080 = 0;
        const int different1080 = CompareRgb(pixels1080, expected1080, maximum1080, overOne1080);
        std::printf("NGU backend 1080 RGB different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    different1080,
                    maximum1080,
                    overOne1080);
        Require(maximum1080 <= 1 && overOne1080 == 0, "NGU 1080 RGB acceptance");

        std::vector<uint8_t> nv12Bytes(size_t(1920) * 1080 + size_t(960) * 540 * 2);
        for (int y = 0; y < 1080; ++y)
            for (int x = 0; x < 1920; ++x)
                nv12Bytes[size_t(y) * 1920 + x] = 128;
        for (size_t i = size_t(1920) * 1080; i < nv12Bytes.size(); i += 2)
        {
            nv12Bytes[i] = 90;
            nv12Bytes[i + 1] = 180;
        }
        VideoFrame nv12 = frame1080;
        nv12.format = PixelFormat::NV12;
        nv12.stride = 1920;
        Require(SUCCEEDED(backend.Upload(nv12, nv12Bytes.data(), nv12Bytes.size())), "NGU NV12 1080 upload");
        std::vector<uint8_t> nv12Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, nv12Pixels)), "NGU NV12 1080 render");
        Require(nv12Pixels.size() == size_t(3840) * 2160 * 4, "NGU NV12 1080 output size");
        const size_t nv12Probe = (size_t(1080 / 2) * 1920 + 960) * 4;
        Require(nv12Pixels[nv12Probe] || nv12Pixels[nv12Probe + 1] || nv12Pixels[nv12Probe + 2],
                "NGU NV12 1080 output is not black");
        std::printf("NGU backend NV12 1080 nonblack\n");
        std::vector<uint8_t> nv12WindowPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, nv12WindowPixels)), "NGU NV12 1080 window render");
        Require(nv12WindowPixels.size() == size_t(1920) * 1080 * 4, "NGU NV12 1080 window output size");
        const size_t nv12WindowProbe = (size_t(1080 / 2) * 1920 + 960) * 4;
        Require(nv12WindowPixels[nv12WindowProbe] || nv12WindowPixels[nv12WindowProbe + 1] ||
                    nv12WindowPixels[nv12WindowProbe + 2],
                "NGU NV12 1080 window output is not black");
        std::printf("NGU backend NV12 1080 window nonblack\n");

        std::vector<uint8_t> i420Bytes(nv12Bytes.size());
        std::memcpy(i420Bytes.data(), nv12Bytes.data(), size_t(1920) * 1080);
        uint8_t* i420U = i420Bytes.data() + size_t(1920) * 1080;
        uint8_t* i420V = i420U + size_t(960) * 540;
        const uint8_t* nv12Uv = nv12Bytes.data() + size_t(1920) * 1080;
        for (size_t i = 0; i < size_t(960) * 540; ++i)
        {
            i420U[i] = nv12Uv[i * 2];
            i420V[i] = nv12Uv[i * 2 + 1];
        }
        VideoFrame i420 = nv12;
        i420.format = PixelFormat::I420;
        Require(SUCCEEDED(backend.Upload(i420, i420Bytes.data(), i420Bytes.size())), "NGU I420 1080 upload");
        std::vector<uint8_t> i420Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, i420Pixels)), "NGU I420 1080 window render");
        Require(i420Pixels.size() == size_t(1920) * 1080 * 4, "NGU I420 1080 output size");
        Require(i420Pixels[nv12WindowProbe] || i420Pixels[nv12WindowProbe + 1] || i420Pixels[nv12WindowProbe + 2],
                "NGU I420 1080 output is not black");
        std::printf("NGU backend canonical I420 1080 window nonblack\n");

        std::vector<uint8_t> nv12_720(size_t(1280) * 720 + size_t(640) * 360 * 2);
        for (int y = 0; y < 720; ++y)
            for (int x = 0; x < 1280; ++x)
                nv12_720[size_t(y) * 1280 + x] = uint8_t(16 + (x * 219) / 1279);
        for (size_t i = size_t(1280) * 720; i < nv12_720.size(); i += 2)
        {
            nv12_720[i] = 90;
            nv12_720[i + 1] = 180;
        }
        VideoFrame nv12Frame720;
        nv12Frame720.width = 1280;
        nv12Frame720.height = 720;
        nv12Frame720.stride = 1280;
        nv12Frame720.format = PixelFormat::NV12;
        nv12Frame720.matrix = PL_COLOR_SYSTEM_BT_709;
        nv12Frame720.levels = PL_COLOR_LEVELS_LIMITED;
        nv12Frame720.color = pl_color_space_bt709;
        Require(SUCCEEDED(backend.Upload(nv12Frame720, nv12_720.data(), nv12_720.size())),
                "NGU NV12 720 Double upload");
        std::vector<uint8_t> doublePixels;
        Require(SUCCEEDED(backend.RenderOffscreen(2560, 1440, doublePixels)), "NGU NV12 720 Double render");
        Require(doublePixels.size() == size_t(2560) * 1440 * 4, "NGU NV12 720 Double output size");
        const size_t probe720 = (size_t(720) * 2560 + 1280) * 4;
        Require(doublePixels[probe720] || doublePixels[probe720 + 1] || doublePixels[probe720 + 2],
                "NGU NV12 720 Double output is not black");
        std::printf("NGU backend canonical NV12 720 Double nonblack\n");

        std::vector<uint8_t> i420_720(nv12_720.size());
        std::memcpy(i420_720.data(), nv12_720.data(), size_t(1280) * 720);
        uint8_t* i420_720_u = i420_720.data() + size_t(1280) * 720;
        uint8_t* i420_720_v = i420_720_u + size_t(640) * 360;
        const uint8_t* nv12_720_uv = nv12_720.data() + size_t(1280) * 720;
        for (size_t i = 0; i < size_t(640) * 360; ++i)
        {
            i420_720_u[i] = nv12_720_uv[i * 2];
            i420_720_v[i] = nv12_720_uv[i * 2 + 1];
        }
        VideoFrame i420Frame720 = nv12Frame720;
        i420Frame720.format = PixelFormat::I420;
        Require(SUCCEEDED(backend.Upload(i420Frame720, i420_720.data(), i420_720.size())),
                "NGU I420 720 Double upload");
        std::vector<uint8_t> i420DoublePixels;
        Require(SUCCEEDED(backend.RenderOffscreen(2560, 1440, i420DoublePixels)), "NGU I420 720 Double render");
        int i420DoubleMaximum = 0;
        int i420DoubleOverOne = 0;
        const int i420DoubleDifferent =
            CompareRgb(i420DoublePixels, doublePixels, i420DoubleMaximum, i420DoubleOverOne);
        std::printf("NGU backend I420/NV12 720 Double different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    i420DoubleDifferent,
                    i420DoubleMaximum,
                    i420DoubleOverOne);
        Require(i420DoubleMaximum <= 1 && i420DoubleOverOne == 0, "NGU I420 720 Double canonical parity");

        std::vector<uint8_t> window720Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1600, 900, window720Pixels)), "NGU NV12 720 window render");
        Require(window720Pixels.size() == size_t(1600) * 900 * 4, "NGU NV12 720 window output size");
        const size_t window720Probe = (size_t(450) * 1600 + 800) * 4;
        Require(window720Pixels[window720Probe] || window720Pixels[window720Probe + 1] ||
                    window720Pixels[window720Probe + 2],
                "NGU NV12 720 window output is not black");
        std::printf("NGU backend canonical NV12 720 window 1600x900 nonblack\n");

        std::vector<uint8_t> reduce720Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, reduce720Pixels)),
                "NGU NV12 720 1.5x common-reduction render");
        Require(reduce720Pixels.size() == size_t(1920) * 1080 * 4, "NGU NV12 720 1.5x common-reduction output size");
        const size_t reduce720Probe = (size_t(540) * 1920 + 960) * 4;
        Require(reduce720Pixels[reduce720Probe] || reduce720Pixels[reduce720Probe + 1] ||
                    reduce720Pixels[reduce720Probe + 2],
                "NGU NV12 720 1.5x common-reduction output is not black");
        std::printf("NGU backend canonical NV12 720->1080 Franky reduction nonblack\n");

        VideoFrame fullRange720 = nv12Frame720;
        fullRange720.levels = PL_COLOR_LEVELS_FULL;
        Require(SUCCEEDED(backend.Upload(fullRange720, nv12_720.data(), nv12_720.size())),
                "NGU NV12 720 full-range upload");
        std::vector<uint8_t> fullRange720Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, fullRange720Pixels)), "NGU NV12 720 full-range render");
        Require(fullRange720Pixels[reduce720Probe] || fullRange720Pixels[reduce720Probe + 1] ||
                    fullRange720Pixels[reduce720Probe + 2],
                "NGU NV12 720 full-range output is not black");
        std::printf("NGU backend canonical NV12 720 full-range nonblack\n");

        backend.ShowStatistics(true);
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, nv12WindowPixels)), "NGU NV12 statistics render");
        Require(backend.StatisticsSnapshot().find(L"NGU Sharp VeryHigh (native hook)") != std::wstring::npos,
                "NGU statistics name");
        backend.ShowStatistics(false);
        std::printf("NGU backend statistics native-hook name\n");

        const size_t coloredP010Bytes = size_t(1920) * 1080 * 2 + size_t(960) * 540 * 4;
        std::vector<uint8_t> coloredP010(coloredP010Bytes);
        auto writeP010 = [&](size_t offset, uint8_t value)
        {
            const uint16_t sample = uint16_t(value) << 8;
            std::memcpy(coloredP010.data() + offset, &sample, sizeof(sample));
        };
        for (int y = 0; y < 1080; ++y)
            for (int x = 0; x < 1920; ++x)
                writeP010((size_t(y) * 1920 + x) * 2, 128);
        for (size_t i = size_t(1920) * 1080; i < coloredP010Bytes; i += 4)
        {
            writeP010(i, 90);
            writeP010(i + 2, 180);
        }
        VideoFrame coloredP010Frame = nv12;
        coloredP010Frame.format = PixelFormat::P010;
        coloredP010Frame.stride = 1920 * 2;
        Require(SUCCEEDED(backend.Upload(coloredP010Frame, coloredP010.data(), coloredP010.size())),
                "NGU colored P010 upload");
        std::vector<uint8_t> coloredP010Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, coloredP010Pixels)), "NGU colored P010 render");
        auto meanRgb = [](const std::vector<uint8_t>& pixels)
        {
            std::array<uint64_t, 3> sum = {};
            for (size_t i = 0; i < pixels.size(); i += 4)
                for (int c = 0; c < 3; ++c)
                    sum[c] += pixels[i + c];
            const uint64_t count = pixels.size() / 4;
            return std::array<uint32_t, 3>{
                uint32_t(sum[0] / count), uint32_t(sum[1] / count), uint32_t(sum[2] / count)};
        };
        const auto nv12Mean = meanRgb(nv12WindowPixels);
        const auto p010Mean = meanRgb(coloredP010Pixels);
        std::printf("NGU backend colored NV12 avg=%u,%u,%u P010 avg=%u,%u,%u\n",
                    nv12Mean[0],
                    nv12Mean[1],
                    nv12Mean[2],
                    p010Mean[0],
                    p010Mean[1],
                    p010Mean[2]);
        Require((nv12Mean[0] > nv12Mean[2]) == (p010Mean[0] > p010Mean[2]), "NGU NV12/P010 chroma orientation");

        std::vector<uint8_t> coloredP016(coloredP010.size());
        for (size_t i = 0; i + 1 < coloredP016.size(); i += 2)
        {
            const uint16_t p010Sample = uint16_t(coloredP010[i]) | (uint16_t(coloredP010[i + 1]) << 8);
            const uint16_t p016Sample = uint16_t((uint32_t(p010Sample) * 65535u + 32768u) >> 16);
            std::memcpy(coloredP016.data() + i, &p016Sample, sizeof(p016Sample));
        }
        VideoFrame coloredP016Frame = coloredP010Frame;
        coloredP016Frame.format = PixelFormat::P016;
        Require(SUCCEEDED(backend.Upload(coloredP016Frame, coloredP016.data(), coloredP016.size())),
                "NGU colored P016 upload");
        std::vector<uint8_t> coloredP016Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, coloredP016Pixels)), "NGU colored P016 render");
        const auto p016Mean = meanRgb(coloredP016Pixels);
        std::printf("NGU backend canonical P016 avg=%u,%u,%u\n", p016Mean[0], p016Mean[1], p016Mean[2]);
        Require((nv12Mean[0] > nv12Mean[2]) == (p016Mean[0] > p016Mean[2]), "NGU P016 chroma orientation");

        const size_t p016720Bytes = size_t(1280) * 720 * 2 + size_t(640) * 360 * 4;
        std::vector<uint8_t> coloredP016720(p016720Bytes);
        auto writeP016720 = [&](size_t offset, uint8_t value)
        {
            const uint16_t sample = uint16_t(value) * 257;
            std::memcpy(coloredP016720.data() + offset, &sample, sizeof(sample));
        };
        for (size_t i = 0; i < size_t(1280) * 720; ++i)
            writeP016720(i * 2, nv12_720[i]);
        const size_t p016720Uv = size_t(1280) * 720 * 2;
        const uint8_t* nv12Uv720 = nv12_720.data() + size_t(1280) * 720;
        for (size_t i = 0; i < size_t(640) * 360; ++i)
        {
            writeP016720(p016720Uv + i * 4, nv12Uv720[i * 2]);
            writeP016720(p016720Uv + i * 4 + 2, nv12Uv720[i * 2 + 1]);
        }
        VideoFrame coloredP016720Frame = nv12Frame720;
        coloredP016720Frame.format = PixelFormat::P016;
        coloredP016720Frame.stride = 1280 * 2;
        bool sawP016DoubleHook = false;
        pl_hook_stage p016DoubleHookStage = static_cast<pl_hook_stage>(0);
        pl_color_repr p016DoubleHookRepr = {};
        pl_rect2df p016DoubleHookRect = {};
        backend.SetNguHookProbeForTest(
            [&](const pl_hook_params& params, const pl_hook_res& result)
            {
                sawP016DoubleHook = true;
                p016DoubleHookStage = params.stage;
                p016DoubleHookRepr = result.repr;
                p016DoubleHookRect = result.rect;
            });
        Require(SUCCEEDED(backend.Upload(coloredP016720Frame, coloredP016720.data(), coloredP016720.size())),
                "NGU P016 720 Double upload");
        std::vector<uint8_t> p016DoublePixels;
        Require(SUCCEEDED(backend.RenderOffscreen(2560, 1440, p016DoublePixels)), "NGU P016 720 Double render");
        Require(std::any_of(p016DoublePixels.begin(), p016DoublePixels.end(), [](uint8_t value) { return value != 0; }),
                "NGU P016 720 Double output is not black");
        Require(sawP016DoubleHook && p016DoubleHookStage == PL_HOOK_RGB, "NGU P016 720 Double RGB hook");
        Require(p016DoubleHookRepr.bits.sample_depth == 16 && p016DoubleHookRepr.bits.color_depth == 16,
                "NGU P016 720 Double RGBA16 hook");
        Require(p016DoubleHookRect.x1 == 2560.0f && p016DoubleHookRect.y1 == 1440.0f,
                "NGU P016 720 Double hook geometry");
        std::printf("NGU backend P016 720 Double pre-final RGBA16 hook nonblack\n");

        VideoFrame hdrP010Frame = coloredP010Frame;
        hdrP010Frame.color = pl_color_space_hdr10;
        hdrP010Frame.color.hdr.min_luma = .005f;
        hdrP010Frame.color.hdr.max_luma = 4000;
        hdrP010Frame.color.hdr.max_cll = 1000;
        hdrP010Frame.color.hdr.max_fall = 200;
        hdrP010Frame.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
        hdrP010Frame.levels = PL_COLOR_LEVELS_LIMITED;
        bool sawHdrNguHook = false;
        pl_hook_stage hdrNguHookStage = static_cast<pl_hook_stage>(0);
        pl_color_repr hdrNguHookRepr = {};
        pl_color_space hdrNguHookColor = {};
        pl_color_space hdrNguHookInputColor = {};
        pl_rect2df hdrNguHookRect = {};
        int hdrNguHookComponents = 0;
        backend.SetNguHookProbeForTest(
            [&](const pl_hook_params& params, const pl_hook_res& result)
            {
                sawHdrNguHook = true;
                hdrNguHookStage = params.stage;
                hdrNguHookRepr = result.repr;
                hdrNguHookColor = result.color;
                hdrNguHookInputColor = params.color;
                hdrNguHookRect = result.rect;
                hdrNguHookComponents = result.components;
            });
        Require(SUCCEEDED(backend.Upload(hdrP010Frame, coloredP010.data(), coloredP010.size())), "NGU HDR P010 upload");
        std::vector<uint8_t> hdrP010Pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, hdrP010Pixels)), "NGU HDR P010 render");
        Require(hdrP010Pixels.size() == size_t(3840) * 2160 * 4, "NGU HDR P010 output size");
        const bool hdrNonBlack =
            std::any_of(hdrP010Pixels.begin(), hdrP010Pixels.end(), [](uint8_t value) { return value != 0; });
        Require(hdrNonBlack, "NGU HDR P010 output is not black");
        Require(sawHdrNguHook, "NGU HDR hook executed");
        Require(hdrNguHookStage == PL_HOOK_RGB, "NGU HDR hook is before Franky color management");
        Require(hdrNguHookComponents == 3, "NGU HDR hook RGB components");
        pl_color_repr hdrExpectedRepr = pl_color_repr_rgb;
        hdrExpectedRepr.bits = {16, 16, 0};
        hdrExpectedRepr.alpha = PL_ALPHA_NONE;
        Require(pl_color_repr_equal(&hdrNguHookRepr, &hdrExpectedRepr), "NGU HDR hook RGBA16 representation");
        std::printf("NGU HDR hook input color transfer=%d primaries=%d min=%.6f max=%.1f cll=%.1f fall=%.1f\n",
                    int(hdrNguHookInputColor.transfer),
                    int(hdrNguHookInputColor.primaries),
                    hdrNguHookInputColor.hdr.min_luma,
                    hdrNguHookInputColor.hdr.max_luma,
                    hdrNguHookInputColor.hdr.max_cll,
                    hdrNguHookInputColor.hdr.max_fall);
        std::printf("NGU HDR hook output color transfer=%d primaries=%d min=%.6f max=%.1f cll=%.1f fall=%.1f\n",
                    int(hdrNguHookColor.transfer),
                    int(hdrNguHookColor.primaries),
                    hdrNguHookColor.hdr.min_luma,
                    hdrNguHookColor.hdr.max_luma,
                    hdrNguHookColor.hdr.max_cll,
                    hdrNguHookColor.hdr.max_fall);
        auto sameHdrBoundary = [](const pl_color_space& actual, const pl_color_space& expected)
        {
            // Dynamic scene values use NaN/zero sentinels; compare the static
            // HDR10 boundary that must survive the NGU handoff.
            return actual.transfer == expected.transfer && actual.primaries == expected.primaries &&
                   actual.hdr.min_luma == expected.hdr.min_luma && actual.hdr.max_luma == expected.hdr.max_luma &&
                   actual.hdr.max_cll == expected.hdr.max_cll && actual.hdr.max_fall == expected.hdr.max_fall;
        };
        Require(sameHdrBoundary(hdrNguHookInputColor, hdrP010Frame.color),
                "NGU HDR hook input keeps source color space");
        Require(sameHdrBoundary(hdrNguHookColor, hdrP010Frame.color),
                "NGU HDR hook preserves PQ/BT.2020 peak metadata");
        Require(hdrNguHookRect.x0 == 0 && hdrNguHookRect.y0 == 0 && hdrNguHookRect.x1 == 1920 &&
                    hdrNguHookRect.y1 == 1080,
                "NGU HDR hook returns source-resolution PS_4576");
        std::printf("NGU backend HDR P010 hook is RGBA16 PQ/BT.2020 before Franky color management\n");

        auto renderHdrWindow = [&](VideoFrame frame, const char* label)
        {
            bool sawHook = false;
            pl_hook_stage hookStage = static_cast<pl_hook_stage>(0);
            pl_color_repr hookRepr = {};
            pl_color_space hookColor = {};
            pl_rect2df hookRect = {};
            backend.SetNguHookProbeForTest(
                [&](const pl_hook_params& params, const pl_hook_res& result)
                {
                    sawHook = true;
                    hookStage = params.stage;
                    hookRepr = result.repr;
                    hookColor = result.color;
                    hookRect = result.rect;
                });
            Require(SUCCEEDED(backend.Upload(frame, coloredP010.data(), coloredP010.size())), label);
            std::vector<uint8_t> pixels;
            Require(SUCCEEDED(backend.RenderOffscreen(1600, 900, pixels)), label);
            backend.SetNguHookProbeForTest({});
            Require(std::any_of(pixels.begin(), pixels.end(), [](uint8_t value) { return value != 0; }), label);
            Require(sawHook && hookStage == PL_HOOK_RGB, label);
            Require(hookRepr.bits.sample_depth == 16 && hookRepr.bits.color_depth == 16, label);
            Require(hookRect.x1 == 1920.0f && hookRect.y1 == 1080.0f, label);
            Require(sameHdrBoundary(hookColor, frame.color), label);
        };
        VideoFrame hlgP010Frame = hdrP010Frame;
        hlgP010Frame.color.transfer = PL_COLOR_TRC_HLG;
        renderHdrWindow(hlgP010Frame, "NGU BT.2020/HLG arbitrary window handoff");
        std::printf("NGU backend BT.2020/HLG window handoff PASS\n");
        VideoFrame bt709PqFrame = hdrP010Frame;
        bt709PqFrame.color.primaries = PL_COLOR_PRIM_BT_709;
        bt709PqFrame.matrix = PL_COLOR_SYSTEM_BT_709;
        renderHdrWindow(bt709PqFrame, "NGU BT.709/PQ arbitrary window handoff");
        std::printf("NGU backend BT.709/PQ window handoff PASS\n");

        RenderSettings hdrTargetSettings = settings;
        hdrTargetSettings.hdrMode = 1;
        hdrTargetSettings.hdrPassthrough = true;
        hdrTargetSettings.outputFormat = 2;
        hdrTargetSettings.peakDetect = false;
        PlaceboBackend hdrTarget;
        Require(SUCCEEDED(hdrTarget.SetSettings(hdrTargetSettings)), "NGU HDR target settings");
        Require(SUCCEEDED(hdrTarget.Upload(hdrP010Frame, coloredP010.data(), coloredP010.size())),
                "NGU HDR target upload");
        std::vector<uint8_t> hdrTargetPixels;
        Require(SUCCEEDED(hdrTarget.RenderOffscreen(1920, 1080, hdrTargetPixels, true, 16)), "NGU HDR target render");
        Require(hdrTargetPixels.size() == size_t(1920) * 1080 * 8, "NGU HDR target RGBA16 output");
        Require(std::any_of(hdrTargetPixels.begin(), hdrTargetPixels.end(), [](uint8_t value) { return value != 0; }),
                "NGU HDR target output is not black");
        std::printf("NGU backend HDR P010 reached Franky HDR target color management\n");

        WNDCLASSW presentClass = {};
        presentClass.lpfnWndProc = DefWindowProcW;
        presentClass.hInstance = GetModuleHandleW(nullptr);
        presentClass.lpszClassName = L"FrankyNguHdrHandoffTest";
        Require(RegisterClassW(&presentClass) != 0, "NGU HDR present test window class");
        HWND presentWindow = CreateWindowExW(0,
                                             presentClass.lpszClassName,
                                             L"NGU HDR handoff",
                                             WS_POPUP,
                                             0,
                                             0,
                                             64,
                                             64,
                                             nullptr,
                                             nullptr,
                                             presentClass.hInstance,
                                             nullptr);
        Require(presentWindow != nullptr, "NGU HDR present test window");
        RenderSettings presentSettings = settings;
        presentSettings.hdrMode = 2;
        presentSettings.hdrPassthrough = false;
        presentSettings.autoHdr = false;
        presentSettings.peakDetect = false;
        presentSettings.outputFormat = 2;
        PlaceboBackend present;
        Require(SUCCEEDED(present.SetSettings(presentSettings)), "NGU HDR present settings");
        Require(SUCCEEDED(present.SetWindow(presentWindow)), "NGU HDR present window");
        Require(SUCCEEDED(present.Upload(hdrP010Frame, coloredP010.data(), coloredP010.size())),
                "NGU HDR present upload");
        bool sawPresentColor = false;
        bool presentIsHdr = true;
        present.SetPresentColorProbeForTest(
            [&](const pl_color_space& color)
            {
                sawPresentColor = true;
                presentIsHdr = pl_color_space_is_hdr(&color);
            });
        bool sawPresentPixels = false;
        present.SetPresentProbeForTest(
            [&](ID3D11Texture2D* texture)
            {
                ComPtr<ID3D11Device> device;
                texture->GetDevice(&device);
                ComPtr<ID3D11DeviceContext> context;
                device->GetImmediateContext(&context);
                D3D11_TEXTURE2D_DESC desc = {};
                texture->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                ComPtr<ID3D11Texture2D> staging;
                Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "NGU HDR present staging");
                context->CopyResource(staging.Get(), texture);
                D3D11_MAPPED_SUBRESOURCE map = {};
                Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "NGU HDR present map");
                for (UINT row = 0; row < desc.Height && !sawPresentPixels; ++row)
                {
                    const auto* pixels = static_cast<const uint8_t*>(map.pData) + size_t(row) * map.RowPitch;
                    for (UINT column = 0; column < desc.Width && !sawPresentPixels; ++column)
                    {
                        if (desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
                        {
                            const uint32_t value = reinterpret_cast<const uint32_t*>(pixels)[column];
                            sawPresentPixels = (value & 0x3fffffff) != 0;
                        }
                        else
                        {
                            sawPresentPixels = pixels[column * 4] || pixels[column * 4 + 1] || pixels[column * 4 + 2];
                        }
                    }
                }
                context->Unmap(staging.Get(), 0);
            });
        Require(SUCCEEDED(present.Present()), "NGU HDR present");
        Require(sawPresentColor && !presentIsHdr, "NGU HDR reaches Franky SDR color management");
        Require(sawPresentPixels, "NGU HDR final present is not black");
        std::printf("NGU backend HDR passed Franky SDR color management and presentation\n");
        present.SetWindow(nullptr);
        DestroyWindow(presentWindow);

        Require(SUCCEEDED(backend.Upload(coloredP010Frame, coloredP010.data(), coloredP010.size())),
                "NGU HDR-to-SDR P010 upload");
        std::vector<uint8_t> hdrToSdrPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(1920, 1080, hdrToSdrPixels)), "NGU HDR-to-SDR P010 render");
        const auto hdrToSdrMean = meanRgb(hdrToSdrPixels);
        std::printf(
            "NGU backend HDR-to-SDR transition avg=%u,%u,%u\n", hdrToSdrMean[0], hdrToSdrMean[1], hdrToSdrMean[2]);
        Require((p010Mean[0] > p010Mean[2]) == (hdrToSdrMean[0] > hdrToSdrMean[2]),
                "NGU HDR-to-SDR chroma orientation");

        if (nv12Oracle)
        {
            const auto i420Size = std::filesystem::file_size(argv[3]);
            Require(i420Size == size_t(1280) * 720 * 3 / 2 || i420Size == size_t(1920) * 1080 * 3 / 2,
                    "NV12 oracle source geometry");
            const int oracleWidth = i420Size == size_t(1280) * 720 * 3 / 2 ? 1280 : 1920;
            const int oracleHeight = i420Size == size_t(1280) * 720 * 3 / 2 ? 720 : 1080;
            const int expectedOutputWidth = oracleWidth == 1920 ? 1920 : 3840;
            const int expectedOutputHeight = oracleHeight == 1080 ? 1080 : 2160;
            const auto oracleNv12 = ReadI420AsNv12(argv[3], oracleWidth, oracleHeight);
            VideoFrame oracleFrame;
            oracleFrame.width = oracleWidth;
            oracleFrame.height = oracleHeight;
            oracleFrame.stride = oracleWidth;
            oracleFrame.format = PixelFormat::NV12;
            oracleFrame.matrix = PL_COLOR_SYSTEM_BT_709;
            oracleFrame.levels = PL_COLOR_LEVELS_LIMITED;
            oracleFrame.color = pl_color_space_bt709;
            Require(SUCCEEDED(backend.Upload(oracleFrame, oracleNv12.data(), oracleNv12.size())),
                    "NGU NV12 oracle upload");
            std::vector<uint8_t> oraclePixels;
            Require(SUCCEEDED(backend.RenderOffscreen(expectedOutputWidth, expectedOutputHeight, oraclePixels)),
                    "NGU NV12 oracle render");
            const auto oracleExpected =
                std::filesystem::path(argv[4]).extension() == ".bmp"
                    ? ReadBmp32AsTopDownRgba(argv[4])
                    : BgraToRgba(ReadBytes(argv[4], size_t(expectedOutputWidth) * expectedOutputHeight * 4));
            int oracleMaximum = 0;
            int oracleOverOne = 0;
            const int oracleDifferent = CompareRgb(oraclePixels, oracleExpected, oracleMaximum, oracleOverOne);
            std::array<int64_t, 3> oracleDelta = {};
            for (size_t i = 0; i < oraclePixels.size(); i += 4)
                for (int channel = 0; channel < 3; ++channel)
                    oracleDelta[channel] += int(oraclePixels[i + channel]) - int(oracleExpected[i + channel]);
            const int oraclePixelCount = int(oraclePixels.size() / 4);
            std::printf("NGU backend NV12 %dx%d oracle different=%d maximum=%d "
                        "diff_gt_1_pixels=%d mean_delta=%.4f,%.4f,%.4f\n",
                        oracleWidth,
                        oracleHeight,
                        oracleDifferent,
                        oracleMaximum,
                        oracleOverOne,
                        double(oracleDelta[0]) / oraclePixelCount,
                        double(oracleDelta[1]) / oraclePixelCount,
                        double(oracleDelta[2]) / oraclePixelCount);
            if (oracleMaximum > 1)
            {
                std::printf("  actual=%u,%u,%u expected=%u,%u,%u\n",
                            unsigned(oraclePixels[0]),
                            unsigned(oraclePixels[1]),
                            unsigned(oraclePixels[2]),
                            unsigned(oracleExpected[0]),
                            unsigned(oracleExpected[1]),
                            unsigned(oracleExpected[2]));
            }

            D3D11_TEXTURE2D_DESC nv12Desc = {};
            nv12Desc.Width = oracleWidth;
            nv12Desc.Height = oracleHeight;
            nv12Desc.MipLevels = 1;
            nv12Desc.ArraySize = 1;
            nv12Desc.Format = DXGI_FORMAT_NV12;
            nv12Desc.SampleDesc.Count = 1;
            nv12Desc.Usage = D3D11_USAGE_DEFAULT;
            nv12Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA nv12Data = {};
            nv12Data.pSysMem = oracleNv12.data();
            nv12Data.SysMemPitch = oracleWidth;
            ComPtr<ID3D11Texture2D> nv12Texture;
            Require(SUCCEEDED(backend.Device()->CreateTexture2D(&nv12Desc, &nv12Data, &nv12Texture)),
                    "NGU NV12 decoder texture");
            Require(SUCCEEDED(backend.Import(oracleFrame, nv12Texture.Get(), 0)), "NGU NV12 oracle import");
            std::vector<uint8_t> importedPixels;
            Require(SUCCEEDED(backend.RenderOffscreen(expectedOutputWidth, expectedOutputHeight, importedPixels)),
                    "NGU NV12 imported render");
            int importedMaximum = 0;
            int importedOverOne = 0;
            const int importedDifferent = CompareRgb(importedPixels, oracleExpected, importedMaximum, importedOverOne);
            std::printf("NGU backend imported NV12 %dx%d oracle different=%d "
                        "maximum=%d diff_gt_1_pixels=%d\n",
                        oracleWidth,
                        oracleHeight,
                        importedDifferent,
                        importedMaximum,
                        importedOverOne);
            if (importedMaximum > 1)
            {
                std::printf("  actual=%u,%u,%u expected=%u,%u,%u\n",
                            unsigned(importedPixels[0]),
                            unsigned(importedPixels[1]),
                            unsigned(importedPixels[2]),
                            unsigned(oracleExpected[0]),
                            unsigned(oracleExpected[1]),
                            unsigned(oracleExpected[2]));
            }
            Require(oracleMaximum <= 1 && oracleOverOne == 0 && importedMaximum <= 1 && importedOverOne == 0,
                    "NGU NV12 oracle acceptance");
        }

        VideoFrame p010;
        p010.width = 1280;
        p010.height = 720;
        p010.stride = 1280 * 2;
        p010.format = PixelFormat::P010;
        p010.matrix = PL_COLOR_SYSTEM_BT_709;
        p010.levels = PL_COLOR_LEVELS_LIMITED;
        p010.color = pl_color_space_bt709;
        const size_t yBytes = size_t(1280) * 720 * 2;
        const size_t uvBytes = size_t(640) * 360 * 4;
        std::vector<uint8_t> p010Bytes(yBytes + uvBytes);
        for (int y = 0; y < 720; ++y)
            for (int x = 0; x < 1280; ++x)
            {
                const uint16_t value = uint16_t((16 + (x * 219) / 1279) << 6);
                const size_t offset = (size_t(y) * 1280 + x) * 2;
                p010Bytes[offset] = uint8_t(value);
                p010Bytes[offset + 1] = uint8_t(value >> 8);
            }
        for (size_t i = 0; i < uvBytes; i += 4)
        {
            const uint16_t neutral = 128 << 6;
            std::memcpy(p010Bytes.data() + yBytes + i, &neutral, sizeof(neutral));
            std::memcpy(p010Bytes.data() + yBytes + i + 2, &neutral, sizeof(neutral));
        }
        D3D11_TEXTURE2D_DESC p010Desc = {};
        p010Desc.Width = 1280;
        p010Desc.Height = 720;
        p010Desc.MipLevels = 1;
        p010Desc.ArraySize = 1;
        p010Desc.Format = DXGI_FORMAT_P010;
        p010Desc.SampleDesc.Count = 1;
        p010Desc.Usage = D3D11_USAGE_DEFAULT;
        p010Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA p010Data = {};
        p010Data.pSysMem = p010Bytes.data();
        p010Data.SysMemPitch = 1280 * 2;
        ComPtr<ID3D11Texture2D> p010Texture;
        Require(SUCCEEDED(backend.Device()->CreateTexture2D(&p010Desc, &p010Data, &p010Texture)),
                "create P010 decoder texture");
        Require(SUCCEEDED(backend.Import(p010, p010Texture.Get(), 0)), "NGU P010 import");
        std::vector<uint8_t> p010Pixels;
        const HRESULT p010Render = backend.RenderOffscreen(3840, 2160, p010Pixels);
        if (FAILED(p010Render))
            throw std::runtime_error("NGU P010 backend render: " + backend.LastError());
        Require(p010Pixels.size() == size_t(3840) * 2160 * 4, "NGU P010 output size");
        Require(p010Pixels[0] || p010Pixels[1] || p010Pixels[2], "NGU P010 output is not black");
        std::vector<uint8_t> p010DoublePixels;
        Require(SUCCEEDED(backend.RenderOffscreen(2560, 1440, p010DoublePixels)), "NGU P010 720 Double render");
        Require(p010DoublePixels.size() == size_t(2560) * 1440 * 4, "NGU P010 720 Double output size");
        Require(std::any_of(p010DoublePixels.begin(), p010DoublePixels.end(), [](uint8_t value) { return value != 0; }),
                "NGU P010 720 Double output is not black");
        std::printf("NGU backend P010 720 Double high-bit path nonblack\n");

        if (argc == 10 || argc == 14)
        {
            const struct
            {
                const char* label;
                PixelFormat format;
                const char* raw;
                const char* capture;
            } class3Cases[] = {
                {"P210", PixelFormat::P210, argv[2], argv[3]},
                {"P216", PixelFormat::P216, argv[4], argv[5]},
                {"P210-second", PixelFormat::P210, argv[6], argv[7]},
                {"P216-second", PixelFormat::P216, argv[8], argv[9]},
            };

            auto runClass3 = [&](const char* label,
                                 PixelFormat format,
                                 const char* rawPath,
                                 const char* capture,
                                 int width,
                                 int height,
                                 bool benchmark = false)
            {
                VideoFrame class3;
                class3.width = width;
                class3.height = height;
                class3.stride = width * 2;
                class3.format = format;
                class3.matrix = PL_COLOR_SYSTEM_BT_709;
                class3.levels = PL_COLOR_LEVELS_LIMITED;
                class3.color = pl_color_space_bt709;
                const size_t packedBytes = size_t(width) * height * 2 + size_t(width / 2) * height * 4;
                const auto class3Bytes = ReadBytes(rawPath, packedBytes);
                Require(SUCCEEDED(backend.Upload(class3, class3Bytes.data(), class3Bytes.size())),
                        "class-3 backend upload");
                std::vector<uint8_t> class3Pixels;
                const HRESULT class3Render = backend.RenderOffscreen(3840, 2160, class3Pixels);
                if (FAILED(class3Render))
                    throw std::runtime_error(std::string(label) + " backend render: " + backend.LastError());
                const auto expectedClass3 = BgraToRgba(ReadBytes(FindFinalCapture(capture), size_t(3840) * 2160 * 4));
                int class3Maximum = 0;
                int class3OverOne = 0;
                const int class3Different = CompareRgb(class3Pixels, expectedClass3, class3Maximum, class3OverOne);
                std::printf("NGU backend %s different=%d maximum=%d "
                            "diff_gt_1_pixels=%d\n",
                            label,
                            class3Different,
                            class3Maximum,
                            class3OverOne);
                if (class3Maximum > 1)
                {
                    std::printf("  actual=%u,%u,%u expected=%u,%u,%u\n",
                                unsigned(class3Pixels[0]),
                                unsigned(class3Pixels[1]),
                                unsigned(class3Pixels[2]),
                                unsigned(expectedClass3[0]),
                                unsigned(expectedClass3[1]),
                                unsigned(expectedClass3[2]));
                }
                Require(class3Maximum <= 1 && class3OverOne == 0, "class-3 backend acceptance");
                if (benchmark)
                {
                    const auto start = std::chrono::steady_clock::now();
                    constexpr int benchmarkRuns = 3;
                    for (int run = 0; run < benchmarkRuns; ++run)
                    {
                        std::vector<uint8_t> timedPixels;
                        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, timedPixels)),
                                "class-3 benchmark render");
                    }
                    const double elapsedMs =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    std::printf(
                        "NGU backend %s warm_avg_ms=%.3f runs=%d\n", label, elapsedMs / benchmarkRuns, benchmarkRuns);
                }
            };
            for (const auto& test : class3Cases)
            {
                runClass3(test.label, test.format, test.raw, test.capture, 1280, 720);
            }
            if (argc == 14)
            {
                runClass3("P210-1080", PixelFormat::P210, argv[10], argv[11], 1920, 1080, true);
                runClass3("P216-1080", PixelFormat::P216, argv[12], argv[13], 1920, 1080, true);
            }
        }

        auto secondParams = pl_d3d11_default_params;
        auto secondD3d = pl_d3d11_create(nullptr, &secondParams);
        Require(bool(secondD3d), "create second D3D11 backend");
        {
            PlaceboBackend switched;
            Require(SUCCEEDED(switched.SetSettings(settings)), "switched-device settings");
            Require(SUCCEEDED(switched.SetDevice(pl_d3d11_get(secondD3d->gpu)->device)), "switched renderer device");
            Require(SUCCEEDED(switched.Upload(nv12Frame720, nv12_720.data(), nv12_720.size())),
                    "switched-device upload");
            std::vector<uint8_t> switchedPixels;
            Require(SUCCEEDED(switched.RenderOffscreen(1920, 1080, switchedPixels)), "switched-device render");
            Require(switchedPixels.size() == size_t(1920) * 1080 * 4, "switched-device output size");
        }
        pl_d3d11_destroy(&secondD3d);
        std::printf("NGU backend device switch/source renegotiation PASS\n");

        std::printf("PASS_PRODUCT ngu_sharp backend RGB48 + P010 + class-3 -> 4K\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
