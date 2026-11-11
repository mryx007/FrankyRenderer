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
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open sample");
    const auto size = file.tellg();
    Require(size > 0, "sample size");
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()))),
            "read sample");
    return bytes;
}

ComPtr<ID3D11Texture2D> CreateDecoderTexture(ID3D11Device* device,
                                             const std::vector<uint8_t>& bytes,
                                             UINT width,
                                             UINT height,
                                             DXGI_FORMAT format)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    initial.SysMemPitch = width * sizeof(uint16_t);
    ComPtr<ID3D11Texture2D> texture;
    const HRESULT hr = device->CreateTexture2D(&desc, &initial, &texture);
    if (FAILED(hr))
    {
        UINT support = 0;
        device->CheckFormatSupport(format, &support);
        std::printf("decoder texture format=%u support=0x%08X hr=0x%08X\n",
                    unsigned(format), support, unsigned(hr));
    }
    Require(SUCCEEDED(hr), "create decoder texture");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateOutput(ID3D11Device* device,
                                     UINT width,
                                     UINT height,
                                     DXGI_FORMAT format,
                                     ComPtr<ID3D11RenderTargetView>& view)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)),
            "create source output");
    Require(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)),
            "create source output RTV");
    return texture;
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
    Require(SUCCEEDED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)),
            "create staging");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging");
    std::vector<uint8_t> bytes(size_t(desc.Width) * desc.Height * bytesPerPixel);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(bytes.data() + size_t(y) * desc.Width * bytesPerPixel,
               static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
               size_t(desc.Width) * bytesPerPixel);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

int CompareLuma(const std::vector<uint8_t>& actual,
                const std::vector<uint8_t>& source,
                UINT sourceWidth,
                UINT width,
                UINT height)
{
    int different = 0;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
        {
            const size_t sourceOffset = (size_t(y) * sourceWidth + x) * 2;
            const size_t outputOffset = (size_t(y) * width + x) * 2;
            different += memcmp(source.data() + sourceOffset,
                                actual.data() + outputOffset,
                                2) != 0;
        }
    return different;
}

int CompareChroma(const std::vector<uint8_t>& actual,
                  const std::vector<uint8_t>& source,
                  UINT sourceWidth,
                  UINT width,
                  UINT height)
{
    int different = 0;
    bool reported = false;
    const UINT chromaWidth = width / 2;
    const UINT chromaHeight = height / 2;
    const size_t sourceBase = size_t(sourceWidth) * height * 2;
    for (UINT y = 0; y < chromaHeight; ++y)
        for (UINT x = 0; x < chromaWidth; ++x)
        {
            const size_t sourceOffset =
                sourceBase + (size_t(y) * sourceWidth + x * 2) * 2;
            const size_t outputOffset = (size_t(y) * chromaWidth + x) * 8;
            const bool uvDifferent =
                memcmp(source.data() + sourceOffset,
                       actual.data() + outputOffset,
                       4) != 0;
            different += uvDifferent;
            uint32_t upper = 0;
            memcpy(&upper, actual.data() + outputOffset + 4, sizeof(upper));
            different += upper != 0;
            if ((uvDifferent || upper) && !reported)
            {
                reported = true;
                std::printf(
                    "chroma mismatch x=%u y=%u source=%02X%02X%02X%02X "
                    "actual=%02X%02X%02X%02X%02X%02X%02X%02X\n",
                    x, y,
                    source[sourceOffset], source[sourceOffset + 1],
                    source[sourceOffset + 2], source[sourceOffset + 3],
                    actual[outputOffset], actual[outputOffset + 1],
                    actual[outputOffset + 2], actual[outputOffset + 3],
                    actual[outputOffset + 4], actual[outputOffset + 5],
                    actual[outputOffset + 6], actual[outputOffset + 7]);
            }
        }
    return different;
}

void TestPacked(NguScaler& ngu,
                ID3D11Device* device,
                ID3D11DeviceContext* context,
                const std::filesystem::path& path,
                DXGI_FORMAT format,
                UINT paddedWidth,
                UINT width,
                UINT height,
                const char* name)
{
    const auto source = ReadBytes(path);
    Require(source.size() == size_t(paddedWidth) * height * 3,
            "decoder sample size");
    auto texture = CreateDecoderTexture(device, source, paddedWidth, height, format);

    ComPtr<ID3D11RenderTargetView> lumaView;
    auto luma = CreateOutput(
        device, width, height, DXGI_FORMAT_R16_UNORM, lumaView);
    ComPtr<ID3D11RenderTargetView> chromaView;
    auto chroma = CreateOutput(
        device, width / 2, height / 2, DXGI_FORMAT_R16G16B16A16_UNORM, chromaView);

    Require(ngu.RunNativeDecoderSourceCopy(texture.Get(),
                                           0,
                                           int(width),
                                           int(height),
                                           lumaView.Get(),
                                           chromaView.Get()),
            ngu.LastError());
    const auto lumaBytes = Download(device, context, luma.Get(), 2);
    const auto chromaBytes = Download(device, context, chroma.Get(), 8);
    const int lumaDifferent =
        CompareLuma(lumaBytes, source, paddedWidth, width, height);
    const int chromaDifferent =
        CompareChroma(chromaBytes, source, paddedWidth, width, height);
    std::printf("%s luma different=%d chroma different=%d\n",
                name, lumaDifferent, chromaDifferent);
    Require(lumaDifferent == 0 && chromaDifferent == 0,
            "decoder source import comparison");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: test_ngu_source_import <p010> <p016>");
        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);

        NguScaler ngu;
        Require(ngu.PrepareNativeDecoderSource(d3d->gpu), ngu.LastError());
        TestPacked(ngu,
                   d3d->device,
                   context.Get(),
                   argv[1],
                   DXGI_FORMAT_P010,
                   2048,
                   1280,
                   720,
                   "P010");
        TestPacked(ngu,
                   d3d->device,
                   context.Get(),
                   argv[2],
                   DXGI_FORMAT_P010,
                   512,
                   320,
                   180,
                   "P016-layout");
        pl_gpu_finish(d3d->gpu);
        std::puts("PASS_PRODUCT native decoder source import");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
