#define NOMINMAX
#include "Renderer/Backend.h"
#include "Renderer/RenderOptions.h"
#include <libplacebo/colorspace.h>
#include <wrl/client.h>
#include <algorithm>
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
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(expected))),
            "read capture");
    return bytes;
}

int CompareRgb(const std::vector<uint8_t>& actual,
               const std::vector<uint8_t>& expected,
               int& maximum,
               int& overOne)
{
    Require(actual.size() == expected.size() && actual.size() % 4 == 0,
            "final comparison size");
    maximum = 0;
    overOne = 0;
    int different = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 4)
    {
        bool pixelOverOne = false;
        for (int channel = 0; channel < 3; ++channel)
        {
            const int delta = std::abs(int(actual[offset + channel]) -
                                       int(expected[offset + channel]));
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
        Require(argc == 2, "usage: test_ngu_backend <capture-root>");
        Require(ResolveNativeScaler("ngu_sharp") == NativeScalerKind::NguSharp,
                "native scaler resolver NGU");
        Require(ResolveNativeScaler("ewa_lanczos") == NativeScalerKind::None,
                "native scaler resolver ordinary filter");
        const auto root = std::filesystem::path(argv[1]);
        const auto rgba16 =
            ReadBytes(root / "input_00224_PS_460_bfe50921_s0.bin",
                      size_t(1280) * 720 * 8);
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
        Require(SUCCEEDED(BuildRenderOptions(options, settings)),
                "NGU render options");
        pl_options_free(&options);
        Require(SUCCEEDED(backend.SetSettings(settings)), "NGU backend settings");

        VideoFrame frame;
        frame.width = 1280;
        frame.height = 720;
        frame.stride = 1280 * 6;
        frame.format = PixelFormat::RGB48;
        frame.matrix = PL_COLOR_SYSTEM_RGB;
        frame.levels = PL_COLOR_LEVELS_FULL;
        frame.color = pl_color_space_srgb;
        Require(SUCCEEDED(backend.Upload(
                    frame, rgb48.data(), rgb48.size())),
                "NGU RGB48 upload");

        std::vector<uint8_t> pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, pixels)),
                "NGU backend render");
        Require(pixels.size() == size_t(3840) * 2160 * 4, "NGU output size");
        const auto expected =
            ReadBytes(root / "draw_00266_PS_1352_6992f6f0_rt0_3840x2160_f21.bin",
                      size_t(3840) * 2160 * 4);
        int maximum = 0, overOne = 0;
        const int different = CompareRgb(pixels, expected, maximum, overOne);
        std::printf("NGU backend RGB different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    different, maximum, overOne);
        Require(maximum <= 1 && overOne == 0, "NGU backend RGB acceptance");

        uint64_t capturedId = 0;
        const HRESULT captured = backend.CaptureUpload(
            frame, rgb48.data(), rgb48.size(), capturedId);
        if (FAILED(captured))
            throw std::runtime_error("NGU stored-frame capture: " + backend.LastError());
        std::vector<uint8_t> storedPixels;
        const HRESULT storedRender =
            backend.RenderOffscreen(3840, 2160, storedPixels);
        if (FAILED(storedRender))
            throw std::runtime_error("NGU stored-frame render: " + backend.LastError());
        int storedMaximum = 0, storedOverOne = 0;
        const int storedDifferent =
            CompareRgb(storedPixels, expected, storedMaximum, storedOverOne);
        std::printf("NGU backend stored RGB different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    storedDifferent, storedMaximum, storedOverOne);
        Require(storedMaximum <= 1 && storedOverOne == 0,
                "NGU stored-frame RGB acceptance");

        std::vector<uint8_t> pattern(rgb48.size());
        for (size_t i = 0; i < size_t(1280) * 720; ++i)
        {
            const uint16_t red = uint16_t(30000 + (i % 1280) * 20);
            const uint16_t green = uint16_t(40000 + ((i / 1280) % 720) * 20);
            std::memcpy(pattern.data() + i * 6, &red, sizeof(red));
            std::memcpy(pattern.data() + i * 6 + 2, &green, sizeof(green));
        }
        Require(SUCCEEDED(backend.Upload(frame, pattern.data(), pattern.size())),
                "NGU pattern upload");
        std::vector<uint8_t> patternPixels;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, patternPixels)),
                "NGU pattern render");
        Require(patternPixels[0] || patternPixels[1] || patternPixels[2],
                "NGU pattern output is not black");

        const auto capture1080 = root.parent_path() / "luma_1080_full_v17";
        const auto rgba1080 =
            ReadBytes(capture1080 / "input_31703_PS_460_bfe50921_s0.bin",
                      size_t(1920) * 1080 * 8);
        std::vector<uint8_t> rgb48_1080(size_t(1920) * 1080 * 6);
        for (size_t i = 0; i < size_t(1920) * 1080; ++i)
            std::memcpy(rgb48_1080.data() + i * 6, rgba1080.data() + i * 8, 6);
        VideoFrame frame1080 = frame;
        frame1080.width = 1920;
        frame1080.height = 1080;
        frame1080.stride = 1920 * 6;
        Require(SUCCEEDED(backend.Upload(
                    frame1080, rgb48_1080.data(), rgb48_1080.size())),
                "NGU 1080 RGB48 upload");
        std::vector<uint8_t> pixels1080;
        Require(SUCCEEDED(backend.RenderOffscreen(3840, 2160, pixels1080)),
                "NGU 1080 render");
        const auto expected1080 =
            ReadBytes(capture1080 /
                          "draw_31727_PS_1352_6992f6f0_rt0_3840x2160_f21.bin",
                      size_t(3840) * 2160 * 4);
        int maximum1080 = 0, overOne1080 = 0;
        const int different1080 =
            CompareRgb(pixels1080, expected1080, maximum1080, overOne1080);
        std::printf("NGU backend 1080 RGB different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    different1080, maximum1080, overOne1080);
        Require(maximum1080 <= 1 && overOne1080 == 0,
                "NGU 1080 RGB acceptance");

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
                const uint16_t value =
                    uint16_t((16 + (x * 219) / 1279) << 6);
                const size_t offset = (size_t(y) * 1280 + x) * 2;
                p010Bytes[offset] = uint8_t(value);
                p010Bytes[offset + 1] = uint8_t(value >> 8);
            }
        for (size_t i = 0; i < uvBytes; i += 4)
        {
            const uint16_t neutral = 128 << 6;
            std::memcpy(p010Bytes.data() + yBytes + i,
                        &neutral,
                        sizeof(neutral));
            std::memcpy(p010Bytes.data() + yBytes + i + 2,
                        &neutral,
                        sizeof(neutral));
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
        Require(SUCCEEDED(backend.Device()->CreateTexture2D(
                    &p010Desc, &p010Data, &p010Texture)),
                "create P010 decoder texture");
        Require(SUCCEEDED(backend.Import(p010, p010Texture.Get(), 0)),
                "NGU P010 import");
        std::vector<uint8_t> p010Pixels;
        const HRESULT p010Render =
            backend.RenderOffscreen(3840, 2160, p010Pixels);
        if (FAILED(p010Render))
            throw std::runtime_error(
                "NGU P010 backend render: " + backend.LastError());
        Require(p010Pixels.size() == size_t(3840) * 2160 * 4,
                "NGU P010 output size");
        Require(p010Pixels[0] || p010Pixels[1] || p010Pixels[2],
                "NGU P010 output is not black");

        std::printf(
            "PASS_PRODUCT ngu_sharp backend RGB48 + P010 -> 4K\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
