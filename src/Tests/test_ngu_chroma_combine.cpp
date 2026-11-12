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

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path,
                               size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open binary");
    Require(size_t(file.tellg()) == expected, "binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),
                           std::streamsize(bytes.size()))),
            "read binary");
    return bytes;
}

std::vector<uint8_t> ExtractL16(const std::vector<uint8_t>& expanded)
{
    std::vector<uint8_t> bytes(expanded.size() / 4);
    for (size_t pixel = 0; pixel < bytes.size() / 2; ++pixel)
        memcpy(bytes.data() + pixel * 2, expanded.data() + pixel * 8, 2);
    return bytes;
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      const std::vector<uint8_t>& bytes,
                                      DXGI_FORMAT format,
                                      UINT width,
                                      UINT height,
                                      UINT bytesPerPixel)
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
    initial.pSysMem = bytes.empty() ? nullptr : bytes.data();
    initial.SysMemPitch = width * bytesPerPixel;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, bytes.empty() ? nullptr : &initial, &texture)),
            "create texture");
    return texture;
}

ComPtr<ID3D11ShaderResourceView> CreateView(ID3D11Device* device,
                                            ID3D11Texture2D* texture)
{
    ComPtr<ID3D11ShaderResourceView> view;
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture, nullptr, &view)),
            "create SRV");
    return view;
}

ComPtr<ID3D11Texture2D> CreateOutput(ID3D11Device* device,
                                     DXGI_FORMAT format,
                                     UINT width,
                                     UINT height,
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
            "create output");
    Require(SUCCEEDED(device->CreateRenderTargetView(
                texture.Get(), nullptr, &view)),
            "create output RTV");
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
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(
                &stagingDesc, nullptr, &staging)),
            "create staging");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(
                staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging");
    const size_t rowBytes = size_t(desc.Width) * bytesPerPixel;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(bytes.data() + size_t(y) * rowBytes,
               static_cast<const uint8_t*>(mapped.pData) +
                   size_t(y) * mapped.RowPitch,
               rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

void DumpIfRequested(const std::vector<uint8_t>& bytes, const char* name)
{
    char root[32768] = {};
    const DWORD length =
        GetEnvironmentVariableA("NGU_CHROMA_DUMP_ROOT", root, 32768);
    if (!length || length >= 32768)
        return;
    const auto path = std::filesystem::path(std::string(root, length)) / name;
    std::ofstream file(path, std::ios::binary);
    Require(bool(file), "open chroma dump");
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    Require(bool(file), "write chroma dump");
}

bool CompareCode(const std::vector<uint8_t>& actual,
                 const std::vector<uint8_t>& expected,
                 int components,
                 int maximumAllowed,
                 int width,
                 const char* name)
{
    int different = 0;
    int above = 0;
    int maximum = 0;
    bool reported = false;
    for (size_t pixel = 0; pixel < actual.size() / (size_t(components) * 2);
         ++pixel)
    {
        for (int component = 0; component < components; ++component)
        {
            const size_t offset =
                (pixel * size_t(components) + size_t(component)) * 2;
            const uint16_t a = uint16_t(actual[offset]) |
                               (uint16_t(actual[offset + 1]) << 8);
            const uint16_t e = uint16_t(expected[offset]) |
                               (uint16_t(expected[offset + 1]) << 8);
            const int delta = std::abs(int(a) - int(e));
            different += delta != 0;
            above += delta > maximumAllowed;
            maximum = std::max(maximum, delta);
            if (delta > maximumAllowed && !reported)
            {
                reported = true;
                std::printf(
                    "%s first x=%zu y=%zu component=%d actual=%u expected=%u\n",
                    name,
                    pixel % size_t(width),
                    pixel / size_t(width),
                    component,
                    a,
                    e);
            }
        }
    }
    std::printf("%s different=%d maximum=%d diff_gt_%d=%d\n",
                name,
                different,
                maximum,
                maximumAllowed,
                above);
    return above == 0;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "usage: test_ngu_chroma_combine <capture-root>");
        const std::filesystem::path root(argv[1]);
        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);
        NguScaler ngu;
        if (!ngu.PrepareNativeChromaCombines(d3d->gpu))
            throw std::runtime_error(ngu.LastError());

        auto combine1AddBytes = ExtractL16(
            ReadBytes(root / "input_00121_PS_564_500335c8_s0.bin",
                      size_t(1280) * 720 * 8));
        const auto combine1NnBytes =
            ReadBytes(root / "input_00121_PS_564_500335c8_s2.bin",
                      size_t(642) * 362 * 8);
        auto combine1Add = CreateTexture(d3d->device,
                                         combine1AddBytes,
                                         DXGI_FORMAT_R16_UNORM,
                                         1280,
                                         720,
                                         2);
        auto combine1Nn = CreateTexture(d3d->device,
                                        combine1NnBytes,
                                        DXGI_FORMAT_R16G16B16A16_FLOAT,
                                        642,
                                        362,
                                        8);
        const auto combine1AddView = CreateView(d3d->device, combine1Add.Get());
        const auto combine1NnView = CreateView(d3d->device, combine1Nn.Get());
        ComPtr<ID3D11RenderTargetView> combine1View;
        auto combine1Output = CreateOutput(d3d->device,
                                           DXGI_FORMAT_R16_UNORM,
                                           1280,
                                           720,
                                           combine1View);
        if (!ngu.RunNativeChromaCombine1(combine1AddView.Get(),
                                         combine1NnView.Get(),
                                         combine1View.Get()))
            throw std::runtime_error(ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto combine1Actual = Download(
            d3d->device, context.Get(), combine1Output.Get(), 2);
        DumpIfRequested(combine1Actual, "ps564_combine1_l16.bin");
        const auto combine1Expected = ExtractL16(
            ReadBytes(root / "draw_00121_PS_564_500335c8_rt0_1280x720_f81.bin",
                      size_t(1280) * 720 * 8));
        bool passed =
            CompareCode(combine1Actual, combine1Expected, 1, 1, 1280, "PS_564");

        auto combine2AddBytes = ExtractL16(
            ReadBytes(root / "input_00154_PS_644_512f7798_s0.bin",
                      size_t(1280) * 720 * 8));
        auto combine2CbBytes = ExtractL16(
            ReadBytes(root / "input_00154_PS_644_512f7798_s2.bin",
                      size_t(1280) * 720 * 8));
        const auto combine2NnBytes =
            ReadBytes(root / "input_00154_PS_644_512f7798_s3.bin",
                      size_t(642) * 362 * 8);
        auto combine2Add = CreateTexture(d3d->device,
                                         combine2AddBytes,
                                         DXGI_FORMAT_R16_UNORM,
                                         1280,
                                         720,
                                         2);
        auto combine2Cb = CreateTexture(d3d->device,
                                        combine2CbBytes,
                                        DXGI_FORMAT_R16_UNORM,
                                        1280,
                                        720,
                                        2);
        auto combine2Nn = CreateTexture(d3d->device,
                                        combine2NnBytes,
                                        DXGI_FORMAT_R16G16B16A16_FLOAT,
                                        642,
                                        362,
                                        8);
        const auto combine2AddView = CreateView(d3d->device, combine2Add.Get());
        const auto combine2CbView = CreateView(d3d->device, combine2Cb.Get());
        const auto combine2NnView = CreateView(d3d->device, combine2Nn.Get());
        ComPtr<ID3D11RenderTargetView> combine2View;
        auto combine2Output = CreateOutput(d3d->device,
                                           DXGI_FORMAT_R16G16_UNORM,
                                           1280,
                                           720,
                                           combine2View);
        if (!ngu.RunNativeChromaCombine2(combine2AddView.Get(),
                                         combine2CbView.Get(),
                                         combine2NnView.Get(),
                                         combine2View.Get()))
            throw std::runtime_error(ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto combine2Actual = Download(
            d3d->device, context.Get(), combine2Output.Get(), 4);
        DumpIfRequested(combine2Actual, "ps644_combine2_g16r16.bin");
        const auto combine2Expected =
            ReadBytes(root / "draw_00154_PS_644_512f7798_rt0_1280x720_f34.bin",
                      size_t(1280) * 720 * 4);
        passed &=
            CompareCode(combine2Actual, combine2Expected, 2, 1, 1280, "PS_644");
        if (!passed)
        {
            std::puts("FAIL_PRODUCT chroma combine comparison");
            return 1;
        }
        std::puts("PASS_PRODUCT native PS_564/PS_644 chroma combines");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
