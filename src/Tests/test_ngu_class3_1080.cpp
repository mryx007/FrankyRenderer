#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path,
                               size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open capture");
    Require(size_t(file.tellg()) == expected, "capture size");
    file.seekg(0);
    std::vector<uint8_t> result(expected);
    Require(bool(file.read(
                reinterpret_cast<char*>(result.data()),
                static_cast<std::streamsize>(expected))),
            "read capture");
    return result;
}

std::filesystem::path FindCapture(const std::filesystem::path& root,
                                  int draw,
                                  const char* shader,
                                  const char* geometry,
                                  size_t expected)
{
    char drawPrefix[32] = {};
    std::snprintf(drawPrefix,
                  sizeof(drawPrefix),
                  "draw_%05d_",
                  draw);
    std::filesystem::path result;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind(drawPrefix, 0) != 0 ||
            name.find(shader) == std::string::npos ||
            name.find(geometry) == std::string::npos ||
            name.find("_rt0_") == std::string::npos ||
            entry.file_size() != expected)
            continue;
        result = entry.path();
    }
    Require(!result.empty(), "exact stage capture");
    return result;
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      UINT width,
                                      UINT height,
                                      DXGI_FORMAT format,
                                      UINT bind,
                                      const void* data,
                                      UINT pitch)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    initial.SysMemPitch = pitch;
    ComPtr<ID3D11Texture2D> result;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, data ? &initial : nullptr, &result)),
            "create texture");
    return result;
}

size_t PixelBytes(DXGI_FORMAT format)
{
    return format == DXGI_FORMAT_R16_UNORM ? 2
           : format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4
           : format == DXGI_FORMAT_R16G16_UNORM ||
                     format == DXGI_FORMAT_R16G16_FLOAT
               ? 4
               : 8;
}

std::vector<uint8_t> Download(ID3D11Device* device,
                              ID3D11DeviceContext* context,
                              ID3D11Texture2D* source)
{
    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, nullptr, &staging)),
            "create staging");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(
                staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging");
    const UINT pixelBytes = UINT(PixelBytes(desc.Format));
    std::vector<uint8_t> result(
        size_t(desc.Width) * desc.Height * pixelBytes);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(result.data() + size_t(y) * desc.Width * pixelBytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        size_t(y) * mapped.RowPitch,
                    size_t(desc.Width) * pixelBytes);
    context->Unmap(staging.Get(), 0);
    return result;
}

int CompareChannels(const std::vector<uint8_t>& actual,
                    const std::vector<uint8_t>& expected,
                    int& maximum,
                    int& overOne)
{
    Require(actual.size() == expected.size(), "comparison size");
    maximum = 0;
    overOne = 0;
    int different = 0;
    for (size_t i = 0; i < actual.size(); i += 2)
    {
        const uint16_t a =
            uint16_t(actual[i]) | (uint16_t(actual[i + 1]) << 8);
        const uint16_t e =
            uint16_t(expected[i]) | (uint16_t(expected[i + 1]) << 8);
        const int delta = std::abs(int(a) - int(e));
        different += delta != 0;
        overOne += delta > 1;
        maximum = std::max(maximum, delta);
    }
    return different;
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

int CompareR16Expanded(const std::vector<uint8_t>& actual,
                       const std::vector<uint8_t>& expected,
                       int& maximum,
                       int& overOne)
{
    Require(expected.size() == actual.size() * 4,
            "expanded comparison size");
    maximum = 0;
    overOne = 0;
    int different = 0;
    for (size_t i = 0; i < actual.size() / 2; ++i)
    {
        const uint16_t a =
            uint16_t(actual[i * 2]) | (uint16_t(actual[i * 2 + 1]) << 8);
        const uint16_t e =
            uint16_t(expected[i * 8]) |
            (uint16_t(expected[i * 8 + 1]) << 8);
        const int delta = std::abs(int(a) - int(e));
        different += delta != 0;
        overOne += delta > 1;
        maximum = std::max(maximum, delta);
    }
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc >= 3 && argc <= 5,
                "usage: test_ngu_class3_1080 <capture-root> <packed-raw> "
                "[p210|p216] [final-capture]");
        const bool p216 = argc >= 4 && std::strcmp(argv[3], "p216") == 0;
        Require(argc == 3 || p216 ||
                    std::strcmp(argv[3], "p210") == 0,
                "unknown input format");
        const std::filesystem::path root(argv[1]);
        const auto raw = ReadBytes(
            argv[2], size_t(1920) * 1080 * 2 + size_t(960) * 1080 * 4);
        const size_t yBytes = size_t(1920) * 1080 * 2;
        auto d3d = pl_d3d11_create(nullptr, &pl_d3d11_default_params);
        Require(d3d != nullptr, "D3D11");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        auto yTexture = CreateTexture(
            device, 1920, 1080, DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, raw.data(), 1920 * 2);
        auto uvTexture = CreateTexture(
            device, 960, 1080, DXGI_FORMAT_R16G16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, raw.data() + yBytes, 960 * 4);
        auto output = CreateTexture(
            device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
            nullptr, 0);
        ComPtr<ID3D11ShaderResourceView> yView;
        ComPtr<ID3D11ShaderResourceView> uvView;
        ComPtr<ID3D11RenderTargetView> outputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    yTexture.Get(), nullptr, &yView)) &&
                    SUCCEEDED(device->CreateShaderResourceView(
                        uvTexture.Get(), nullptr, &uvView)) &&
                    SUCCEEDED(device->CreateRenderTargetView(
                        output.Get(), nullptr, &outputView)),
                "create views");

        NguScaler core;
        const bool prepared =
            core.PrepareNativeChromaCoreClass3_1080(d3d->gpu);
        const std::string prepareError = core.LastError();
        Require(prepared, prepareError.c_str());
        const bool ran = core.RunNativeChromaCoreClass3_1080(
            uvView.Get(), yView.Get(), outputView.Get());
        const std::string runError = core.LastError();
        Require(ran, runError.c_str());

        const struct
        {
            int stage;
            int draw;
            const char* shader;
            const char* geometry;
            UINT width;
            UINT height;
            DXGI_FORMAT format;
        } stages[] = {
            {0, 6, "PS_260_cdfc6b55", "960x1080", 960, 1080,
             DXGI_FORMAT_R16_UNORM},
            {6, 90, "PS_1516_11f763dd", "1920x2160", 1920, 2160,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {7, 91, "PS_396_e3199f71", "1920x1080", 1920, 1080,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {8, 92, "PS_520_4a9505a4", "960x1080", 960, 1080,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {9, 93, "PS_284_e8e2a8eb", "960x1080", 960, 1080,
             DXGI_FORMAT_R16G16_FLOAT},
            {10, 94, "PS_4412_c39b49cf", "1920x1080", 1920, 1080,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {11, 97, "PS_4576_b7830945", "1920x1080", 1920, 1080,
             DXGI_FORMAT_R16G16B16A16_UNORM},
        };
        for (const auto& stage : stages)
        {
            const int captureDraw =
                p216 ? (stage.stage == 0   ? 6
                        : stage.stage == 6 ? 139
                        : stage.stage == 7 ? 140
                        : stage.stage == 8 ? 141
                        : stage.stage == 9 ? 142
                        : stage.stage == 10 ? 143
                                           : 146)
                     : stage.draw;
            const size_t capturePixelBytes =
                stage.stage == 0 ? 8 : PixelBytes(stage.format);
            const size_t expectedBytes =
                size_t(stage.width) * stage.height *
                capturePixelBytes;
            const auto expected = ReadBytes(
                FindCapture(root,
                            captureDraw,
                            stage.shader,
                            stage.geometry,
                            expectedBytes),
                expectedBytes);
            auto actualTexture = CreateTexture(
                device, stage.width, stage.height, stage.format,
                D3D11_BIND_RENDER_TARGET |
                    D3D11_BIND_SHADER_RESOURCE,
                nullptr, 0);
            ComPtr<ID3D11RenderTargetView> actualView;
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        actualTexture.Get(), nullptr, &actualView)),
                    "stage RTV");
            if (stage.stage == 11)
            {
                actualTexture = output;
            }
            else
            {
                Require(core.CopyNativeChromaCoreClass3Stage1080(
                            stage.stage, actualView.Get()),
                        core.LastError());
            }
            const auto actual = Download(
                device, context.Get(), actualTexture.Get());
            int maximum = 0;
            int overOne = 0;
            const int different =
                stage.stage == 0
                    ? CompareR16Expanded(
                          actual, expected, maximum, overOne)
                    : CompareChannels(
                          actual, expected, maximum, overOne);
            std::printf(
                "1080 class3 stage=%d different=%d maximum=%d "
                "diff_gt_1_channels=%d\n",
                stage.stage,
                different,
                maximum,
                overOne);
            if (maximum > 1 && actual.size() >= 8 && expected.size() >= 8)
            {
                size_t firstMismatch = 0;
                size_t maximumMismatch = 0;
                int maximumDelta = 0;
                for (size_t i = 0; i < actual.size(); i += 2)
                {
                    const uint16_t a =
                        uint16_t(actual[i]) |
                        (uint16_t(actual[i + 1]) << 8);
                    const uint16_t e =
                        uint16_t(expected[i]) |
                        (uint16_t(expected[i + 1]) << 8);
                    const int delta = std::abs(int(a) - int(e));
                    if (delta > maximumDelta)
                    {
                        maximumDelta = delta;
                        maximumMismatch = i;
                    }
                    if (a != e)
                    {
                        firstMismatch = i;
                        if (maximumDelta == delta)
                            firstMismatch = i;
                    }
                }
                for (size_t i = 0; i < actual.size(); i += 2)
                {
                    const uint16_t a =
                        uint16_t(actual[i]) |
                        (uint16_t(actual[i + 1]) << 8);
                    const uint16_t e =
                        uint16_t(expected[i]) |
                        (uint16_t(expected[i + 1]) << 8);
                    if (std::abs(int(a) - int(e)) == maximumDelta)
                    {
                        firstMismatch = i;
                        maximumMismatch = i;
                        break;
                    }
                }
                const uint16_t a =
                    uint16_t(actual[0]) | (uint16_t(actual[1]) << 8);
                const uint16_t e =
                    uint16_t(expected[0]) | (uint16_t(expected[1]) << 8);
                std::printf("first actual=%u expected=%u\n",
                            unsigned(a),
                            unsigned(e));
                std::printf("first mismatch xy=%zu,%zu channel=%zu\n",
                            (firstMismatch / 8) % stage.width,
                            (firstMismatch / 8) / stage.width,
                            (firstMismatch / 2) % 4);
                std::printf("maximum mismatch xy=%zu,%zu channel=%zu "
                            "actual=%u expected=%u delta=%d\n",
                            (maximumMismatch / 8) % stage.width,
                            (maximumMismatch / 8) / stage.width,
                            (maximumMismatch / 2) % 4,
                            unsigned(uint16_t(actual[maximumMismatch]) |
                                     (uint16_t(actual[maximumMismatch + 1])
                                      << 8)),
                            unsigned(uint16_t(expected[maximumMismatch]) |
                                     (uint16_t(expected[maximumMismatch + 1])
                                      << 8)),
                            maximumDelta);
                const size_t mismatchX =
                    (firstMismatch / 8) % stage.width;
                const size_t mismatchY =
                    (firstMismatch / 8) / stage.width;
                for (size_t x = mismatchX > 4 ? mismatchX - 4 : 0;
                     x <= std::min<size_t>(mismatchX + 4,
                                           size_t(stage.width) - 1);
                     ++x)
                {
                    const size_t offset =
                        (mismatchY * stage.width + x) * 8;
                    std::printf("  x=%zu actual=", x);
                    for (size_t c = 0; c < 4; ++c)
                    {
                        const uint16_t value =
                            uint16_t(actual[offset + c * 2]) |
                            (uint16_t(actual[offset + c * 2 + 1]) << 8);
                        std::printf("%u%s",
                                    unsigned(value),
                                    c == 3 ? "" : ",");
                    }
                    std::printf(" expected=");
                    for (size_t c = 0; c < 4; ++c)
                    {
                        const uint16_t value =
                            uint16_t(expected[offset + c * 2]) |
                            (uint16_t(expected[offset + c * 2 + 1]) << 8);
                        std::printf("%u%s",
                                    unsigned(value),
                                    c == 3 ? "\n" : ",");
                    }
                }
            }
            Require(maximum <= 1 && overOne == 0,
                    "1080 class-3 stage exceeds one code");
        }

        if (argc == 5)
        {
            auto finalTexture = CreateTexture(
                device,
                3840,
                2160,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                nullptr,
                0);
            ComPtr<ID3D11RenderTargetView> finalView;
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        finalTexture.Get(), nullptr, &finalView)),
                    "final RTV");
            Require(core.PrepareNativeNguClass3_1080(d3d->gpu),
                    core.LastError());
            Require(core.RunNativeNguClass3_1080(
                        uvView.Get(), yView.Get(), finalView.Get()),
                    core.LastError());
            const auto actualFinal =
                Download(device, context.Get(), finalTexture.Get());
            const auto expectedFinal = ReadBytes(
                argv[4], size_t(3840) * 2160 * 4);
            int maximumFinal = 0;
            int overOneFinal = 0;
            const int differentFinal = CompareRgb(
                actualFinal, expectedFinal, maximumFinal, overOneFinal);
            std::printf(
                "1080 class3 combined different=%d maximum=%d "
                "diff_gt_1_pixels=%d\n",
                differentFinal,
                maximumFinal,
                overOneFinal);
            Require(maximumFinal <= 1 && overOneFinal == 0,
                    "1080 class-3 combined acceptance");
        }
        std::printf(argc == 5
                        ? "PASS_PRODUCT 1080 class-3 core + combined PS_1352\n"
                        : "PASS_PRODUCT 1080 class-3 core PS_4576\n");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
