// Executes a complete NGU multi-pass chain through libplacebo and compares the
// final float16 output against a captured target render target.
#define NOMINMAX
#include <libplacebo/d3d11.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/log.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/upload.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Context
{
    pl_gpu gpu = nullptr;
    pl_dispatch dispatch = nullptr;
    std::vector<pl_tex> ownedTextures;
};

void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

void FilteredLog(void*, enum pl_log_level level, const char* message)
{
    if (std::strstr(message, "hook pass") ||
        std::strstr(message, "Skipping hook") ||
        std::strstr(message, "Saving output") ||
        std::strstr(message, "Failed") ||
        level <= PL_LOG_ERR) {
        std::printf("log[%d] %s\n", static_cast<int>(level), message);
    }
}

std::string ReadText(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    Require(file.good(), "open shader");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> ReadBytes(const std::string& path, size_t expected = 0)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(file.good(), "open binary");
    const auto size = static_cast<size_t>(file.tellg());
    if (expected)
        Require(size == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(file.good() || file.eof(), "read binary");
    return bytes;
}

pl_tex CreatePassTexture(void* priv, int width, int height)
{
    auto* context = static_cast<Context*>(priv);
    const pl_fmt format = pl_find_fmt(
        context->gpu, PL_FMT_FLOAT, 4, 16, 16,
        static_cast<pl_fmt_caps>(
            PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE |
            PL_FMT_CAP_STORABLE | PL_FMT_CAP_HOST_READABLE));
    Require(format != nullptr, "rgba16f pass texture format");
    pl_tex_params params = {};
    params.w = width;
    params.h = height;
    params.format = format;
    params.sampleable = true;
    params.renderable = true;
    params.storable = true;
    params.host_readable = true;
    pl_tex texture = pl_tex_create(context->gpu, &params);
    Require(texture != nullptr, "create pass texture");
    context->ownedTextures.push_back(texture);
    return texture;
}

int CountHalfDifferences(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2) {
        const uint16_t actualHalf = static_cast<uint16_t>(actual[offset]) |
                                    (static_cast<uint16_t>(actual[offset + 1]) << 8);
        const uint16_t expectedHalf = static_cast<uint16_t>(expected[offset]) |
                                      (static_cast<uint16_t>(expected[offset + 1]) << 8);
        different += actualHalf != expectedHalf;
    }
    return different;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        Require(
            argc == 8 || argc == 10 || argc == 11,
            "usage: compare_ngu_chain <shader> <l16-input|@zero> <expected> <width> <height> <summary> <actual> [source-width source-height [second-l16-input]]");
        const std::string shaderPath = argv[1];
        const std::string inputPath = argv[2];
        const std::string expectedPath = argv[3];
        const int width = std::stoi(argv[4]);
        const int height = std::stoi(argv[5]);
        const std::string summaryPath = argv[6];
        const std::string actualPath = argv[7];
        Require(width > 0 && height > 0, "output dimensions");

        const int sourceWidth = argc >= 10 ? std::stoi(argv[8]) : 640;
        const int sourceHeight = argc >= 10 ? std::stoi(argv[9]) : 360;
        Require(sourceWidth > 0 && sourceHeight > 0, "source dimensions");
        const bool pairedInput = argc == 11;
        Require(inputPath != "@pair", "@pair is no longer a path sentinel");
        const int sourceComponents = pairedInput ? 2 : 1;
        const auto expected = ReadBytes(
            expectedPath, static_cast<size_t>(width) * height * 8);
        const std::string source = ReadText(shaderPath);

        pl_log_params logParams = {};
        logParams.log_cb = FilteredLog;
        logParams.log_priv = stdout;
        logParams.log_level = PL_LOG_TRACE;
        pl_log log = pl_log_create(PL_API_VER, &logParams);
        Require(log != nullptr, "libplacebo log");

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(log, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        Context context;
        context.gpu = d3d->gpu;
        context.dispatch = pl_dispatch_create(log, context.gpu);
        Require(context.dispatch != nullptr, "libplacebo dispatch");

        const size_t sourcePixels = static_cast<size_t>(sourceWidth) * sourceHeight;
        std::vector<uint16_t> l16Input(sourcePixels * sourceComponents);
        if (inputPath != "@zero") {
            const auto firstBytes = ReadBytes(inputPath, sourcePixels * 8);
            std::vector<uint8_t> secondBytes;
            if (pairedInput)
                secondBytes = ReadBytes(argv[10], sourcePixels * 8);
            for (size_t index = 0; index < sourcePixels; ++index) {
                l16Input[index * sourceComponents] =
                    static_cast<uint16_t>(firstBytes[index * 8]) |
                    (static_cast<uint16_t>(firstBytes[index * 8 + 1]) << 8);
                if (pairedInput) {
                    l16Input[index * 2 + 1] =
                        static_cast<uint16_t>(secondBytes[index * 8]) |
                        (static_cast<uint16_t>(secondBytes[index * 8 + 1]) << 8);
                }
            }
        }

        pl_plane inputPlane = {};
        pl_tex inputTexture = nullptr;
        pl_plane_data inputData = {};
        inputData.type = PL_FMT_UNORM;
        inputData.width = sourceWidth;
        inputData.height = sourceHeight;
        inputData.component_size[0] = 16;
        inputData.component_map[0] = 0;
        if (pairedInput) {
            inputData.component_size[1] = 16;
            inputData.component_map[1] = 1;
        }
        inputData.pixel_stride = sourceComponents * 2;
        inputData.row_stride = static_cast<size_t>(sourceWidth) * sourceComponents * 2;
        inputData.pixels = l16Input.data();
        Require(pl_upload_plane(context.gpu, &inputPlane, &inputTexture, &inputData), "upload L16 input");
        inputPlane.shift_x = inputPlane.shift_y = 0;
        inputPlane.flipped = false;

        const pl_hook* hook = pl_mpv_user_shader_parse(context.gpu, source.data(), source.size());
        Require(hook != nullptr, "parse NGU chain");
        Require(hook->input == PL_HOOK_SIG_TEX, "NGU chain must accept texture input");
        Require((hook->stages & PL_HOOK_CHROMA_INPUT) != 0, "NGU chain must hook chroma");

        if (hook->reset)
            hook->reset(hook->priv);
        pl_dispatch_reset_frame(context.dispatch);
        const auto repr = pl_color_repr_rgb;
        const auto color = pl_color_space_bt709;
        pl_hook_params hookParams = {};
        hookParams.gpu = context.gpu;
        hookParams.dispatch = context.dispatch;
        hookParams.get_tex = CreatePassTexture;
        hookParams.priv = &context;
        hookParams.stage = PL_HOOK_CHROMA_INPUT;
        hookParams.tex = inputTexture;
        hookParams.rect = {0.0f, 0.0f, 1.0f, 1.0f};
        hookParams.repr = repr;
        hookParams.color = color;
        hookParams.components = 4;
        hookParams.orig_repr = &repr;
        hookParams.orig_color = &color;
        hookParams.src_rect = {0.0f, 0.0f, static_cast<float>(sourceWidth), static_cast<float>(sourceHeight)};
        hookParams.dst_rect = {0, 0, sourceWidth, sourceHeight};

        const auto start = std::chrono::steady_clock::now();
        const pl_hook_res result = hook->hook(hook->priv, &hookParams);
        Require(!result.failed, "execute NGU chain");
        Require(result.output == PL_HOOK_SIG_TEX && result.tex != nullptr, "NGU chain output");
        Require(result.tex->params.w == width && result.tex->params.h == height, "NGU output dimensions");
        pl_gpu_finish(context.gpu);
        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        std::vector<uint8_t> actual(expected.size());
        pl_tex_transfer_params transfer = {};
        transfer.tex = result.tex;
        transfer.ptr = actual.data();
        transfer.row_pitch = static_cast<size_t>(width) * 8;
        Require(pl_tex_download(context.gpu, &transfer), "download NGU output");
        const int different = CountHalfDifferences(actual, expected);

        std::ofstream actualFile(actualPath, std::ios::binary);
        Require(actualFile.good(), "write actual output");
        actualFile.write(
            reinterpret_cast<const char*>(actual.data()),
            static_cast<std::streamsize>(actual.size()));
        actualFile.close();

        std::ofstream summary(summaryPath, std::ios::binary);
        Require(summary.good(), "write summary");
        summary << "{\n"
                << "  \"status\": \"" << (different == 0 ? "PASS_PRODUCT" : "FAIL_PRODUCT") << "\",\n"
                << "  \"shader\": \"" << shaderPath << "\",\n"
                << "  \"output\": [" << width << ", " << height << "],\n"
                << "  \"components\": " << actual.size() / 2 << ",\n"
                << "  \"different_half_components\": " << different << ",\n"
                << "  \"elapsed_ms\": " << elapsedMs << "\n"
                << "}\n";
        summary.close();

        pl_mpv_user_shader_destroy(&hook);
        for (pl_tex texture : context.ownedTextures)
            pl_tex_destroy(context.gpu, &texture);
        pl_tex_destroy(context.gpu, &inputTexture);
        pl_dispatch_destroy(&context.dispatch);
        pl_d3d11_destroy(&d3d);
        pl_log_destroy(&log);

        std::printf(
            "different_half_components=%d elapsed_ms=%.3f\n",
            different,
            elapsedMs);
        return different == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
