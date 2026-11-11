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

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path, size_t expected)
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

ComPtr<ID3D11Texture2D> CreateInput(ID3D11Device* device,
                                    const std::vector<uint8_t>& bytes,
                                    UINT width,
                                    UINT height)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    initial.SysMemPitch = width * 8;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)),
            "create input");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateOutput(ID3D11Device* device,
                                     UINT width,
                                     UINT height,
                                     ComPtr<ID3D11RenderTargetView>& view)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)),
            "create output");
    Require(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)),
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
    stagingDesc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)),
            "create staging");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging");
    const size_t rowBytes = size_t(desc.Width) * bytesPerPixel;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(bytes.data() + size_t(y) * rowBytes,
               static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
               rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

std::vector<uint8_t> ExtractL16(const std::vector<uint8_t>& expanded)
{
    std::vector<uint8_t> bytes(expanded.size() / 4);
    for (size_t pixel = 0; pixel < bytes.size() / 2; ++pixel)
        memcpy(bytes.data() + pixel * 2, expanded.data() + pixel * 8, 2);
    return bytes;
}

void CompareWrittenTriangle(const std::vector<uint8_t>& actual,
                            const std::vector<uint8_t>& expected,
                            UINT width,
                            UINT height,
                            bool upperLeftTriangle,
                            int maximumAllowedDifference,
                            const char* name)
{
    int different = 0;
    int maximum = 0;
    int aboveAllowed = 0;
    bool reported = false;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
        {
            if (upperLeftTriangle &&
                uint64_t(x) * 720 + uint64_t(y) * 1280 >=
                    uint64_t(width) * 720)
                continue;
            const size_t pixel = size_t(y) * width + x;
            const uint16_t a = uint16_t(actual[pixel * 2]) |
                               (uint16_t(actual[pixel * 2 + 1]) << 8);
            const uint16_t e = uint16_t(expected[pixel * 2]) |
                               (uint16_t(expected[pixel * 2 + 1]) << 8);
            const int delta = std::abs(int(a) - int(e));
            different += delta != 0;
            maximum = std::max(maximum, delta);
            aboveAllowed += delta > maximumAllowedDifference;
            if (delta && !reported)
            {
                reported = true;
                std::printf("%s first x=%u y=%u actual=%u expected=%u\n",
                            name, x, y, a, e);
            }
        }
    std::printf(
        "%s covered different=%d maximum=%d diff_gt_%d=%d\n",
        name,
        different,
        maximum,
        maximumAllowedDifference,
        aboveAllowed);
    Require(aboveAllowed == 0, "covered region comparison");
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

} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "usage: test_ngu_chroma_split <capture-root>");
        const std::filesystem::path root(argv[1]);
        constexpr UINT width = 640;
        constexpr UINT height = 360;
        const auto input =
            ReadBytes(root / "input_00006_PS_260_cdfc6b55_s0.bin",
                      size_t(width) * height * 8);
        const auto expectedCb =
            ReadBytes(root / "draw_00006_PS_260_cdfc6b55_rt0_640x360_f81.bin",
                      size_t(width) * height * 8);
        const auto expectedCr =
            ReadBytes(root / "draw_00006_PS_260_cdfc6b55_rt1_640x360_f81.bin",
                      size_t(width) * height * 8);
        const auto expectedScaleX =
            ReadBytes(root / "draw_00007_PS_748_721dd04e_rt0_1280x360_f81.bin",
                      size_t(1280) * 360 * 8);
        const auto expectedScaleY =
            ReadBytes(root / "draw_00008_PS_748_4085ea73_rt0_1280x720_f81.bin",
                      size_t(1280) * 720 * 8);

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);

        auto inputTexture = CreateInput(d3d->device, input, width, height);
        ComPtr<ID3D11ShaderResourceView> inputView;
        Require(SUCCEEDED(d3d->device->CreateShaderResourceView(
                    inputTexture.Get(), nullptr, &inputView)),
                "create input SRV");
        ComPtr<ID3D11RenderTargetView> cbView;
        auto cbTexture = CreateOutput(d3d->device, width, height, cbView);
        ComPtr<ID3D11RenderTargetView> crView;
        auto crTexture = CreateOutput(d3d->device, width, height, crView);

        NguScaler ngu;
        Require(ngu.PrepareNativeChromaSplit(d3d->gpu), ngu.LastError());
        Require(ngu.RunNativeChromaSplit(
                    inputView.Get(), width, height, cbView.Get(), crView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto cb = Download(d3d->device, context.Get(), cbTexture.Get(), 2);
        const auto cr = Download(d3d->device, context.Get(), crTexture.Get(), 2);
        const auto expectedCbL16 = ExtractL16(expectedCb);
        const auto expectedCrL16 = ExtractL16(expectedCr);
        const int cbDifferent =
            memcmp(cb.data(), expectedCbL16.data(), cb.size()) != 0;
        const int crDifferent =
            memcmp(cr.data(), expectedCrL16.data(), cr.size()) != 0;
        std::printf("PS_260 Cb different=%d Cr different=%d\n",
                    cbDifferent, crDifferent);
        Require(cbDifferent == 0 && crDifferent == 0,
                "chroma split comparison");

        ComPtr<ID3D11ShaderResourceView> cbShaderView;
        Require(SUCCEEDED(d3d->device->CreateShaderResourceView(
                    cbTexture.Get(), nullptr, &cbShaderView)),
                "create Cb SRV");
        ComPtr<ID3D11RenderTargetView> scaleXView;
        auto scaleXTexture = CreateOutput(d3d->device, 1280, 360, scaleXView);
        Require(ngu.PrepareNativeChromaScale(d3d->gpu), ngu.LastError());
        Require(ngu.RunNativeChromaScaleX(
                    cbShaderView.Get(), scaleXView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto scaleX = Download(d3d->device, context.Get(), scaleXTexture.Get(), 2);
        DumpIfRequested(scaleX, "scale_x_l16.bin");
        CompareWrittenTriangle(
            scaleX,
            ExtractL16(expectedScaleX),
            1280,
            360,
            true,
            0,
            "PS_748 X");

        ComPtr<ID3D11ShaderResourceView> scaleXShaderView;
        Require(SUCCEEDED(d3d->device->CreateShaderResourceView(
                    scaleXTexture.Get(), nullptr, &scaleXShaderView)),
                "create scale-X SRV");
        ComPtr<ID3D11RenderTargetView> scaleYView;
        auto scaleYTexture = CreateOutput(d3d->device, 1280, 720, scaleYView);
        Require(ngu.RunNativeChromaScaleY(
                    scaleXShaderView.Get(), scaleYView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto scaleY = Download(d3d->device, context.Get(), scaleYTexture.Get(), 2);
        DumpIfRequested(scaleY, "scale_y_l16.bin");
        CompareWrittenTriangle(
            scaleY,
            ExtractL16(expectedScaleY),
            1280,
            720,
            false,
            1,
            "PS_748 Y");
        std::puts("PASS_PRODUCT native PS_260/PS_748 chroma front end");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
