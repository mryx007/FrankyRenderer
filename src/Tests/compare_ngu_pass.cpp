// Compares one generated NGU GLSL pass against a captured madVR render target.
// The pass is executed through Franky's libplacebo mpv-user-shader path.
#define NOMINMAX
#include <libplacebo/d3d11.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/log.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/upload.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

std::string SelectSingleOutputShader(const std::string& source, int outputIndex)
{
    const std::string passMarker = "//!DESC";
    const std::string textureMarker = "//!TEXTURE ngu_cubici2_constants";
    std::vector<size_t> passStarts;
    size_t cursor = 0;
    while ((cursor = source.find(passMarker, cursor)) != std::string::npos) {
        passStarts.push_back(cursor);
        cursor += passMarker.size();
    }
    Require(passStarts.size() == 4, "expected four MRT-split shader passes");
    Require(outputIndex >= 0 && outputIndex < static_cast<int>(passStarts.size()), "output index");
    const size_t textureStart = source.find(textureMarker);
    const size_t passEnd =
        outputIndex + 1 < static_cast<int>(passStarts.size()) ? passStarts[outputIndex + 1]
        : textureStart != std::string::npos ? textureStart
                                            : source.size();
    Require(passEnd != std::string::npos, "shader pass ending");
    std::string selected =
        source.substr(passStarts[outputIndex], passEnd - passStarts[outputIndex]) +
        (textureStart != std::string::npos ? source.substr(textureStart) : std::string());
    const size_t saveStart = selected.find("//!SAVE ");
    Require(saveStart != std::string::npos, "selected pass SAVE directive");
    const size_t saveEnd = selected.find('\n', saveStart);
    Require(saveEnd != std::string::npos, "selected pass SAVE line ending");
    selected.erase(saveStart, saveEnd - saveStart + 1);
    return selected;
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

int CountLowHalfDifferences(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
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

float HalfToFloat(uint16_t bits)
{
    const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
    uint32_t exponent = (bits >> 10) & 0x1fu;
    uint32_t mantissa = bits & 0x3ffu;
    uint32_t floatBits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            floatBits = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3ffu;
            floatBits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        floatBits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        floatBits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float value = 0.0f;
    std::memcpy(&value, &floatBits, sizeof(value));
    return value;
}

void WriteSummary(
    const std::string& path,
    const std::string& shader,
    const std::string& input,
    const std::vector<std::string>& expectedPaths,
    const std::array<int, 4>& bitDifferences,
    double elapsedMs)
{
    std::ofstream out(path, std::ios::binary);
    Require(out.good(), "write summary");
    out << "{\n"
        << "  \"status\": \"" << (std::all_of(bitDifferences.begin(), bitDifferences.end(), [](int value) { return value == 0; }) ? "PASS_PRODUCT" : "FAIL_PRODUCT") << "\",\n"
        << "  \"shader\": \"" << shader << "\",\n"
        << "  \"input\": \"" << input << "\",\n"
        << "  \"geometry\": [640, 360],\n"
        << "  \"expected\": [";
    for (size_t index = 0; index < expectedPaths.size(); ++index) {
        if (index)
            out << ", ";
        out << '"' << expectedPaths[index] << '"';
    }
    out << "],\n"
        << "  \"exact_half_components\": [";
    for (int index = 0; index < 4; ++index) {
        if (index)
            out << ", ";
        out << (640 * 360 * 4 - bitDifferences[index]);
    }
    out << "],\n"
        << "  \"different_half_components\": [";
    for (int index = 0; index < 4; ++index) {
        if (index)
            out << ", ";
        out << bitDifferences[index];
    }
    out << "],\n"
        << "  \"elapsed_ms\": " << elapsedMs << "\n"
        << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        Require(argc == 6, "usage: compare_ngu_pass <shader> <input> <expected-prefix> <summary> <run-prefix>");
        const std::string shaderPath = argv[1];
        const std::string inputPath = argv[2];
        const std::string expectedPrefix = argv[3];
        const std::string summaryPath = argv[4];
        const std::string runPrefix = argv[5];

        constexpr int width = 640;
        constexpr int height = 360;
        constexpr size_t sampleCount = static_cast<size_t>(width) * height;
        constexpr size_t inputBytes = sampleCount * 8;
        constexpr size_t outputBytes = inputBytes;

        const std::string source = ReadText(shaderPath);
        const auto inputBytesData = ReadBytes(inputPath, inputBytes);
        std::vector<std::string> expectedPaths;
        std::vector<std::vector<uint8_t>> expected;
        for (int index = 0; index < 4; ++index)
            expectedPaths.push_back(expectedPrefix + std::to_string(index) + "_640x360_f113.bin");
        for (const auto& path : expectedPaths)
            expected.push_back(ReadBytes(path, outputBytes));

        pl_log_params logParams = {};
        logParams.log_cb = pl_log_simple;
        logParams.log_priv = stdout;
        logParams.log_level = PL_LOG_WARN;
        pl_log log = pl_log_create(PL_API_VER, &logParams);
        Require(log != nullptr, "libplacebo log");
        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(log, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        Context context;
        context.gpu = d3d->gpu;
        context.dispatch = pl_dispatch_create(log, context.gpu);
        Require(context.dispatch != nullptr, "libplacebo dispatch");

        const auto readU16 = [&](size_t offset)
        {
            return static_cast<uint16_t>(
                static_cast<uint16_t>(inputBytesData[offset]) |
                (static_cast<uint16_t>(inputBytesData[offset + 1]) << 8));
        };
        const bool preservedIntegerInput =
            readU16(0) == readU16(2) &&
            readU16(0) == readU16(4) &&
            readU16(6) == 0xffffu;

        std::vector<uint16_t> l16Input(sampleCount);
        for (size_t index = 0; index < sampleCount; ++index) {
            if (preservedIntegerInput) {
                l16Input[index] = readU16(index * 8);
            } else {
                uint16_t half = 0;
                std::memcpy(&half, inputBytesData.data() + index * 8, sizeof(half));
                const float value = std::clamp(HalfToFloat(half), 0.0f, 1.0f);
                l16Input[index] = static_cast<uint16_t>(value * 65535.0f + 0.5f);
            }
        }

        pl_plane inputPlane = {};
        pl_tex inputTexture = nullptr;
        pl_plane_data inputData = {};
        inputData.type = PL_FMT_UNORM;
        inputData.width = width;
        inputData.height = height;
        inputData.component_size[0] = 16;
        inputData.component_map[0] = 0;
        inputData.pixel_stride = 2;
        inputData.row_stride = static_cast<size_t>(width) * 2;
        inputData.pixels = l16Input.data();
        Require(pl_upload_plane(context.gpu, &inputPlane, &inputTexture, &inputData), "upload captured input");
        inputPlane.shift_x = inputPlane.shift_y = 0;
        inputPlane.flipped = false;

        const auto repr = pl_color_repr_rgb;
        const auto color = pl_color_space_bt709;
        std::array<int, 4> bitDifferences = {};
        const auto start = std::chrono::steady_clock::now();

        for (int outputIndex = 0; outputIndex < 4; ++outputIndex) {
            const std::string singleOutput = SelectSingleOutputShader(source, outputIndex);
            const pl_hook* hook =
                pl_mpv_user_shader_parse(context.gpu, singleOutput.data(), singleOutput.size());
            Require(hook != nullptr, "parse NGU GLSL pass");
            Require(hook->input == PL_HOOK_SIG_TEX, "NGU pass must accept a texture hook input");
            Require((hook->stages & PL_HOOK_CHROMA_INPUT) != 0, "NGU pass must hook chroma input");

            if (hook->reset)
                hook->reset(hook->priv);
            pl_dispatch_reset_frame(context.dispatch);
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
            hookParams.src_rect = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
            hookParams.dst_rect = {0, 0, width, height};

            const pl_hook_res result = hook->hook(hook->priv, &hookParams);
            if (result.failed)
                throw std::runtime_error("execute NGU GLSL pass rt" + std::to_string(outputIndex));
            Require(result.output == PL_HOOK_SIG_TEX && result.tex != nullptr, "NGU GLSL pass texture output");
            pl_gpu_finish(context.gpu);

            std::vector<uint8_t> actual(outputBytes);
            pl_tex_transfer_params transfer = {};
            transfer.tex = result.tex;
            transfer.ptr = actual.data();
            transfer.row_pitch = static_cast<size_t>(width) * 8;
            Require(pl_tex_download(context.gpu, &transfer), "download NGU GLSL output");
            bitDifferences[outputIndex] = CountLowHalfDifferences(actual, expected[outputIndex]);

            std::ofstream actualFile(
                runPrefix + std::to_string(outputIndex) + ".bin",
                std::ios::binary);
            Require(actualFile.good(), "write actual output");
            actualFile.write(reinterpret_cast<const char*>(actual.data()), static_cast<std::streamsize>(actual.size()));

            pl_mpv_user_shader_destroy(&hook);
        }

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        WriteSummary(summaryPath, shaderPath, inputPath, expectedPaths, bitDifferences, elapsedMs);
        for (int index = 0; index < 4; ++index)
            std::printf("rt%d different_half_components=%d\n", index, bitDifferences[index]);

        for (pl_tex texture : context.ownedTextures)
            pl_tex_destroy(context.gpu, &texture);
        pl_tex_destroy(context.gpu, &inputTexture);
        pl_dispatch_destroy(&context.dispatch);
        pl_d3d11_destroy(&d3d);
        pl_log_destroy(&log);
        return std::all_of(bitDifferences.begin(), bitDifferences.end(), [](int value) { return value == 0; }) ? 0 : 1;
    } catch (const std::exception& error) {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
