#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT Width = 640;
constexpr UINT Height = 360;
constexpr UINT Components = 4;
constexpr size_t FeatureBytes = size_t(Width) * Height * Components * sizeof(uint16_t);

void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path FindFile(const std::filesystem::path& root, const std::wstring& prefix)
{
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.is_regular_file() && entry.path().filename().wstring().starts_with(prefix))
            return entry.path();
    throw std::runtime_error("missing capture prefix: " + std::string(prefix.begin(), prefix.end()));
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path, size_t bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), path.string() + ": open failed");
    Require(size_t(file.tellg()) == bytes, path.string() + ": unexpected size");
    file.seekg(0);
    std::vector<uint8_t> data(bytes);
    Require(bool(file.read(reinterpret_cast<char*>(data.data()), std::streamsize(bytes))),
            path.string() + ": read failed");
    return data;
}

ComPtr<ID3D11Texture2D>
CreateTexture(ID3D11Device* device, UINT width, UINT height, const void* data = nullptr)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    initial.SysMemPitch = width * Components * sizeof(uint16_t);
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture)),
            "CreateTexture2D failed");
    return texture;
}

std::vector<uint8_t>
Download(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)),
            "CreateTexture2D staging failed");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "Map staging failed");
    const size_t rowBytes = size_t(desc.Width) * Components * sizeof(uint16_t);
    std::vector<uint8_t> data(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(data.data() + y * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return data;
}

struct Difference
{
    int components = 0;
    uint16_t maximum = 0;
};

Difference Compare(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
{
    Require(actual.size() == expected.size(), "comparison size mismatch");
    Difference result;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left =
            uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        if (left == right)
            continue;
        ++result.components;
        result.maximum =
            std::max(result.maximum, uint16_t(std::abs(int(left) - int(right))));
    }
    return result;
}

struct StageRunner
{
    NguScaler& ngu;
    ID3D11Device* device;
    ID3D11DeviceContext* context;
    pl_gpu gpu;
    const std::filesystem::path& root;
    int failures = 0;
    uint16_t maximum = 0;

    struct Inputs
    {
        std::array<std::vector<uint8_t>, 4> bytes;
        std::array<ComPtr<ID3D11Texture2D>, 4> textures;
        std::array<ComPtr<ID3D11ShaderResourceView>, 4> views;
        std::array<ID3D11ShaderResourceView*, 4> raw = {};
        int count = 0;
    };

    Inputs LoadInputs(int draw, const wchar_t* shader, std::initializer_list<int> stages)
    {
        Inputs result;
        for (int stage : stages)
        {
            wchar_t prefix[128];
            swprintf_s(prefix, L"input_%04d_%s_s%d_", draw, shader, stage);
            result.bytes[result.count] =
                ReadBytes(FindFile(root, prefix), FeatureBytes);
            result.textures[result.count] =
                CreateTexture(device, Width, Height, result.bytes[result.count].data());
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        result.textures[result.count].Get(), nullptr, &result.views[result.count])),
                    "input SRV");
            result.raw[result.count] = result.views[result.count].Get();
            ++result.count;
        }
        return result;
    }

    void CheckOutput(int draw,
                     const wchar_t* shader,
                     ID3D11Texture2D* texture,
                     int output,
                     UINT width,
                     UINT height)
    {
        wchar_t prefix[128];
        swprintf_s(prefix, L"draw_%04d_%s_rt%d_%ux%u_", draw, shader, output, width, height);
        const auto expected = ReadBytes(
            FindFile(root, prefix), size_t(width) * height * Components * sizeof(uint16_t));
        pl_gpu_finish(gpu);
        const auto actual = Download(device, context, texture);
        const auto difference = Compare(actual, expected);
        failures += difference.components;
        maximum = std::max(maximum, difference.maximum);
        std::printf("%ls draw %d rt%d: different=%d maximum=%u\n",
                    shader,
                    draw,
                    output,
                    difference.components,
                    unsigned(difference.maximum));
    }

    void Layer(int draw, NguQuality quality, int layer, int inputCount, int outputCount)
    {
        const wchar_t* shader = quality == NguQuality::Low ? L"PS_4280_d591580d"
                                                           : L"PS_4280_d591580d";
        auto inputs = inputCount == 2 ? LoadInputs(draw, shader, {0, 2})
                                      : LoadInputs(draw, shader, {0, 2, 3});
        std::array<ComPtr<ID3D11Texture2D>, 2> textures;
        std::array<ComPtr<ID3D11RenderTargetView>, 2> views;
        std::array<ID3D11RenderTargetView*, 2> raw = {};
        for (int output = 0; output < outputCount; ++output)
        {
            textures[output] = CreateTexture(device, Width, Height);
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        textures[output].Get(), nullptr, &views[output])),
                    "layer RTV");
            raw[output] = views[output].Get();
        }
        Require(ngu.RunNativeChromaQualityLayerMultiForTest(
                    quality, inputs.raw.data(), inputCount, layer, raw.data(), outputCount),
                ngu.LastError());
        for (int output = 0; output < outputCount; ++output)
            CheckOutput(draw, shader, textures[output].Get(), output, Width, Height);
    }

    void Mixer(int draw)
    {
        auto inputs = LoadInputs(draw, L"PS_6140_e8f1d399", {0, 2, 3});
        std::array<ComPtr<ID3D11Texture2D>, 2> textures;
        std::array<ComPtr<ID3D11RenderTargetView>, 2> views;
        std::array<ID3D11RenderTargetView*, 2> raw = {};
        for (int output = 0; output < 2; ++output)
        {
            textures[output] = CreateTexture(device, Width, Height);
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        textures[output].Get(), nullptr, &views[output])),
                    "mixer RTV");
            raw[output] = views[output].Get();
        }
        Require(ngu.RunNativeChromaMixerForTest(
                    NguQuality::Medium, inputs.raw.data(), raw.data(), 2),
                ngu.LastError());
        for (int output = 0; output < 2; ++output)
            CheckOutput(draw, L"PS_6140_e8f1d399", textures[output].Get(), output, Width, Height);
    }

    void Dense(int draw,
               NguQuality quality,
               int inputCount,
               int expectedDraw,
               const wchar_t* expectedShader)
    {
        auto inputs = inputCount == 2 ? LoadInputs(draw, expectedShader, {0, 2})
                                      : LoadInputs(draw, expectedShader, {0, 2, 3});
        auto texture = CreateTexture(device, 642, 362);
        ComPtr<ID3D11RenderTargetView> view;
        Require(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)),
                "dense RTV");
        Require(ngu.RunNativeChromaQualityDenseForTest(
                    quality, inputs.raw.data(), inputCount, view.Get()),
                ngu.LastError());
        wchar_t prefix[128];
        swprintf_s(prefix, L"draw_%04d_%s_rt0_", expectedDraw, expectedShader);
        const auto expected =
            ReadBytes(FindFile(root, prefix), size_t(642) * 362 * Components * sizeof(uint16_t));
        pl_gpu_finish(gpu);
        const auto actual = Download(device, context, texture.Get());
        const auto difference = Compare(actual, expected);
        failures += difference.components;
        maximum = std::max(maximum, difference.maximum);
        std::printf("%ls dense draw %d: different=%d maximum=%u\n",
                    expectedShader,
                    expectedDraw,
                    difference.components,
                    unsigned(difference.maximum));
    }

};
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: test_ngu_quality_low_med <low-root> <med-root>");
        const std::filesystem::path lowRoot = std::filesystem::path(argv[1]);
        const std::filesystem::path medRoot = std::filesystem::path(argv[2]);

        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Require(d3d != nullptr, "D3D11 device unavailable");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        NguScaler ngu;
        Require(ngu.PrepareNativeChromaNetwork(d3d->gpu), ngu.LastError());

        StageRunner low{ngu, device, context.Get(), d3d->gpu, lowRoot};
        low.Layer(10, NguQuality::Low, 0, 2, 2);
        low.Layer(11, NguQuality::Low, 1, 2, 2);
        low.Dense(12, NguQuality::Low, 2, 12, L"PS_1528_f0e4bb92");

        StageRunner medium{ngu, device, context.Get(), d3d->gpu, medRoot};
        medium.Mixer(10);
        for (int layer = 0; layer < 3; ++layer)
            medium.Layer(11 + layer, NguQuality::Medium, layer, 2, 2);
        medium.Dense(14, NguQuality::Medium, 2, 14, L"PS_1528_f0e4bb92");

        const int failures = low.failures + medium.failures;
        const uint16_t maximum = std::max(low.maximum, medium.maximum);
        std::printf("%s NGU Sharp Low/Medium chroma profiles: failures=%d maximum=%u\n",
                    failures == 0 ? "PASS_PRODUCT" : "FAIL_PRODUCT",
                    failures,
                    unsigned(maximum));
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
