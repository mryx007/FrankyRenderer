#define NOMINMAX
#include "Renderer/Backend.h"
#include "Renderer/RenderOptions.h"
#include <libplacebo/colorspace.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

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
        settings.nativeNguSharp = true;
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
        std::printf("PASS_PRODUCT ngu_sharp backend RGB48 -> 4K\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
