#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
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

ComPtr<ID3D11Texture2D> CreateL16Input(ID3D11Device* device,
                                       const std::vector<uint8_t>& bytes,
                                       UINT width,
                                       UINT height,
                                       ComPtr<ID3D11ShaderResourceView>& view)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    initial.SysMemPitch = width * 2;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)),
            "create L16 input");
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
    viewDesc.Format = DXGI_FORMAT_R16_UNORM;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels = 1;
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), &viewDesc, &view)),
            "create L16 SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateRg16Input(
    ID3D11Device* device,
    const std::vector<uint8_t>& bytes,
    UINT width,
    UINT height,
    ComPtr<ID3D11ShaderResourceView>& view)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    initial.SysMemPitch = width * 4;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)),
            "create RG16 input");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &view)),
            "create RG16 SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateR8Input(
    ID3D11Device* device,
    const std::vector<uint8_t>& bytes,
    UINT width,
    UINT height,
    ComPtr<ID3D11ShaderResourceView>& view)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    initial.SysMemPitch = width;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)),
            "create R8 input");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &view)),
            "create R8 SRV");
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

ComPtr<ID3D11Texture2D> CreateL16Output(
    ID3D11Device* device,
    UINT width,
    UINT height,
    ComPtr<ID3D11RenderTargetView>& view,
    ComPtr<ID3D11ShaderResourceView>& shaderView)
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
            "create L16 output");
    Require(SUCCEEDED(device->CreateRenderTargetView(
                texture.Get(), nullptr, &view)),
            "create L16 output RTV");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &shaderView)),
            "create L16 output SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateRg16Output(
    ID3D11Device* device,
    UINT width,
    UINT height,
    ComPtr<ID3D11RenderTargetView>& view,
    ComPtr<ID3D11ShaderResourceView>& shaderView)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)),
            "create RG16 output");
    Require(SUCCEEDED(device->CreateRenderTargetView(
                texture.Get(), nullptr, &view)),
            "create RG16 output RTV");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &shaderView)),
            "create RG16 output SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateRgbaOutput(
    ID3D11Device* device,
    UINT width,
    UINT height,
    ComPtr<ID3D11RenderTargetView>& view,
    ComPtr<ID3D11ShaderResourceView>& shaderView)
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
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)),
            "create RGBA output");
    Require(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)),
            "create RGBA output RTV");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &shaderView)),
            "create RGBA output SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateRgbaUnormOutput(
    ID3D11Device* device,
    UINT width,
    UINT height,
    ComPtr<ID3D11RenderTargetView>& view,
    ComPtr<ID3D11ShaderResourceView>& shaderView)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)),
            "create RGBA UNORM output");
    Require(SUCCEEDED(device->CreateRenderTargetView(
                texture.Get(), nullptr, &view)),
            "create RGBA UNORM output RTV");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &shaderView)),
            "create RGBA UNORM output SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateRgbaUnormInput(
    ID3D11Device* device,
    const std::vector<uint8_t>& bytes,
    UINT width,
    UINT height,
    ComPtr<ID3D11ShaderResourceView>& shaderView)
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
            "create RGBA UNORM input");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &shaderView)),
            "create RGBA UNORM input SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateBgra8Input(
    ID3D11Device* device,
    const std::vector<uint8_t>& bytes,
    UINT width,
    UINT height,
    ComPtr<ID3D11ShaderResourceView>& shaderView)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    initial.SysMemPitch = width * 4;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)),
            "create BGRA input");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &shaderView)),
            "create BGRA input SRV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateRgbaFloatInput(
    ID3D11Device* device,
    const std::vector<uint8_t>& bytes,
    UINT width,
    UINT height,
    ComPtr<ID3D11ShaderResourceView>& shaderView)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = bytes.data();
    initial.SysMemPitch = width * 8;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)),
            "create RGBA float input");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                texture.Get(), nullptr, &shaderView)),
            "create RGBA float input SRV");
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

std::vector<uint8_t> ExtractRg16(const std::vector<uint8_t>& expanded)
{
    Require(expanded.size() % 8 == 0, "RG16 expanded size");
    std::vector<uint8_t> bytes(expanded.size() / 2);
    for (size_t pixel = 0; pixel < bytes.size() / 4; ++pixel)
        memcpy(bytes.data() + pixel * 4, expanded.data() + pixel * 8, 4);
    return bytes;
}

std::vector<uint8_t> ExtractBgra8(const std::vector<uint8_t>& expanded)
{
    return ExtractRg16(expanded);
}

void CompareWrittenTriangle(const std::vector<uint8_t>& actual,
                            const std::vector<uint8_t>& expected,
                            UINT width,
                            UINT height,
                            bool upperLeftTriangle,
                            int maximumAllowedDifference,
                            const char* name,
                            UINT virtualWidth = 1280,
                            UINT virtualHeight = 720)
{
    int different = 0;
    int maximum = 0;
    int aboveAllowed = 0;
    bool reported = false;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
        {
            if (upperLeftTriangle &&
                uint64_t(x) * virtualHeight +
                        uint64_t(y) * virtualWidth >=
                    uint64_t(width) * virtualHeight)
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

float HalfToFloat(uint16_t bits)
{
    const uint32_t sign = uint32_t(bits & 0x8000) << 16;
    const uint32_t exponent = (bits >> 10) & 0x1f;
    const uint32_t mantissa = bits & 0x3ff;
    uint32_t result = 0;
    if (exponent == 0)
    {
        if (mantissa)
        {
            uint32_t value = mantissa;
            uint32_t shift = 0;
            while ((value & 0x400) == 0)
            {
                value <<= 1;
                ++shift;
            }
            result = sign | ((127 - 15 - shift) << 23) |
                     ((value & 0x3ff) << 13);
        }
        else
            result = sign;
    }
    else if (exponent == 0x1f)
        result = sign | 0x7f800000 | (mantissa << 13);
    else
        result = sign | ((exponent + 112) << 23) | (mantissa << 13);
    return std::bit_cast<float>(result);
}

void CompareFloatStage(const std::vector<uint8_t>& actual,
                       const std::vector<uint8_t>& expected,
                       float maximumAllowedAbsolute,
                       const char* name)
{
    Require(actual.size() == expected.size(), "chroma network size");
    int different = 0;
    float maximumAbsolute = 0.0f;
    bool reported = false;
    const size_t componentCount = actual.size() / 2;
    for (size_t componentIndex = 0; componentIndex < componentCount;
         ++componentIndex)
    {
        const size_t offset = componentIndex * 2;
        const uint16_t aBits = uint16_t(actual[offset]) |
                               (uint16_t(actual[offset + 1]) << 8);
        const uint16_t eBits = uint16_t(expected[offset]) |
                               (uint16_t(expected[offset + 1]) << 8);
        const float a = HalfToFloat(aBits);
        const float e = HalfToFloat(eBits);
        const float difference = std::abs(a - e);
        different += aBits != eBits;
        maximumAbsolute = std::max(maximumAbsolute, difference);
        if (difference > maximumAllowedAbsolute && !reported)
        {
            reported = true;
            std::printf(
                "%s first component=%zu actual=%g expected=%g diff=%g\n",
                name,
                componentIndex,
                a,
                e,
                difference);
        }
    }
    std::printf("%s different=%d maximum_abs=%g allowed=%g\n",
                name,
                different,
                maximumAbsolute,
                maximumAllowedAbsolute);
    Require(maximumAbsolute <= maximumAllowedAbsolute,
            "chroma network comparison");
}

void CompareUnormOutput(const std::vector<uint8_t>& actual,
                        const std::vector<uint8_t>& expected,
                        int components,
                        int maximumAllowed,
                        UINT width,
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
    Require(above == 0, "chroma UNORM comparison");
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc >= 2 && argc <= 5,
                "usage: test_ngu_chroma_split <capture-root> [network-root "
                "[network-draw-base]] | <1080-capture-root> 1080-core");
        const std::filesystem::path root(argv[1]);
        const bool core1080 =
            argc == 3 && std::strcmp(argv[2], "1080-core") == 0;

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);
        NguScaler ngu;

        if (!core1080)
        {
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

            auto inputTexture = CreateInput(d3d->device, input, width, height);
            ComPtr<ID3D11ShaderResourceView> inputView;
            Require(SUCCEEDED(d3d->device->CreateShaderResourceView(
                        inputTexture.Get(), nullptr, &inputView)),
                    "create input SRV");
            ComPtr<ID3D11RenderTargetView> cbView;
            auto cbTexture = CreateOutput(d3d->device, width, height, cbView);
            ComPtr<ID3D11RenderTargetView> crView;
            auto crTexture = CreateOutput(d3d->device, width, height, crView);

            Require(ngu.PrepareNativeChromaSplit(d3d->gpu), ngu.LastError());
            Require(ngu.RunNativeChromaSplit(inputView.Get(),
                                             width,
                                             height,
                                             cbView.Get(),
                                             crView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto cb =
                Download(d3d->device, context.Get(), cbTexture.Get(), 2);
            const auto cr =
                Download(d3d->device, context.Get(), crTexture.Get(), 2);
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
            auto scaleXTexture =
                CreateOutput(d3d->device, 1280, 360, scaleXView);
            Require(ngu.PrepareNativeChromaScale(d3d->gpu), ngu.LastError());
            Require(ngu.RunNativeChromaScaleX(
                        cbShaderView.Get(), scaleXView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto scaleX =
                Download(d3d->device, context.Get(), scaleXTexture.Get(), 2);
            DumpIfRequested(scaleX, "scale_x_l16.bin");
            CompareWrittenTriangle(scaleX,
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
            auto scaleYTexture =
                CreateOutput(d3d->device, 1280, 720, scaleYView);
            Require(ngu.RunNativeChromaScaleY(
                        scaleXShaderView.Get(), scaleYView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto scaleY =
                Download(d3d->device, context.Get(), scaleYTexture.Get(), 2);
            DumpIfRequested(scaleY, "scale_y_l16.bin");
            CompareWrittenTriangle(scaleY,
                                   ExtractL16(expectedScaleY),
                                   1280,
                                   720,
                                   false,
                                   1,
                                   "PS_748 Y");
        }

        if (core1080 || argc >= 3)
        {
            const std::filesystem::path networkRoot =
                core1080 ? root : std::filesystem::path(argv[2]);
            const int networkDrawBase =
                core1080 ? 10 : (argc == 4 ? std::stoi(argv[3]) : 9);
            const UINT networkWidth = core1080 ? 960 : 640;
            const UINT networkHeight = core1080 ? 540 : 360;
            const UINT denseWidth = networkWidth + 2;
            const UINT denseHeight = networkHeight + 2;
            const auto networkInputExpanded =
                ReadBytes(networkRoot /
                              ("input_" +
                               [&] {
                                   char fileName[32] = {};
                                   std::snprintf(fileName,
                                                 sizeof(fileName),
                                                 "%05d",
                                                 networkDrawBase);
                                   return std::string(fileName);
                               }() +
                               "_PS_1672_b083dfa8_s0.bin"),
                          size_t(networkWidth) * networkHeight * 8);
            const auto networkInput = ExtractL16(networkInputExpanded);
            ComPtr<ID3D11ShaderResourceView> networkInputView;
            auto networkInputTexture =
                CreateL16Input(d3d->device,
                               networkInput,
                               networkWidth,
                               networkHeight,
                               networkInputView);

            std::array<ComPtr<ID3D11Texture2D>, 4> initialTextures;
            std::array<ComPtr<ID3D11RenderTargetView>, 4> initialViews;
            std::array<ComPtr<ID3D11ShaderResourceView>, 4> initialShaderViews;
            for (size_t index = 0; index < initialTextures.size(); ++index)
            {
                initialTextures[index] =
                    CreateRgbaOutput(d3d->device,
                                     networkWidth,
                                     networkHeight,
                                     initialViews[index],
                                     initialShaderViews[index]);
            }

            if (!ngu.PrepareNativeChromaNetwork(d3d->gpu))
                throw std::runtime_error(ngu.LastError());
            std::array<ID3D11RenderTargetView*, 4> initialTargets = {};
            for (size_t index = 0; index < initialTargets.size(); ++index)
                initialTargets[index] = initialViews[index].Get();
            if (!ngu.RunNativeChromaCubici2(networkInputView.Get(),
                                            initialTargets.data()))
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            for (size_t index = 0; index < initialTextures.size(); ++index)
            {
                const auto actual = Download(d3d->device,
                                             context.Get(),
                                             initialTextures[index].Get(),
                                             8);
                const std::string dumpName =
                    "ps1672_rt" + std::to_string(index) + ".bin";
                DumpIfRequested(actual, dumpName.c_str());
                const auto path =
                    networkRoot /
                    ([&] {
                        char fileName[128] = {};
                        std::snprintf(
                            fileName,
                            sizeof(fileName),
                            "draw_%05d_PS_1672_b083dfa8_rt%zu_%ux%u_f113.bin",
                            networkDrawBase,
                            index,
                            networkWidth,
                            networkHeight);
                        return std::string(fileName);
                    })();
                const std::string name =
                    "PS_1672 rt" + std::to_string(index);
                CompareFloatStage(
                    actual,
                    ReadBytes(path,
                              size_t(networkWidth) * networkHeight * 8),
                    0.0f,
                    name.c_str());
            }

            std::array<ID3D11ShaderResourceView*, 4> current = {};
            const int denseDraw = networkDrawBase + 29;
            if (!core1080)
            {
                std::array<ComPtr<ID3D11Texture2D>, 28> layerTextures;
                std::array<ComPtr<ID3D11RenderTargetView>, 28> layerViews;
                std::array<ComPtr<ID3D11ShaderResourceView>, 28>
                    layerShaderViews;
                std::array<ComPtr<ID3D11Texture2D>, 4> capturedTextures;
                std::array<ComPtr<ID3D11ShaderResourceView>, 4> capturedViews;
                constexpr int inputStages[4] = {0, 2, 3, 4};
                for (int layer = 0; layer < 28; ++layer)
                {
                    const int draw = networkDrawBase + 1 + layer;
                    for (int feature = 0; feature < 4; ++feature)
                    {
                        char fileName[128] = {};
                        std::snprintf(
                            fileName,
                            sizeof(fileName),
                            "input_%05d_PS_4620_342cc74b_s%d.bin",
                            draw,
                            inputStages[feature]);
                        capturedTextures[feature] =
                            CreateRgbaFloatInput(
                                d3d->device,
                                ReadBytes(
                                    networkRoot / fileName,
                                    size_t(networkWidth) * networkHeight * 8),
                                networkWidth,
                                networkHeight,
                                capturedViews[feature]);
                        current[feature] = capturedViews[feature].Get();
                    }
                    layerTextures[layer] =
                        CreateRgbaOutput(d3d->device,
                                         networkWidth,
                                         networkHeight,
                                         layerViews[layer],
                                         layerShaderViews[layer]);
                    if (!ngu.RunNativeChromaLayer(current.data(),
                                                  layer,
                                                  layerViews[layer].Get()))
                        throw std::runtime_error(ngu.LastError());
                    pl_gpu_finish(d3d->gpu);
                    const auto actual =
                        Download(d3d->device,
                                 context.Get(),
                                 layerTextures[layer].Get(),
                                 8);
                    const std::string dumpName =
                        "ps4620_layer_" + std::to_string(layer) + ".bin";
                    DumpIfRequested(actual, dumpName.c_str());
                    char fileName[128] = {};
                    std::snprintf(
                        fileName,
                        sizeof(fileName),
                        "draw_%05d_PS_4620_342cc74b_rt0_%ux%u_f113.bin",
                        draw,
                        networkWidth,
                        networkHeight);
                    const auto path = networkRoot / fileName;
                    const std::string name =
                        "PS_4620 layer " + std::to_string(layer);
                    CompareFloatStage(
                        actual,
                        ReadBytes(path,
                                  size_t(networkWidth) * networkHeight * 8),
                        0.00004f,
                        name.c_str());
                }

                ComPtr<ID3D11RenderTargetView> denseView;
                ComPtr<ID3D11ShaderResourceView> denseShaderView;
                for (int feature = 0; feature < 4; ++feature)
                {
                    char fileName[128] = {};
                    std::snprintf(
                        fileName,
                        sizeof(fileName),
                        "input_%05d_PS_2672_9e6139d1_s%d.bin",
                        denseDraw,
                        inputStages[feature]);
                    capturedTextures[feature] =
                        CreateRgbaFloatInput(
                            d3d->device,
                            ReadBytes(networkRoot / fileName,
                                      size_t(networkWidth) * networkHeight * 8),
                            networkWidth,
                            networkHeight,
                            capturedViews[feature]);
                    current[feature] = capturedViews[feature].Get();
                }
                auto denseTexture =
                    CreateRgbaOutput(d3d->device,
                                     denseWidth,
                                     denseHeight,
                                     denseView,
                                     denseShaderView);
                if (!ngu.RunNativeChromaDense(current.data(), denseView.Get()))
                    throw std::runtime_error(ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto dense = Download(d3d->device,
                                            context.Get(),
                                            denseTexture.Get(),
                                            8);
                DumpIfRequested(dense, "ps2672_dense.bin");
                CompareFloatStage(
                    dense,
                    ReadBytes(networkRoot /
                                  ([&] {
                                      char fileName[128] = {};
                                      std::snprintf(
                                          fileName,
                                          sizeof(fileName),
                                          "draw_%05d_PS_2672_9e6139d1_"
                                          "rt0_%ux%u_f113.bin",
                                          denseDraw,
                                          denseWidth,
                                          denseHeight);
                                      return std::string(fileName);
                                  })(),
                              size_t(denseWidth) * denseHeight * 8),
                    0.0f,
                    "PS_2672 dense");
            }

            std::array<ComPtr<ID3D11Texture2D>, 28> cascadeTextures;
            std::array<ComPtr<ID3D11RenderTargetView>, 28> cascadeViews;
            std::array<ComPtr<ID3D11ShaderResourceView>, 28>
                cascadeShaderViews;
            for (size_t index = 0; index < current.size(); ++index)
                current[index] = initialShaderViews[index].Get();
            for (int layer = 0; layer < 28; ++layer)
            {
                cascadeTextures[layer] =
                    CreateRgbaOutput(d3d->device,
                                     networkWidth,
                                     networkHeight,
                                     cascadeViews[layer],
                                     cascadeShaderViews[layer]);
                if (!ngu.RunNativeChromaLayer(current.data(),
                                              layer,
                                              cascadeViews[layer].Get()))
                    throw std::runtime_error(ngu.LastError());
                if (layer % 4 == 3)
                {
                    for (int feature = 0; feature < 4; ++feature)
                        current[feature] =
                            cascadeShaderViews[layer - 3 + feature].Get();
                }
            }
            ComPtr<ID3D11RenderTargetView> cascadeDenseView;
            ComPtr<ID3D11ShaderResourceView> cascadeDenseShaderView;
            auto cascadeDenseTexture =
                CreateRgbaOutput(d3d->device,
                                 denseWidth,
                                 denseHeight,
                                 cascadeDenseView,
                                 cascadeDenseShaderView);
            if (!ngu.RunNativeChromaDense(current.data(),
                                          cascadeDenseView.Get()))
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto cascadeDense =
                Download(d3d->device,
                         context.Get(),
                         cascadeDenseTexture.Get(),
                         8);
            DumpIfRequested(cascadeDense, "ps2672_cascade_dense.bin");
            CompareFloatStage(
                cascadeDense,
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_2672_9e6139d1_"
                                      "rt0_%ux%u_f113.bin",
                                      denseDraw,
                                      denseWidth,
                                      denseHeight);
                                  return std::string(fileName);
                              })(),
                          size_t(denseWidth) * denseHeight * 8),
                0.00004f,
                "PS_2672 cascade dense");

            if (core1080 ? !ngu.PrepareNativeChromaVeryHigh1080(d3d->gpu)
                         : !ngu.PrepareNativeChromaVeryHigh720(d3d->gpu))
                throw std::runtime_error(ngu.LastError());
            ComPtr<ID3D11RenderTargetView> schedulerView;
            ComPtr<ID3D11ShaderResourceView> schedulerShaderView;
            auto schedulerTexture =
                CreateRgbaOutput(d3d->device,
                                 denseWidth,
                                 denseHeight,
                                 schedulerView,
                                 schedulerShaderView);
            const auto schedulerStart = std::chrono::steady_clock::now();
            const bool schedulerOk =
                core1080 ? ngu.RunNativeChromaVeryHigh1080(
                               networkInputView.Get(), schedulerView.Get())
                         : ngu.RunNativeChromaVeryHigh720(
                               networkInputView.Get(), schedulerView.Get());
            if (!schedulerOk)
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto schedulerEnd = std::chrono::steady_clock::now();
            const double schedulerMs =
                std::chrono::duration<double, std::milli>(
                    schedulerEnd - schedulerStart)
                    .count();
            std::printf("PS_2672 scheduler elapsed=%.3f ms/frame\n", schedulerMs);
            const auto schedulerDense =
                Download(d3d->device,
                         context.Get(),
                         schedulerTexture.Get(),
                         8);
            CompareFloatStage(
                schedulerDense,
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_2672_9e6139d1_"
                                      "rt0_%ux%u_f113.bin",
                                      denseDraw,
                                      denseWidth,
                                      denseHeight);
                                  return std::string(fileName);
                              })(),
                          size_t(denseWidth) * denseHeight * 8),
                0.00004f,
                "PS_2672 scheduler dense");

            const int crNetworkDraw = networkDrawBase + 33;
            const int crDenseDraw = networkDrawBase + 62;
            const auto crNetworkInputExpanded =
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "input_%05d_PS_1672_b083dfa8_s0.bin",
                                      crNetworkDraw);
                                  return std::string(fileName);
                              })(),
                          size_t(networkWidth) * networkHeight * 8);
            const auto crNetworkInput = ExtractL16(crNetworkInputExpanded);
            ComPtr<ID3D11ShaderResourceView> crNetworkInputView;
            auto crNetworkInputTexture =
                CreateL16Input(d3d->device,
                               crNetworkInput,
                               networkWidth,
                               networkHeight,
                               crNetworkInputView);
            ComPtr<ID3D11RenderTargetView> crSchedulerView;
            ComPtr<ID3D11ShaderResourceView> crSchedulerShaderView;
            auto crSchedulerTexture =
                CreateRgbaOutput(d3d->device,
                                 denseWidth,
                                 denseHeight,
                                 crSchedulerView,
                                 crSchedulerShaderView);
            const auto crSchedulerStart = std::chrono::steady_clock::now();
            const bool crSchedulerOk =
                core1080 ? ngu.RunNativeChromaVeryHigh1080(
                               crNetworkInputView.Get(), crSchedulerView.Get())
                         : ngu.RunNativeChromaVeryHigh720(
                               crNetworkInputView.Get(), crSchedulerView.Get());
            if (!crSchedulerOk)
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto crSchedulerEnd = std::chrono::steady_clock::now();
            const double crSchedulerMs =
                std::chrono::duration<double, std::milli>(
                    crSchedulerEnd - crSchedulerStart)
                    .count();
            std::printf("PS_2672 Cr scheduler elapsed=%.3f ms/frame\n",
                        crSchedulerMs);
            const auto crSchedulerDense =
                Download(d3d->device,
                         context.Get(),
                         crSchedulerTexture.Get(),
                         8);
            DumpIfRequested(crSchedulerDense, "ps2672_cr_dense.bin");
            CompareFloatStage(
                crSchedulerDense,
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_2672_9e6139d1_"
                                      "rt0_%ux%u_f113.bin",
                                      crDenseDraw,
                                      denseWidth,
                                      denseHeight);
                                  return std::string(fileName);
                              })(),
                          size_t(denseWidth) * denseHeight * 8),
                0.00004f,
                "PS_2672 Cr scheduler dense");

            if (core1080)
            {
                ComPtr<ID3D11ShaderResourceView> packedUvView;
                auto packedUvTexture =
                    CreateBgra8Input(
                        d3d->device,
                        ReadBytes(networkRoot /
                                      "input_00006_PS_260_cdfc6b55_s0.bin",
                                  size_t(960) * 540 * 4),
                        960,
                        540,
                        packedUvView);
                ComPtr<ID3D11RenderTargetView> cbView;
                ComPtr<ID3D11ShaderResourceView> cbShaderView;
                auto cbTexture =
                    CreateL16Output(
                        d3d->device, 960, 540, cbView, cbShaderView);
                ComPtr<ID3D11RenderTargetView> crView;
                ComPtr<ID3D11ShaderResourceView> crShaderView;
                auto crTexture =
                    CreateL16Output(
                        d3d->device, 960, 540, crView, crShaderView);
                Require(ngu.PrepareNativeChromaSplit(d3d->gpu),
                        ngu.LastError());
                Require(ngu.RunNativeChromaSplit(packedUvView.Get(),
                                                 960,
                                                 540,
                                                 cbView.Get(),
                                                 crView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto cbActual =
                    Download(d3d->device, context.Get(), cbTexture.Get(), 2);
                const auto crActual =
                    Download(d3d->device, context.Get(), crTexture.Get(), 2);
                const auto expectedCb1080 = ExtractL16(ReadBytes(
                    networkRoot /
                        "draw_00006_PS_260_cdfc6b55_rt0_960x540_f81.bin",
                    size_t(960) * 540 * 8));
                const auto expectedCr1080 = ExtractL16(ReadBytes(
                    networkRoot /
                        "draw_00006_PS_260_cdfc6b55_rt1_960x540_f81.bin",
                    size_t(960) * 540 * 8));
                const int splitCbDifferent =
                    memcmp(cbActual.data(),
                           expectedCb1080.data(),
                           cbActual.size()) != 0;
                const int splitCrDifferent =
                    memcmp(crActual.data(),
                           expectedCr1080.data(),
                           crActual.size()) != 0;
                std::printf("PS_260 1080 Cb different=%d Cr different=%d\n",
                            splitCbDifferent,
                            splitCrDifferent);
                Require(splitCbDifferent == 0 && splitCrDifferent == 0,
                        "1080p chroma split comparison");

                Require(ngu.PrepareNativeChromaScale(d3d->gpu),
                        ngu.LastError());
                ComPtr<ID3D11RenderTargetView> cbScaleXView;
                ComPtr<ID3D11ShaderResourceView> cbScaleXShaderView;
                auto cbScaleXTexture =
                    CreateL16Output(d3d->device,
                                    1920,
                                    540,
                                    cbScaleXView,
                                    cbScaleXShaderView);
                Require(ngu.RunNativeChromaScaleX(
                            cbShaderView.Get(), cbScaleXView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto cbScaleXActual =
                    Download(d3d->device,
                             context.Get(),
                             cbScaleXTexture.Get(),
                             2);
                DumpIfRequested(cbScaleXActual, "ps748_1080_cb_x.bin");
                CompareWrittenTriangle(
                    cbScaleXActual,
                    ExtractL16(ReadBytes(
                        networkRoot /
                            "draw_00008_PS_748_721dd04e_"
                            "rt0_1920x540_f81.bin",
                        size_t(1920) * 540 * 8)),
                    1920,
                    540,
                    true,
                    1,
                    "PS_748 X 1080",
                    1920,
                    1080);

                ComPtr<ID3D11RenderTargetView> crScaleXView;
                ComPtr<ID3D11ShaderResourceView> crScaleXShaderView;
                auto crScaleXTexture =
                    CreateL16Output(d3d->device,
                                    1920,
                                    540,
                                    crScaleXView,
                                    crScaleXShaderView);
                Require(ngu.RunNativeChromaScaleX(
                            crShaderView.Get(), crScaleXView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto crScaleXActual =
                    Download(d3d->device,
                             context.Get(),
                             crScaleXTexture.Get(),
                             2);
                DumpIfRequested(crScaleXActual, "ps748_1080_cr_x.bin");
                CompareWrittenTriangle(
                    crScaleXActual,
                    ExtractL16(ReadBytes(
                        networkRoot /
                            "draw_00041_PS_748_721dd04e_"
                            "rt0_1920x540_f81.bin",
                        size_t(1920) * 540 * 8)),
                    1920,
                    540,
                    true,
                    1,
                    "PS_748 Cr X 1080",
                    1920,
                    1080);

                ComPtr<ID3D11RenderTargetView> cbScaleYView;
                ComPtr<ID3D11ShaderResourceView> cbScaleYShaderView;
                auto cbScaleYTexture =
                    CreateL16Output(d3d->device,
                                    1920,
                                    1080,
                                    cbScaleYView,
                                    cbScaleYShaderView);
                Require(ngu.RunNativeChromaScaleY(
                            cbScaleXShaderView.Get(),
                            cbScaleYView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto cbScaleYActual =
                    Download(d3d->device,
                             context.Get(),
                             cbScaleYTexture.Get(),
                             2);
                DumpIfRequested(cbScaleYActual, "ps748_1080_cb_y.bin");
                CompareWrittenTriangle(
                    cbScaleYActual,
                    ExtractL16(ReadBytes(
                        networkRoot /
                            "draw_00009_PS_748_4085ea73_"
                            "rt0_1920x1080_f81.bin",
                        size_t(1920) * 1080 * 8)),
                    1920,
                    1080,
                    false,
                    2,
                    "PS_748 Y 1080");

                ComPtr<ID3D11RenderTargetView> crScaleYView;
                ComPtr<ID3D11ShaderResourceView> crScaleYShaderView;
                auto crScaleYTexture =
                    CreateL16Output(d3d->device,
                                    1920,
                                    1080,
                                    crScaleYView,
                                    crScaleYShaderView);
                Require(ngu.RunNativeChromaScaleY(
                            crScaleXShaderView.Get(),
                            crScaleYView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto crScaleYActual =
                    Download(d3d->device,
                             context.Get(),
                             crScaleYTexture.Get(),
                             2);
                DumpIfRequested(crScaleYActual, "ps748_1080_cr_y.bin");
                CompareWrittenTriangle(
                    crScaleYActual,
                    ExtractL16(ReadBytes(
                        networkRoot /
                            "draw_00042_PS_748_4085ea73_"
                            "rt0_1920x1080_f81.bin",
                        size_t(1920) * 1080 * 8)),
                    1920,
                    1080,
                    false,
                    2,
                    "PS_748 Cr Y 1080");

                ComPtr<ID3D11RenderTargetView> combine1View;
                ComPtr<ID3D11ShaderResourceView> combine1ShaderView;
                auto combine1Texture =
                    CreateL16Output(d3d->device,
                                    1920,
                                    1080,
                                    combine1View,
                                    combine1ShaderView);
                Require(ngu.PrepareNativeChromaCombines(d3d->gpu),
                        ngu.LastError());
                Require(ngu.RunNativeChromaCombine1(
                            cbScaleYShaderView.Get(),
                            schedulerShaderView.Get(),
                            combine1View.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto combine1Actual =
                    Download(d3d->device,
                             context.Get(),
                             combine1Texture.Get(),
                             2);
                DumpIfRequested(combine1Actual, "ps564_1080.bin");
                CompareUnormOutput(
                    combine1Actual,
                    ExtractL16(ReadBytes(
                        networkRoot /
                            "draw_00040_PS_564_500335c8_"
                            "rt0_1920x1080_f81.bin",
                        size_t(1920) * 1080 * 8)),
                    1,
                    2,
                    1920,
                    "PS_564 1080");

                ComPtr<ID3D11RenderTargetView> combine2View;
                ComPtr<ID3D11ShaderResourceView> combine2ShaderView;
                auto combine2Texture =
                    CreateRg16Output(d3d->device,
                                     1920,
                                     1080,
                                     combine2View,
                                     combine2ShaderView);
                Require(ngu.RunNativeChromaCombine2(
                            crScaleYShaderView.Get(),
                            combine1ShaderView.Get(),
                            crSchedulerShaderView.Get(),
                            combine2View.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto combine2Actual =
                    Download(d3d->device,
                             context.Get(),
                             combine2Texture.Get(),
                             4);
                DumpIfRequested(combine2Actual, "ps644_1080.bin");
                CompareUnormOutput(
                    combine2Actual,
                    ReadBytes(networkRoot /
                                  "draw_00073_PS_644_512f7798_"
                                  "rt0_1920x1080_f34.bin",
                              size_t(1920) * 1080 * 4),
                    2,
                    2,
                    1920,
                    "PS_644 1080");

                ComPtr<ID3D11RenderTargetView> realignView;
                ComPtr<ID3D11ShaderResourceView> realignShaderView;
                auto realignTexture =
                    CreateRgbaUnormOutput(d3d->device,
                                          1920,
                                          1080,
                                          realignView,
                                          realignShaderView);
                Require(ngu.PrepareNativeChromaRealign(d3d->gpu),
                        ngu.LastError());
                Require(ngu.RunNativeChromaRealign(
                            combine2ShaderView.Get(), realignView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto realignActual =
                    Download(d3d->device,
                             context.Get(),
                             realignTexture.Get(),
                             8);
                DumpIfRequested(realignActual, "ps1516_1080.bin");
                CompareUnormOutput(
                    realignActual,
                    ReadBytes(networkRoot /
                                  "draw_00074_PS_1516_11f763dd_"
                                  "rt0_1920x1080_f36.bin",
                              size_t(1920) * 1080 * 8),
                    4,
                    2,
                    1920,
                    "PS_1516 1080");

                const auto yExpanded =
                    ReadBytes(networkRoot /
                                  "input_00075_PS_396_e3199f71_s0.bin",
                              size_t(1920) * 1080 * 8);
                std::vector<uint8_t> yPlane(size_t(1920) * 1080);
                for (size_t pixel = 0; pixel < yPlane.size(); ++pixel)
                    yPlane[pixel] = yExpanded[pixel * 8];
                ComPtr<ID3D11ShaderResourceView> yInputView;
                auto yInputTexture =
                    CreateR8Input(
                        d3d->device, yPlane, 1920, 1080, yInputView);
                ComPtr<ID3D11RenderTargetView> ycbcrView;
                ComPtr<ID3D11ShaderResourceView> ycbcrShaderView;
                auto ycbcrTexture =
                    CreateRgbaUnormOutput(d3d->device,
                                          1920,
                                          1080,
                                          ycbcrView,
                                          ycbcrShaderView);
                Require(ngu.PrepareNativeChromaYCbCr(d3d->gpu),
                        ngu.LastError());
                Require(ngu.RunNativeChromaYCbCr(yInputView.Get(),
                                                 realignShaderView.Get(),
                                                 ycbcrView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto ycbcrActual =
                    Download(d3d->device,
                             context.Get(),
                             ycbcrTexture.Get(),
                             8);
                DumpIfRequested(ycbcrActual, "ps396_1080.bin");
                CompareUnormOutput(
                    ycbcrActual,
                    ReadBytes(networkRoot /
                                  "draw_00075_PS_396_e3199f71_"
                                  "rt0_1920x1080_f36.bin",
                              size_t(1920) * 1080 * 8),
                    4,
                    3,
                    1920,
                    "PS_396 1080 chained");

                ComPtr<ID3D11ShaderResourceView> diffuseSourceView;
                auto diffuseSourceTexture =
                    CreateBgra8Input(
                        d3d->device,
                        ReadBytes(networkRoot /
                                      "input_00148_PS_284_e8e2a8eb_s0.bin",
                                  size_t(960) * 540 * 4),
                        960,
                        540,
                        diffuseSourceView);
                ComPtr<ID3D11ShaderResourceView> mainOriginalView;
                auto mainOriginalTexture =
                    CreateRgbaUnormInput(
                        d3d->device,
                        ReadBytes(networkRoot /
                                      "input_00149_PS_4412_c39b49cf_s0.bin",
                                  size_t(1920) * 1080 * 8),
                        1920,
                        1080,
                        mainOriginalView);
                ComPtr<ID3D11ShaderResourceView> baseOriginalView;
                auto baseOriginalTexture =
                    CreateRgbaUnormInput(
                        d3d->device,
                        ReadBytes(networkRoot /
                                      "input_00153_PS_4576_b7830945_s0.bin",
                                  size_t(1920) * 1080 * 8),
                        1920,
                        1080,
                        baseOriginalView);
                Require(ngu.PrepareNativeSuperResStrength3_1080(d3d->gpu),
                        ngu.LastError());
                ComPtr<ID3D11RenderTargetView> superResView;
                ComPtr<ID3D11ShaderResourceView> superResShaderView;
                auto superResTexture =
                    CreateRgbaUnormOutput(d3d->device,
                                          1920,
                                          1080,
                                          superResView,
                                          superResShaderView);
                Require(ngu.RunNativeSuperResStrength3_1080(
                            ycbcrShaderView.Get(),
                            diffuseSourceView.Get(),
                            mainOriginalView.Get(),
                            baseOriginalView.Get(),
                            superResView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto superResActual =
                    Download(d3d->device,
                             context.Get(),
                             superResTexture.Get(),
                             8);
                DumpIfRequested(superResActual, "ps4576_1080.bin");
                CompareUnormOutput(
                    superResActual,
                    ReadBytes(networkRoot /
                                  "draw_00153_PS_4576_b7830945_"
                                  "rt0_1920x1080_f36.bin",
                              size_t(1920) * 1080 * 8),
                    4,
                    8,
                    1920,
                    "PS_4576 1080 chained");
            }

            if (!core1080)
            {
            const int realignDraw = networkDrawBase + 64;
            const auto realignInput =
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "input_%05d_PS_1516_11f763dd_s0.bin",
                                      realignDraw);
                                  return std::string(fileName);
                              })(),
                          size_t(1280) * 720 * 4);
            ComPtr<ID3D11ShaderResourceView> realignInputView;
            auto realignInputTexture =
                CreateRg16Input(d3d->device,
                                realignInput,
                                1280,
                                720,
                                realignInputView);
            ComPtr<ID3D11RenderTargetView> realignView;
            ComPtr<ID3D11ShaderResourceView> realignShaderView;
            auto realignTexture =
                CreateRgbaUnormOutput(d3d->device,
                                      1280,
                                      720,
                                      realignView,
                                      realignShaderView);
            if (!ngu.PrepareNativeChromaRealign(d3d->gpu))
                throw std::runtime_error(ngu.LastError());
            if (!ngu.RunNativeChromaRealign(realignInputView.Get(),
                                            realignView.Get()))
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto realignActual =
                Download(d3d->device,
                         context.Get(),
                         realignTexture.Get(),
                         8);
            DumpIfRequested(realignActual, "ps1516_realign.bin");
            const auto realignTarget =
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_1516_11f763dd_"
                                      "rt0_1280x720_f36.bin",
                                      realignDraw);
                                  return std::string(fileName);
                              })(),
                          size_t(1280) * 720 * 8);
            CompareUnormOutput(
                realignActual,
                realignTarget,
                4,
                1,
                1280,
                "PS_1516 Realign");

            const int ycbcrDraw = networkDrawBase + 65;
            const auto yExpanded =
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "input_%05d_PS_396_e3199f71_s0.bin",
                                      ycbcrDraw);
                                  return std::string(fileName);
                              })(),
                          size_t(1280) * 720 * 8);
            std::vector<uint8_t> yPlane(size_t(1280) * 720);
            for (size_t pixel = 0; pixel < yPlane.size(); ++pixel)
                yPlane[pixel] = yExpanded[pixel * 8];
            ComPtr<ID3D11ShaderResourceView> yInputView;
            auto yInputTexture =
                CreateR8Input(d3d->device,
                              yPlane,
                              1280,
                              720,
                              yInputView);
            ComPtr<ID3D11ShaderResourceView> cbcrInputView;
            const auto realignTargetRg = ExtractRg16(realignTarget);
            auto cbcrInputTexture =
                CreateRg16Input(d3d->device,
                                realignTargetRg,
                                1280,
                                720,
                                cbcrInputView);
            ComPtr<ID3D11RenderTargetView> ycbcrView;
            ComPtr<ID3D11ShaderResourceView> ycbcrShaderView;
            auto ycbcrTexture =
                CreateRgbaUnormOutput(d3d->device,
                                      1280,
                                      720,
                                      ycbcrView,
                                      ycbcrShaderView);
            if (!ngu.PrepareNativeChromaYCbCr(d3d->gpu))
                throw std::runtime_error(ngu.LastError());
            if (!ngu.RunNativeChromaYCbCr(yInputView.Get(),
                                          cbcrInputView.Get(),
                                          ycbcrView.Get()))
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto ycbcrActual =
                Download(d3d->device,
                         context.Get(),
                         ycbcrTexture.Get(),
                         8);
            DumpIfRequested(ycbcrActual, "ps396_ycbcr.bin");
            CompareUnormOutput(
                ycbcrActual,
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_396_e3199f71_"
                                      "rt0_1280x720_f36.bin",
                                      ycbcrDraw);
                                  return std::string(fileName);
                              })(),
                          size_t(1280) * 720 * 8),
                4,
                0,
                1280,
                "PS_396 YCbCr");

            const auto combine1AddBytes = ExtractL16(
                ReadBytes(networkRoot /
                              "input_00121_PS_564_500335c8_s0.bin",
                          size_t(1280) * 720 * 8));
            ComPtr<ID3D11ShaderResourceView> combine1AddView;
            auto combine1AddTexture =
                CreateL16Input(d3d->device,
                               combine1AddBytes,
                               1280,
                                 720,
                               combine1AddView);
            ComPtr<ID3D11RenderTargetView> combine1View;
            ComPtr<ID3D11ShaderResourceView> combine1ShaderView;
            auto combine1Texture =
                CreateL16Output(d3d->device,
                                1280,
                                720,
                                combine1View,
                                combine1ShaderView);
            if (!ngu.PrepareNativeChromaCombines(d3d->gpu))
                throw std::runtime_error(ngu.LastError());
            if (!ngu.RunNativeChromaCombine1(combine1AddView.Get(),
                                             schedulerShaderView.Get(),
                                             combine1View.Get()))
                throw std::runtime_error(ngu.LastError());

            const auto combine2AddBytes = ExtractL16(
                ReadBytes(networkRoot /
                              "input_00154_PS_644_512f7798_s0.bin",
                          size_t(1280) * 720 * 8));
            ComPtr<ID3D11ShaderResourceView> combine2AddView;
            auto combine2AddTexture =
                CreateL16Input(d3d->device,
                               combine2AddBytes,
                               1280,
                               720,
                               combine2AddView);
            ComPtr<ID3D11RenderTargetView> combine2View;
            ComPtr<ID3D11ShaderResourceView> combine2ShaderView;
            auto combine2Texture =
                CreateRg16Output(d3d->device,
                                 1280,
                                 720,
                                 combine2View,
                                 combine2ShaderView);
            if (!ngu.RunNativeChromaCombine2(combine2AddView.Get(),
                                             combine1ShaderView.Get(),
                                             crSchedulerShaderView.Get(),
                                             combine2View.Get()))
                throw std::runtime_error(ngu.LastError());

            ComPtr<ID3D11RenderTargetView> chainedRealignView;
            ComPtr<ID3D11ShaderResourceView> chainedRealignShaderView;
            auto chainedRealignTexture =
                CreateRgbaUnormOutput(d3d->device,
                                      1280,
                                      720,
                                      chainedRealignView,
                                      chainedRealignShaderView);
            if (!ngu.RunNativeChromaRealign(combine2ShaderView.Get(),
                                            chainedRealignView.Get()))
                throw std::runtime_error(ngu.LastError());

            ComPtr<ID3D11RenderTargetView> chainedYCbCrView;
            ComPtr<ID3D11ShaderResourceView> chainedYCbCrShaderView;
            auto chainedYCbCrTexture =
                CreateRgbaUnormOutput(d3d->device,
                                      1280,
                                      720,
                                      chainedYCbCrView,
                                      chainedYCbCrShaderView);
            if (!ngu.RunNativeChromaYCbCr(yInputView.Get(),
                                          chainedRealignShaderView.Get(),
                                          chainedYCbCrView.Get()))
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto chainedYCbCr =
                Download(d3d->device,
                         context.Get(),
                         chainedYCbCrTexture.Get(),
                         8);
            DumpIfRequested(chainedYCbCr, "ps396_chained_ycbcr.bin");
            CompareUnormOutput(
                chainedYCbCr,
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_396_e3199f71_"
                                      "rt0_1280x720_f36.bin",
                                      ycbcrDraw);
                                  return std::string(fileName);
                              })(),
                          size_t(1280) * 720 * 8),
                4,
                3,
                1280,
                "PS_396 chained YCbCr");

            ComPtr<ID3D11ShaderResourceView> diffuseSourceView;
            auto diffuseSourceTexture =
                CreateBgra8Input(
                    d3d->device,
                    ReadBytes(networkRoot /
                                  "input_00159_PS_284_e8e2a8eb_s0.bin",
                             size_t(640) * 360 * 4),
                    640,
                    360,
                    diffuseSourceView);
            ComPtr<ID3D11ShaderResourceView> mainOriginalView;
            auto mainOriginalTexture =
                CreateRgbaUnormInput(
                    d3d->device,
                    ReadBytes(networkRoot /
                                  "input_00160_PS_4412_c39b49cf_s0.bin",
                             size_t(1280) * 720 * 8),
                    1280,
                    720,
                    mainOriginalView);
            ComPtr<ID3D11ShaderResourceView> baseOriginalView;
            auto baseOriginalTexture =
                CreateRgbaUnormInput(
                    d3d->device,
                    ReadBytes(networkRoot /
                                  "input_00164_PS_4576_b7830945_s0.bin",
                             size_t(1280) * 720 * 8),
                    1280,
                    720,
                    baseOriginalView);
            if (!ngu.PrepareNativeSuperResStrength3(d3d->gpu))
                throw std::runtime_error(ngu.LastError());
            ComPtr<ID3D11RenderTargetView> superResView;
            ComPtr<ID3D11ShaderResourceView> superResShaderView;
            auto superResTexture =
                CreateRgbaUnormOutput(d3d->device,
                                      1280,
                                      720,
                                      superResView,
                                      superResShaderView);
            if (!ngu.RunNativeSuperResStrength3(chainedYCbCrShaderView.Get(),
                                                diffuseSourceView.Get(),
                                                mainOriginalView.Get(),
                                                baseOriginalView.Get(),
                                                superResView.Get()))
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto superResActual =
                Download(d3d->device,
                         context.Get(),
                         superResTexture.Get(),
                         8);
            DumpIfRequested(superResActual, "ps4576_chained.bin");
            CompareUnormOutput(
                superResActual,
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_4576_b7830945_"
                                      "rt0_1280x720_f36.bin",
                                      ycbcrDraw + 8);
                                  return std::string(fileName);
                              })(),
                          size_t(1280) * 720 * 8),
                4,
                8,
                1280,
                "PS_4576 chained");

            const auto packedUvBytes =
                ReadBytes(networkRoot /
                              "input_00088_PS_260_cdfc6b55_s0.bin",
                          size_t(640) * 360 * 4);
            ComPtr<ID3D11ShaderResourceView> packedUvView;
            auto packedUvTexture =
                CreateBgra8Input(d3d->device,
                                 packedUvBytes,
                                 640,
                                 360,
                                 packedUvView);
            if (!ngu.PrepareNativeChromaVeryHigh720Full(d3d->gpu))
                throw std::runtime_error(ngu.LastError());
            ComPtr<ID3D11RenderTargetView> fullView;
            ComPtr<ID3D11ShaderResourceView> fullShaderView;
            auto fullTexture =
                CreateRgbaUnormOutput(d3d->device,
                                      1280,
                                      720,
                                      fullView,
                                      fullShaderView);
            if (!ngu.RunNativeChromaVeryHigh720Full(packedUvView.Get(),
                                                    yInputView.Get(),
                                                    diffuseSourceView.Get(),
                                                    mainOriginalView.Get(),
                                                    baseOriginalView.Get(),
                                                    fullView.Get()))
                throw std::runtime_error(ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto fullActual =
                Download(d3d->device,
                         context.Get(),
                         fullTexture.Get(),
                         8);
            DumpIfRequested(fullActual, "ps4576_full_native.bin");
            CompareUnormOutput(
                fullActual,
                ReadBytes(networkRoot /
                              ([&] {
                                  char fileName[128] = {};
                                  std::snprintf(
                                      fileName,
                                      sizeof(fileName),
                                      "draw_%05d_PS_4576_b7830945_"
                                      "rt0_1280x720_f36.bin",
                                      ycbcrDraw + 8);
                                  return std::string(fileName);
                              })(),
                          size_t(1280) * 720 * 8),
                4,
                8,
                1280,
                "PS_4576 full native");
            }
        }

        std::puts(core1080
                      ? "PASS_PRODUCT native 1080p chroma through PS_4576"
                      : "PASS_PRODUCT native PS_260/PS_748/PS_1672...PS_2672 chroma");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
