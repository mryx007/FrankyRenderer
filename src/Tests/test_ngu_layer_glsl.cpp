#define NOMINMAX
#include <libplacebo/d3d11.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/log.h>
#include <libplacebo/shaders/custom.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int Width = 1920, Height = 1080;
constexpr size_t Half4Bytes = size_t(Width) * Height * 8;

void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path, size_t expected = 0)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open binary");
    const size_t size = size_t(file.tellg());
    if (expected)
        Require(size == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(size);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))), "read binary");
    return bytes;
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    Require(bool(file), "open text");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

int CountDifferences(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left = uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right = uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        different += left != right;
    }
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: test_ngu_layer_glsl <capture-root> <actual-output>");
        const std::filesystem::path root(argv[1]);
        const std::filesystem::path actualPath(argv[2]);

        pl_log_params logParams = {};
        logParams.log_cb = pl_log_simple;
        logParams.log_priv = stdout;
        logParams.log_level = PL_LOG_WARN;
        pl_log log = pl_log_create(PL_API_VER, &logParams);
        Require(log != nullptr, "libplacebo log");
        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(log, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        std::printf("checkpoint: gpu\n");
        std::fflush(stdout);

        const auto format16 = pl_find_fmt(
            d3d->gpu, PL_FMT_FLOAT, 4, 16, 16,
            static_cast<pl_fmt_caps>(
                PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE | PL_FMT_CAP_HOST_READABLE));
        Require(format16 != nullptr, "RGBA16F format");
        const auto format32 = pl_find_fmt(
            d3d->gpu, PL_FMT_FLOAT, 4, 32, 32, PL_FMT_CAP_SAMPLEABLE);
        Require(format32 != nullptr, "RGBA32F format");
        std::printf("checkpoint: formats\n");
        std::fflush(stdout);

        auto createTexture = [&](int width,
                                 int height,
                                 pl_fmt format,
                                 const void* initial,
                                 bool renderable,
                                 bool readable)
        {
            pl_tex_params params = {};
            params.w = width;
            params.h = height;
            params.format = format;
            params.sampleable = true;
            params.renderable = renderable;
            params.host_readable = readable;
            params.initial_data = initial;
            pl_tex texture = pl_tex_create(d3d->gpu, &params);
            Require(texture != nullptr, "create libplacebo texture");
            return texture;
        };

        const auto input0 = ReadBytes(root / "draw_31706_PS_1412_aad01c2f_rt0_1920x1080_f113.bin", Half4Bytes);
        const auto input1 = ReadBytes(root / "input_31707_PS_3588_80fb311e_s2.bin", Half4Bytes);
        const auto input2 = ReadBytes(root / "input_31707_PS_3588_80fb311e_s3.bin", Half4Bytes);
        const auto constants =
            ReadBytes(root / "const_snapshot_draw_31707_PS_3588_80fb311e.f32", 111 * 16);
        const auto expected =
            ReadBytes(root / "draw_31707_PS_3588_80fb311e_rt0_1920x1080_f113.bin", Half4Bytes);

        pl_tex feature0 = createTexture(Width, Height, format16, input0.data(), false, false);
        pl_tex feature1 = createTexture(Width, Height, format16, input1.data(), false, false);
        pl_tex feature2 = createTexture(Width, Height, format16, input2.data(), false, false);
        pl_tex constantsTexture = createTexture(111, 1, format32, constants.data(), false, false);
        pl_tex output = createTexture(Width, Height, format16, nullptr, true, true);
        std::printf("checkpoint: textures\n");
        std::fflush(stdout);

        auto body = ReadText(std::filesystem::path("src") / "Shaders" / "Ngu" / "luma_1080_layer_body.glsl");
        body = "#define HOOKED_pos (gl_FragCoord.xy / vec2(1920.0, 1080.0))\n" + body;

        struct DescriptorSet
        {
            pl_shader_desc descriptors[4] = {};
        } set;
        const char* names[] = {"Feature0", "Feature1", "Feature2", "Constants"};
        const pl_tex textures[] = {feature0, feature1, feature2, constantsTexture};
        for (int i = 0; i < 4; ++i)
        {
            set.descriptors[i].desc.name = names[i];
            set.descriptors[i].desc.type = PL_DESC_SAMPLED_TEX;
            set.descriptors[i].desc.binding = i;
            set.descriptors[i].binding.object = textures[i];
            set.descriptors[i].binding.address_mode = PL_TEX_ADDRESS_CLAMP;
            set.descriptors[i].binding.sample_mode = PL_TEX_SAMPLE_NEAREST;
        }

        pl_dispatch dispatch = pl_dispatch_create(log, d3d->gpu);
        Require(dispatch != nullptr, "dispatch");
        std::printf("checkpoint: dispatch\n");
        std::fflush(stdout);
        pl_dispatch_reset_frame(dispatch);
        pl_shader shader = pl_dispatch_begin(dispatch);
        Require(shader != nullptr, "shader");
        pl_custom_shader custom = {};
        custom.description = "NGU luma Cubic11 layer oracle";
        custom.header = body.c_str();
        custom.body = "color = ngu_luma_body();";
        custom.input = PL_SHADER_SIG_NONE;
        custom.output = PL_SHADER_SIG_COLOR;
        custom.descriptors = set.descriptors;
        custom.num_descriptors = 4;
        Require(pl_shader_custom(shader, &custom), "custom shader");
        std::printf("checkpoint: custom\n");
        std::fflush(stdout);
        pl_dispatch_params dispatchParams = {};
        dispatchParams.shader = &shader;
        dispatchParams.target = output;
        Require(pl_dispatch_finish(dispatch, &dispatchParams), "dispatch layer");
        std::printf("checkpoint: finish\n");
        std::fflush(stdout);
        pl_gpu_finish(d3d->gpu);
        std::printf("checkpoint: gpu finish\n");
        std::fflush(stdout);

        std::vector<uint8_t> actual(Half4Bytes);
        pl_tex_transfer_params transfer = {};
        transfer.tex = output;
        transfer.ptr = actual.data();
        transfer.row_pitch = size_t(Width) * 8;
        Require(pl_tex_download(d3d->gpu, &transfer), "download");
        {
            std::ofstream file(actualPath, std::ios::binary);
            file.write(reinterpret_cast<const char*>(actual.data()), std::streamsize(actual.size()));
        }
        const int different = CountDifferences(actual, expected);
        std::printf("%s libplacebo NGU layer0: %d different half components\n",
                    different == 0 ? "PASS_PRODUCT" : "FAIL_PRODUCT",
                    different);

        pl_tex_destroy(d3d->gpu, &output);
        pl_tex_destroy(d3d->gpu, &constantsTexture);
        pl_tex_destroy(d3d->gpu, &feature2);
        pl_tex_destroy(d3d->gpu, &feature1);
        pl_tex_destroy(d3d->gpu, &feature0);
        pl_dispatch_destroy(&dispatch);
        pl_d3d11_destroy(&d3d);
        pl_log_destroy(&log);
        return different == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
