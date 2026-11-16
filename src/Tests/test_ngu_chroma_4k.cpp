#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
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
        Require(size == expected, "binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(size);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))), "read binary");
    return bytes;
}

std::filesystem::path FindSurface(const std::filesystem::path& root, int draw, int index)
{
    char prefix[32] = {};
    std::snprintf(prefix, sizeof(prefix), "draw_%04d_", draw);
    const std::string suffix = "_rt" + std::to_string(index) + "_";
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && name.find(suffix) != std::string::npos)
            return entry.path();
    }
    return {};
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
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture)), "create texture");
    return texture;
}

ComPtr<ID3D11ShaderResourceView> CreateView(ID3D11Device* device, ID3D11Texture2D* texture)
{
    ComPtr<ID3D11ShaderResourceView> view;
    Require(SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view)), "create SRV");
    return view;
}

ComPtr<ID3D11RenderTargetView> CreateRenderTarget(ID3D11Device* device, ID3D11Texture2D* texture)
{
    ComPtr<ID3D11RenderTargetView> view;
    Require(SUCCEEDED(device->CreateRenderTargetView(texture, nullptr, &view)), "create RTV");
    return view;
}

std::vector<uint8_t> Download(ID3D11Device* device,
                              ID3D11DeviceContext* context,
                              ID3D11Texture2D* texture,
                              UINT bytesPerPixel)
{
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)), "create staging");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map staging");
    const size_t rowBytes = size_t(desc.Width) * bytesPerPixel;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

struct Difference
{
    size_t different = 0;
    size_t overOne = 0;
    int maximum = 0;
};

Difference Compare(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, UINT bytesPerPixel)
{
    Require(actual.size() == expected.size(), "comparison size");
    Difference result;
    const UINT channelBytes = bytesPerPixel == 2 || bytesPerPixel == 4 || bytesPerPixel == 8 ? 2 : 1;
    const size_t channels = bytesPerPixel == 2 ? 1 : bytesPerPixel == 4 ? 2 : 4;
    const size_t pixels = actual.size() / bytesPerPixel;
    for (size_t pixel = 0; pixel < pixels; ++pixel)
    {
        bool pixelOverOne = false;
        for (size_t channel = 0; channel < channels; ++channel)
        {
            const size_t offset = pixel * bytesPerPixel + channel * channelBytes;
            int a = actual[offset];
            int e = expected[offset];
            if (channelBytes == 2)
            {
                a |= int(actual[offset + 1]) << 8;
                e |= int(expected[offset + 1]) << 8;
            }
            const int delta = std::abs(a - e);
            result.different += delta != 0;
            result.maximum = std::max(result.maximum, delta);
            pixelOverOne = pixelOverOne || delta > 1;
        }
        result.overOne += pixelOverOne;
    }
    return result;
}

struct Stage
{
    const char* name;
    int draw;
    int index;
    UINT width;
    UINT height;
    DXGI_FORMAT format;
    UINT bytesPerPixel;
};

bool VerifyStage(NguScaler& ngu,
                 const std::filesystem::path& root,
                 int stageIndex,
                 const Stage& stage)
{
    const auto expectedPath = FindSurface(root, stage.draw, stage.index);
    if (expectedPath.empty())
        return false;
    char dumpName[160] = {};
    std::snprintf(dumpName, sizeof(dumpName), "build\\ngu4k_%s_%d_%d.bin", stage.name, stage.draw, stage.index);
    Require(ngu.DumpNativeChroma4KStage(stageIndex, stage.index, dumpName), ngu.LastError());
    const auto actual = ReadBytes(dumpName);
    const auto expected = ReadBytes(expectedPath);
    const auto difference = Compare(actual, expected, stage.bytesPerPixel);
    std::printf("%-18s different=%zu maximum=%d diff_gt_1_pixels=%zu\n",
                stage.name,
                difference.different,
                difference.maximum,
                difference.overOne);
    if (difference.maximum != 0)
    {
        std::printf("  actual  =");
        for (size_t index = 0; index < std::min<size_t>(16, actual.size()); ++index)
            std::printf(" %02X", actual[index]);
        std::printf("\n  expected=");
        for (size_t index = 0; index < std::min<size_t>(16, expected.size()); ++index)
            std::printf(" %02X", expected[index]);
        std::printf("\n");
    }
    return difference.maximum == 0;
}

std::filesystem::path FindInput(const std::filesystem::path& root, int draw)
{
    char prefix[40] = {};
    std::snprintf(prefix, sizeof(prefix), "input_%04d_PS_260_cdfc6b55_s0_", draw);
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.rfind(prefix, 0) == 0)
            return entry.path();
    }
    return {};
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "usage: test_ngu_chroma_4k <capture-root>");
        const std::filesystem::path captureRoot(argv[1]);
        const auto sourceLuma = ReadBytes(LR"(C:\tmp\ngu\native2160_frame0_yuv420p_20261116.yuv)",
                                          size_t(3840) * 2160 * 3 / 2);

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);

        auto yTexture = CreateTexture(d3d->device,
                                      3840,
                                      2160,
                                      DXGI_FORMAT_R8_UNORM,
                                      D3D11_BIND_SHADER_RESOURCE,
                                      sourceLuma.data(),
                                      3840);
        auto yView = CreateView(d3d->device, yTexture.Get());

        struct QualityCase
        {
            NguQuality quality;
            const char* name;
            const char* folder;
            int inputDraw;
            int finalDraw;
            std::vector<Stage> stages;
        };
        const std::vector<QualityCase> cases = {
            {NguQuality::Low,
             "low",
             "native2160_chroma_20261116",
             6,
             21,
             {{"split-cb", 6, 0, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"split-cr", 6, 1, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-x", 7, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-y", 8, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cb", 12, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cb", 13, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-x", 14, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-y", 15, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cr", 19, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cr", 20, 0, 3840, 2160, DXGI_FORMAT_R16G16_UNORM, 4}}},
            {NguQuality::Medium,
             "med",
             "native2160_chroma_med_20261116",
             6,
             0,
             {{"split-cb", 6, 0, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"split-cr", 6, 1, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-x", 7, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-y", 8, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cb", 14, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cb", 15, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-x", 16, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-y", 17, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cr", 23, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cr", 24, 0, 3840, 2160, DXGI_FORMAT_R16G16_UNORM, 4}}},
            {NguQuality::High,
             "high",
             "native2160_chroma_high_complete_20261116",
             6,
             80,
             {{"split-cb", 39, 0, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"split-cr", 39, 1, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-x", 40, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-y", 41, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cb", 58, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cb", 59, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-x", 60, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-y", 61, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cr", 78, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cr", 79, 0, 3840, 2160, DXGI_FORMAT_R16G16_UNORM, 4}}},
            {NguQuality::VeryHigh,
             "veryhigh",
             "native2160_chroma_veryhigh_complete12_20261116",
             6,
             103,
             {{"split-cb", 36, 0, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"split-cr", 36, 1, 1920, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-x", 37, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cb-y", 38, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cb", 68, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cb", 69, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-x", 70, 0, 3840, 1080, DXGI_FORMAT_R16_UNORM, 2},
              {"scale-cr-y", 71, 0, 3840, 2160, DXGI_FORMAT_R16_UNORM, 2},
              {"dense-cr", 101, 0, 1922, 1082, DXGI_FORMAT_R16G16B16A16_FLOAT, 8},
              {"combine-cr", 102, 0, 3840, 2160, DXGI_FORMAT_R16G16_UNORM, 4}}},
        };

        bool passed = true;
        for (const auto& item : cases)
        {
            const auto qualityRoot = captureRoot / item.folder;
            auto inputPath = FindInput(qualityRoot, item.inputDraw);
            if (inputPath.empty())
                inputPath = FindInput(captureRoot / "native2160_chroma_20261116", item.inputDraw);
            Require(!inputPath.empty(), "4K chroma source capture");
            const auto packed = ReadBytes(inputPath, size_t(1920) * 1080 * 4);
            std::vector<uint8_t> packedRg16(size_t(1920) * 1080 * 4);
            for (size_t pixel = 0; pixel < size_t(1920) * 1080; ++pixel)
            {
                const uint8_t cb = packed[pixel * 4 + 2];
                const uint8_t cr = packed[pixel * 4 + 1];
                packedRg16[pixel * 4] = cb;
                packedRg16[pixel * 4 + 1] = cb;
                packedRg16[pixel * 4 + 2] = cr;
                packedRg16[pixel * 4 + 3] = cr;
            }
            auto packedTexture = CreateTexture(d3d->device,
                                               1920,
                                               1080,
                                               DXGI_FORMAT_R16G16_UNORM,
                                               D3D11_BIND_SHADER_RESOURCE,
                                               packedRg16.data(),
                                               1920 * 4);
            auto packedView = CreateView(d3d->device, packedTexture.Get());
            auto output = CreateTexture(d3d->device,
                                        3840,
                                        2160,
                                        DXGI_FORMAT_R16G16B16A16_UNORM,
                                        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                                        nullptr,
                                        0);
            auto outputView = CreateRenderTarget(d3d->device, output.Get());

            NguScaler ngu;
            Require(ngu.PrepareNativeChroma4K(d3d->gpu, item.quality), ngu.LastError());
            Require(ngu.RunNativeChroma4K(packedView.Get(), yView.Get(), item.quality, outputView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);

            size_t verified = 0;
            int stageIndex = 0;
            for (const auto& stage : item.stages)
            {
                if (VerifyStage(ngu, qualityRoot, stageIndex++, stage))
                    ++verified;
            }
            if (item.finalDraw)
            {
                const auto expectedPath = FindSurface(qualityRoot, item.finalDraw, 0);
                Require(!expectedPath.empty(), "4K final chroma surface");
                const auto actual = Download(
                    d3d->device, context.Get(), output.Get(), 8);
                const auto expected = ReadBytes(expectedPath);
                const auto difference = Compare(actual, expected, 8);
                std::printf("%-18s different=%zu maximum=%d diff_gt_1_pixels=%zu\n",
                            "luma-chroma",
                            difference.different,
                            difference.maximum,
                            difference.overOne);
                Require(difference.maximum == 0, "4K final chroma surface differs");
                ++verified;
            }
            std::printf("%s: %zu surfaces exact\n", item.name, verified);
            Require(verified == item.stages.size() + (item.finalDraw ? 1 : 0),
                    "4K chroma surface matrix differs");
        }
        Require(passed, "4K chroma regression");
        std::printf("PASS_PRODUCT NGU 4K chroma low/med/high/veryHigh\n");
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
