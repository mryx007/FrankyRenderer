#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
enum class HdrTestProfile
{
    PqBt2020,
    HlgBt2020,
    PqBt709,
};

void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

void WriteBmp32(const std::filesystem::path& path, UINT width, UINT height, const std::vector<uint8_t>& rgba)
{
    Require(rgba.size() == size_t(width) * height * 4, "BMP pixels");
    std::vector<uint8_t> bgra(rgba.size());
    for (size_t offset = 0; offset < rgba.size(); offset += 4)
    {
        bgra[offset] = rgba[offset + 2];
        bgra[offset + 1] = rgba[offset + 1];
        bgra[offset + 2] = rgba[offset];
        bgra[offset + 3] = 255;
    }
    BITMAPFILEHEADER file = {};
    BITMAPINFOHEADER info = {};
    info.biSize = sizeof(info);
    info.biWidth = LONG(width);
    info.biHeight = -LONG(height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = DWORD(bgra.size());
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + DWORD(bgra.size());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(bool(out), "open BMP output");
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(bgra.data()), std::streamsize(bgra.size()));
    Require(bool(out), "write BMP output");
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open sample");
    const auto size = file.tellg();
    Require(size > 0, "sample size");
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))),
            "read sample");
    return bytes;
}

ComPtr<ID3D11Texture2D> CreateDecoderTexture(
    ID3D11Device* device, const std::vector<uint8_t>& bytes, UINT width, UINT height, DXGI_FORMAT format)
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
        std::printf("decoder texture format=%u support=0x%08X hr=0x%08X\n", unsigned(format), support, unsigned(hr));
    }
    Require(SUCCEEDED(hr), "create decoder texture");
    return texture;
}

ComPtr<ID3D11Texture2D>
CreateOutput(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, ComPtr<ID3D11RenderTargetView>& view)
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
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)), "create source output");
    Require(SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, &view)), "create source output RTV");
    return texture;
}

ComPtr<ID3D11Texture2D> CreateSource(ID3D11Device* device,
                                     UINT width,
                                     UINT height,
                                     DXGI_FORMAT format,
                                     UINT bytesPerPixel,
                                     const std::vector<uint8_t>& bytes)
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
    initial.SysMemPitch = width * bytesPerPixel;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &texture)), "create source texture");
    return texture;
}

std::vector<uint8_t>
Download(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, UINT bytesPerPixel)
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
    std::vector<uint8_t> bytes(size_t(desc.Width) * desc.Height * bytesPerPixel);
    for (UINT y = 0; y < desc.Height; ++y)
        memcpy(bytes.data() + size_t(y) * desc.Width * bytesPerPixel,
               static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
               size_t(desc.Width) * bytesPerPixel);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

int CompareLuma(
    const std::vector<uint8_t>& actual, const std::vector<uint8_t>& source, UINT sourceWidth, UINT width, UINT height)
{
    int different = 0;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
        {
            const size_t sourceOffset = (size_t(y) * sourceWidth + x) * 2;
            const size_t outputOffset = (size_t(y) * width + x) * 2;
            different += memcmp(source.data() + sourceOffset, actual.data() + outputOffset, 2) != 0;
        }
    return different;
}

int CompareChroma(
    const std::vector<uint8_t>& actual, const std::vector<uint8_t>& source, UINT sourceWidth, UINT width, UINT height)
{
    int different = 0;
    bool reported = false;
    const UINT chromaWidth = width / 2;
    const UINT chromaHeight = height / 2;
    const size_t sourceBase = size_t(sourceWidth) * height * 2;
    for (UINT y = 0; y < chromaHeight; ++y)
        for (UINT x = 0; x < chromaWidth; ++x)
        {
            const size_t sourceOffset = sourceBase + (size_t(y) * sourceWidth + x * 2) * 2;
            const size_t outputOffset = (size_t(y) * chromaWidth + x) * 8;
            const bool uvDifferent = memcmp(source.data() + sourceOffset, actual.data() + outputOffset, 4) != 0;
            different += uvDifferent;
            uint32_t upper = 0;
            memcpy(&upper, actual.data() + outputOffset + 4, sizeof(upper));
            different += upper != 0;
            if ((uvDifferent || upper) && !reported)
            {
                reported = true;
                std::printf("chroma mismatch x=%u y=%u source=%02X%02X%02X%02X "
                            "actual=%02X%02X%02X%02X%02X%02X%02X%02X\n",
                            x,
                            y,
                            source[sourceOffset],
                            source[sourceOffset + 1],
                            source[sourceOffset + 2],
                            source[sourceOffset + 3],
                            actual[outputOffset],
                            actual[outputOffset + 1],
                            actual[outputOffset + 2],
                            actual[outputOffset + 3],
                            actual[outputOffset + 4],
                            actual[outputOffset + 5],
                            actual[outputOffset + 6],
                            actual[outputOffset + 7]);
            }
        }
    return different;
}

int CompareWords(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int& maximum, int& overEight)
{
    Require(actual.size() == expected.size(), "word comparison size");
    int different = 0;
    maximum = 0;
    overEight = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left = uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right = uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        const int delta = std::abs(int(left) - int(right));
        if (delta)
            ++different;
        maximum = std::max(maximum, delta);
        overEight += delta > 8;
    }
    return different;
}

int CompareRgb8(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int& maximum, int& overOne)
{
    Require(actual.size() == expected.size() && actual.size() % 4 == 0, "RGB8 comparison size");
    int different = 0;
    maximum = 0;
    overOne = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 4)
    {
        bool pixelOverOne = false;
        for (int channel = 0; channel < 3; ++channel)
        {
            const int delta = std::abs(int(actual[offset + channel]) - int(expected[offset + channel]));
            if (delta)
                ++different;
            maximum = std::max(maximum, delta);
            pixelOverOne = pixelOverOne || delta > 1;
        }
        overOne += pixelOverOne;
    }
    return different;
}

std::vector<uint8_t> ExtractL16(const std::vector<uint8_t>& source)
{
    Require(source.size() % 8 == 0, "L16 source size");
    std::vector<uint8_t> result(source.size() / 4);
    for (size_t word = 0; word < source.size() / 8; ++word)
    {
        result[word * 2] = source[word * 8];
        result[word * 2 + 1] = source[word * 8 + 1];
    }
    return result;
}

void TestNv12Split(NguScaler& ngu,
                   pl_gpu gpu,
                   ID3D11Device* device,
                   ID3D11DeviceContext* context,
                   const std::filesystem::path& sourcePath,
                   const std::filesystem::path& oracleRoot)
{
    const auto i420 = ReadBytes(sourcePath);
    Require(i420.size() == size_t(1280) * 720 * 3 / 2, "NV12 split source geometry");
    constexpr UINT width = 1280;
    constexpr UINT height = 720;
    constexpr UINT chromaWidth = width / 2;
    constexpr UINT chromaHeight = height / 2;
    const size_t lumaBytes = size_t(width) * height;
    const size_t planeBytes = size_t(chromaWidth) * chromaHeight;
    std::vector<uint8_t> y(i420.begin(), i420.begin() + lumaBytes);
    std::vector<uint8_t> uv(planeBytes * 2);
    for (size_t i = 0; i < planeBytes; ++i)
    {
        uv[i * 2] = i420[lumaBytes + i];
        uv[i * 2 + 1] = i420[lumaBytes + planeBytes + i];
    }

    auto yTexture = CreateSource(device, width, height, DXGI_FORMAT_R8_UNORM, 1, y);
    auto uvTexture = CreateSource(device, chromaWidth, chromaHeight, DXGI_FORMAT_R8G8_UNORM, 2, uv);
    ComPtr<ID3D11ShaderResourceView> yInput;
    ComPtr<ID3D11ShaderResourceView> uvInput;
    Require(SUCCEEDED(device->CreateShaderResourceView(yTexture.Get(), nullptr, &yInput)), "Y source SRV");
    Require(SUCCEEDED(device->CreateShaderResourceView(uvTexture.Get(), nullptr, &uvInput)), "UV source SRV");

    ComPtr<ID3D11RenderTargetView> cbView;
    auto cb = CreateOutput(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16_UNORM, cbView);
    ComPtr<ID3D11RenderTargetView> crView;
    auto cr = CreateOutput(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16_UNORM, crView);
    Require(ngu.RunNativeChromaSplit(uvInput.Get(), chromaWidth, chromaHeight, cbView.Get(), crView.Get()),
            ngu.LastError());
    const auto cbBytes = Download(device, context, cb.Get(), 2);
    const auto crBytes = Download(device, context, cr.Get(), 2);
    int cbMaximum = 0;
    int cbOverEight = 0;
    int crMaximum = 0;
    int crOverEight = 0;
    const int cbDifferent =
        CompareWords(cbBytes,
                     ExtractL16(ReadBytes(oracleRoot / "draw_00090_PS_260_cdfc6b55_rt0_640x360_f81.bin")),
                     cbMaximum,
                     cbOverEight);
    const int crDifferent =
        CompareWords(crBytes,
                     ExtractL16(ReadBytes(oracleRoot / "draw_00090_PS_260_cdfc6b55_rt1_640x360_f81.bin")),
                     crMaximum,
                     crOverEight);
    std::printf("NV12 split Cb different=%d maximum=%d diff_gt_8=%d "
                "Cr different=%d maximum=%d diff_gt_8=%d\n",
                cbDifferent,
                cbMaximum,
                cbOverEight,
                crDifferent,
                crMaximum,
                crOverEight);
    Require(cbDifferent == 0 && crDifferent == 0, "NV12 split oracle");

    Require(ngu.PrepareNativeChromaVeryHigh720Full(gpu), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> cbInput;
    Require(SUCCEEDED(device->CreateShaderResourceView(cb.Get(), nullptr, &cbInput)), "Cb source SRV");
    ComPtr<ID3D11RenderTargetView> scaleXView;
    auto scaleX = CreateOutput(device, width, height / 2, DXGI_FORMAT_R16_UNORM, scaleXView);
    Require(ngu.RunNativeChromaScaleX(cbInput.Get(), scaleXView.Get()), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> scaleXInput;
    Require(SUCCEEDED(device->CreateShaderResourceView(scaleX.Get(), nullptr, &scaleXInput)), "scale-X source SRV");
    ComPtr<ID3D11RenderTargetView> scaleYView;
    auto scaleY = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, scaleYView);
    Require(ngu.RunNativeChromaScaleY(scaleXInput.Get(), scaleYView.Get()), ngu.LastError());
    const auto scaleXBytes = Download(device, context, scaleX.Get(), 2);
    const auto scaleYBytes = Download(device, context, scaleY.Get(), 2);
    int scaleXMaximum = 0;
    int scaleXOverEight = 0;
    int scaleYMaximum = 0;
    int scaleYOverEight = 0;
    const int scaleXDifferent =
        CompareWords(scaleXBytes,
                     ExtractL16(ReadBytes(oracleRoot / "draw_00091_PS_748_721dd04e_rt0_1280x360_f81.bin")),
                     scaleXMaximum,
                     scaleXOverEight);
    const int scaleYDifferent =
        CompareWords(scaleYBytes,
                     ExtractL16(ReadBytes(oracleRoot / "draw_00092_PS_748_4085ea73_rt0_1280x720_f81.bin")),
                     scaleYMaximum,
                     scaleYOverEight);
    std::printf("NV12 chain PS_748 X different=%d maximum=%d diff_gt_8=%d "
                "Y different=%d maximum=%d diff_gt_8=%d\n",
                scaleXDifferent,
                scaleXMaximum,
                scaleXOverEight,
                scaleYDifferent,
                scaleYMaximum,
                scaleYOverEight);

    ComPtr<ID3D11RenderTargetView> cubicViews[4];
    ComPtr<ID3D11Texture2D> cubicOutputs[4];
    for (int i = 0; i < 4; ++i)
        cubicOutputs[i] =
            CreateOutput(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, cubicViews[i]);
    ID3D11RenderTargetView* cubicTargets[4] = {
        cubicViews[0].Get(), cubicViews[1].Get(), cubicViews[2].Get(), cubicViews[3].Get()};
    Require(ngu.RunNativeChromaCubici2(cbInput.Get(), cubicTargets), ngu.LastError());
    for (int i = 0; i < 4; ++i)
    {
        const auto actual = Download(device, context, cubicOutputs[i].Get(), 8);
        int maximum = 0;
        int overEight = 0;
        const auto expected =
            ReadBytes(oracleRoot / ("draw_00093_PS_1672_b083dfa8_rt" + std::to_string(i) + "_640x360_f113.bin"));
        const int different = CompareWords(actual, expected, maximum, overEight);
        std::printf("NV12 chain PS_1672 rt%d different=%d maximum=%d diff_gt_8=%d\n", i, different, maximum, overEight);
    }

    std::vector<uint8_t> expanded(width * height * 8);
    for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
    {
        const uint16_t value = uint16_t(y[pixel]) * 257;
        for (int channel = 0; channel < 3; ++channel)
        {
            const size_t offset = pixel * 8 + channel * 2;
            expanded[offset] = uint8_t(value);
            expanded[offset + 1] = uint8_t(value >> 8);
        }
        expanded[pixel * 8 + 6] = 0xFF;
        expanded[pixel * 8 + 7] = 0xFF;
    }
    auto expandedTexture = CreateSource(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, 8, expanded);
    ComPtr<ID3D11ShaderResourceView> expandedInput;
    Require(SUCCEEDED(device->CreateShaderResourceView(expandedTexture.Get(), nullptr, &expandedInput)),
            "expanded Y SRV");
    ComPtr<ID3D11RenderTargetView> chainView;
    auto chain = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, chainView);
    Require(ngu.RunNativeChromaVeryHigh720Full(
                uvInput.Get(), yInput.Get(), uvInput.Get(), expandedInput.Get(), expandedInput.Get(), chainView.Get()),
            ngu.LastError());
    const auto chainBytes = Download(device, context, chain.Get(), 8);
    int chainMaximum = 0;
    int chainOverEight = 0;
    const int chainDifferent = CompareWords(chainBytes,
                                            ReadBytes(oracleRoot / "draw_00082_PS_4576_b7830945_rt0_1280x720_f36.bin"),
                                            chainMaximum,
                                            chainOverEight);
    std::printf(
        "NV12 chain PS_4576 different=%d maximum=%d diff_gt_8=%d\n", chainDifferent, chainMaximum, chainOverEight);
    Require(chainMaximum <= 8 && chainOverEight == 0, "NV12 PS_4576 oracle");
    pl_gpu_finish(gpu);
}

void TestNv12Orientation(NguScaler& ngu, pl_gpu gpu, ID3D11Device* device, ID3D11DeviceContext* context)
{
    constexpr UINT width = 1920;
    constexpr UINT height = 1080;
    std::vector<uint8_t> y(size_t(width) * height, 128);
    std::vector<uint8_t> uv(size_t(width / 2) * (height / 2) * 2);
    for (size_t i = 0; i < uv.size(); i += 2)
    {
        uv[i] = 90;
        uv[i + 1] = 180;
    }
    auto yTexture = CreateSource(device, width, height, DXGI_FORMAT_R8_UNORM, 1, y);
    auto uvTexture = CreateSource(device, width / 2, height / 2, DXGI_FORMAT_R8G8_UNORM, 2, uv);
    ComPtr<ID3D11ShaderResourceView> yInput;
    ComPtr<ID3D11ShaderResourceView> uvInput;
    Require(SUCCEEDED(device->CreateShaderResourceView(yTexture.Get(), nullptr, &yInput)), "orientation Y SRV");
    Require(SUCCEEDED(device->CreateShaderResourceView(uvTexture.Get(), nullptr, &uvInput)), "orientation UV SRV");
    ComPtr<ID3D11RenderTargetView> outputView;
    auto output = CreateOutput(device, 3840, 2160, DXGI_FORMAT_R8G8B8A8_UNORM, outputView);
    Require(ngu.PrepareNativeNgu1080FromPlanes(gpu), ngu.LastError());
    Require(ngu.RunNativeNgu1080FromPlanes(uvInput.Get(), yInput.Get(), outputView.Get()), ngu.LastError());
    const auto pixels = Download(device, context, output.Get(), 4);
    std::array<uint64_t, 4> sums = {};
    for (size_t i = 0; i < pixels.size(); i += 4)
        for (int channel = 0; channel < 4; ++channel)
            sums[channel] += pixels[i + channel];
    const uint64_t count = pixels.size() / 4;
    std::printf("NV12 raw final average=%llu,%llu,%llu,%llu\n",
                static_cast<unsigned long long>(sums[0] / count),
                static_cast<unsigned long long>(sums[1] / count),
                static_cast<unsigned long long>(sums[2] / count),
                static_cast<unsigned long long>(sums[3] / count));
    pl_gpu_finish(gpu);
}

void TestNv12Final(NguScaler& ngu,
                   pl_gpu gpu,
                   ID3D11Device* device,
                   ID3D11DeviceContext* context,
                   const std::filesystem::path& sourcePath,
                   const std::filesystem::path& outputPath)
{
    constexpr UINT width = 1920;
    constexpr UINT height = 1080;
    constexpr UINT chromaWidth = width / 2;
    constexpr UINT chromaHeight = height / 2;
    const auto i420 = ReadBytes(sourcePath);
    Require(i420.size() == size_t(width) * height * 3 / 2, "NV12 final source geometry");
    const size_t lumaBytes = size_t(width) * height;
    const size_t planeBytes = size_t(chromaWidth) * chromaHeight;
    std::vector<uint8_t> y(i420.begin(), i420.begin() + lumaBytes);
    std::vector<uint8_t> uv(planeBytes * 2);
    for (size_t i = 0; i < planeBytes; ++i)
    {
        uv[i * 2] = i420[lumaBytes + i];
        uv[i * 2 + 1] = i420[lumaBytes + planeBytes + i];
    }
    auto yTexture = CreateSource(device, width, height, DXGI_FORMAT_R8_UNORM, 1, y);
    auto uvTexture = CreateSource(device, chromaWidth, chromaHeight, DXGI_FORMAT_R8G8_UNORM, 2, uv);
    ComPtr<ID3D11ShaderResourceView> yInput;
    ComPtr<ID3D11ShaderResourceView> uvInput;
    Require(SUCCEEDED(device->CreateShaderResourceView(yTexture.Get(), nullptr, &yInput)), "final Y SRV");
    Require(SUCCEEDED(device->CreateShaderResourceView(uvTexture.Get(), nullptr, &uvInput)), "final UV SRV");
    ComPtr<ID3D11RenderTargetView> outputView;
    auto output = CreateOutput(device, 3840, 2160, DXGI_FORMAT_R8G8B8A8_UNORM, outputView);
    Require(ngu.PrepareNativeNgu1080FromPlanes(gpu), ngu.LastError());
    Require(ngu.RunNativeNgu1080FromPlanes(uvInput.Get(), yInput.Get(), outputView.Get()), ngu.LastError());
    const auto pixels = Download(device, context, output.Get(), 4);
    WriteBmp32(outputPath, 3840, 2160, pixels);
    std::printf("NV12 native final -> %ls\n", outputPath.c_str());
    pl_gpu_finish(gpu);
}

void TestPacked(NguScaler& ngu,
                pl_gpu gpu,
                ID3D11Device* device,
                ID3D11DeviceContext* context,
                const std::filesystem::path& path,
                DXGI_FORMAT format,
                UINT paddedWidth,
                UINT width,
                UINT height,
                const char* name,
                const char* oraclePath = nullptr)
{
    const auto source = ReadBytes(path);
    Require(source.size() == size_t(paddedWidth) * height * 3, "decoder sample size");
    auto texture = CreateDecoderTexture(device, source, paddedWidth, height, format);

    ComPtr<ID3D11RenderTargetView> lumaView;
    auto luma = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, lumaView);
    ComPtr<ID3D11RenderTargetView> chromaView;
    auto chroma = CreateOutput(device, width / 2, height / 2, DXGI_FORMAT_R16G16B16A16_UNORM, chromaView);

    Require(ngu.RunNativeDecoderSourceCopy(texture.Get(), 0, int(width), int(height), lumaView.Get(), chromaView.Get()),
            ngu.LastError());
    const auto lumaBytes = Download(device, context, luma.Get(), 2);
    const auto chromaBytes = Download(device, context, chroma.Get(), 8);
    const int lumaDifferent = CompareLuma(lumaBytes, source, paddedWidth, width, height);
    const int chromaDifferent = CompareChroma(chromaBytes, source, paddedWidth, width, height);
    std::printf("%s luma different=%d chroma different=%d\n", name, lumaDifferent, chromaDifferent);
    Require(lumaDifferent == 0 && chromaDifferent == 0, "decoder source import comparison");

    if (oraclePath)
    {
        Require(width == 1920 && height == 1080, "P010 chain oracle requires 1080p");
        Require(ngu.PrepareNativeChromaVeryHigh1080Full(gpu), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> lumaShaderView;
        ComPtr<ID3D11ShaderResourceView> chromaShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(luma.Get(), nullptr, &lumaShaderView)), "luma SRV");
        Require(SUCCEEDED(device->CreateShaderResourceView(chroma.Get(), nullptr, &chromaShaderView)), "chroma SRV");

        ComPtr<ID3D11RenderTargetView> cbView;
        auto cb = CreateOutput(device, width / 2, height / 2, DXGI_FORMAT_R16_UNORM, cbView);
        ComPtr<ID3D11RenderTargetView> crView;
        auto cr = CreateOutput(device, width / 2, height / 2, DXGI_FORMAT_R16_UNORM, crView);
        Require(ngu.RunNativeChromaSplit(chromaShaderView.Get(), width / 2, height / 2, cbView.Get(), crView.Get()),
                ngu.LastError());
        const auto cbBytes = Download(device, context, cb.Get(), 2);
        const auto crBytes = Download(device, context, cr.Get(), 2);
        ComPtr<ID3D11ShaderResourceView> cbShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(cb.Get(), nullptr, &cbShaderView)), "Cb SRV");
        ComPtr<ID3D11ShaderResourceView> crShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(cr.Get(), nullptr, &crShaderView)), "Cr SRV");
        const std::filesystem::path oracleRoot = std::filesystem::path(oraclePath).parent_path();
        int cbMaximum = 0;
        int cbOverEight = 0;
        int crMaximum = 0;
        int crOverEight = 0;
        const int cbDifferent =
            CompareWords(cbBytes,
                         ExtractL16(ReadBytes(oracleRoot / "draw_00078_PS_260_cdfc6b55_rt0_960x540_f81.bin")),
                         cbMaximum,
                         cbOverEight);
        const int crDifferent =
            CompareWords(crBytes,
                         ExtractL16(ReadBytes(oracleRoot / "draw_00078_PS_260_cdfc6b55_rt1_960x540_f81.bin")),
                         crMaximum,
                         crOverEight);
        std::printf("P010 1080 raw->PS_260 Cb different=%d maximum=%d "
                    "Cr different=%d maximum=%d\n",
                    cbDifferent,
                    cbMaximum,
                    crDifferent,
                    crMaximum);

        ComPtr<ID3D11RenderTargetView> scaleXView;
        auto scaleX = CreateOutput(device, width, height / 2, DXGI_FORMAT_R16_UNORM, scaleXView);
        Require(ngu.RunNativeChromaScaleX(cbShaderView.Get(), scaleXView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleXShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(scaleX.Get(), nullptr, &scaleXShaderView)), "scale-X SRV");
        ComPtr<ID3D11RenderTargetView> scaleYView;
        auto scaleY = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, scaleYView);
        Require(ngu.RunNativeChromaScaleY(scaleXShaderView.Get(), scaleYView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleYShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(scaleY.Get(), nullptr, &scaleYShaderView)), "scale-Y SRV");
        const auto scaleXBytes = Download(device, context, scaleX.Get(), 2);
        const auto scaleYBytes = Download(device, context, scaleY.Get(), 2);
        int scaleXMaximum = 0;
        int scaleXOverEight = 0;
        int scaleYMaximum = 0;
        int scaleYOverEight = 0;
        const int scaleXDifferent =
            CompareWords(scaleXBytes,
                         ExtractL16(ReadBytes(oracleRoot / "draw_00079_PS_748_721dd04e_rt0_1920x540_f81.bin")),
                         scaleXMaximum,
                         scaleXOverEight);
        const int scaleYDifferent =
            CompareWords(scaleYBytes,
                         ExtractL16(ReadBytes(oracleRoot / "draw_00080_PS_748_4085ea73_rt0_1920x1080_f81.bin")),
                         scaleYMaximum,
                         scaleYOverEight);
        std::printf("P010 1080 raw->PS_748 X different=%d maximum=%d "
                    "Y different=%d maximum=%d\n",
                    scaleXDifferent,
                    scaleXMaximum,
                    scaleYDifferent,
                    scaleYMaximum);

        ComPtr<ID3D11RenderTargetView> denseView;
        auto dense = CreateOutput(device, width / 2 + 2, height / 2 + 2, DXGI_FORMAT_R16G16B16A16_FLOAT, denseView);
        Require(ngu.RunNativeChromaVeryHigh1080(cbShaderView.Get(), denseView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> denseShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(dense.Get(), nullptr, &denseShaderView)), "dense SRV");
        const auto denseBytes = Download(device, context, dense.Get(), 8);
        int denseMaximum = 0;
        int denseOverEight = 0;
        const int denseDifferent =
            CompareWords(denseBytes,
                         ReadBytes(oracleRoot / "draw_00110_PS_2672_9e6139d1_rt0_962x542_f113.bin"),
                         denseMaximum,
                         denseOverEight);

        ComPtr<ID3D11RenderTargetView> combine1View;
        auto combine1 = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, combine1View);
        Require(ngu.RunNativeChromaCombine1(scaleYShaderView.Get(), denseShaderView.Get(), combine1View.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> combine1ShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(combine1.Get(), nullptr, &combine1ShaderView)),
                "combine1 SRV");
        const auto combine1Bytes = Download(device, context, combine1.Get(), 2);
        int combine1Maximum = 0;
        int combine1OverEight = 0;
        const int combine1Different =
            CompareWords(combine1Bytes,
                         ExtractL16(ReadBytes(oracleRoot / "draw_00111_PS_564_500335c8_rt0_1920x1080_f81.bin")),
                         combine1Maximum,
                         combine1OverEight);
        std::printf("P010 1080 raw->PS_2672 different=%d maximum=%d "
                    "raw->PS_564 different=%d maximum=%d\n",
                    denseDifferent,
                    denseMaximum,
                    combine1Different,
                    combine1Maximum);

        ComPtr<ID3D11RenderTargetView> denseCrView;
        auto denseCr = CreateOutput(device, width / 2 + 2, height / 2 + 2, DXGI_FORMAT_R16G16B16A16_FLOAT, denseCrView);
        Require(ngu.RunNativeChromaVeryHigh1080(crShaderView.Get(), denseCrView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> denseCrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(denseCr.Get(), nullptr, &denseCrShaderView)),
                "Cr dense SRV");
        ComPtr<ID3D11RenderTargetView> scaleXCrView;
        auto scaleXCr = CreateOutput(device, width, height / 2, DXGI_FORMAT_R16_UNORM, scaleXCrView);
        Require(ngu.RunNativeChromaScaleX(crShaderView.Get(), scaleXCrView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleXCrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(scaleXCr.Get(), nullptr, &scaleXCrShaderView)),
                "Cr scale-X SRV");
        ComPtr<ID3D11RenderTargetView> scaleYCrView;
        auto scaleYCr = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, scaleYCrView);
        Require(ngu.RunNativeChromaScaleY(scaleXCrShaderView.Get(), scaleYCrView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleYCrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(scaleYCr.Get(), nullptr, &scaleYCrShaderView)),
                "Cr scale-Y SRV");
        ComPtr<ID3D11RenderTargetView> combine2View;
        auto combine2 = CreateOutput(device, width, height, DXGI_FORMAT_R16G16_UNORM, combine2View);
        Require(ngu.RunNativeChromaCombine2(
                    scaleYCrShaderView.Get(), combine1ShaderView.Get(), denseCrShaderView.Get(), combine2View.Get()),
                ngu.LastError());
        const auto combine2Bytes = Download(device, context, combine2.Get(), 4);
        int combine2Maximum = 0;
        int combine2OverEight = 0;
        const int combine2Different =
            CompareWords(combine2Bytes,
                         ReadBytes(oracleRoot / "draw_00144_PS_644_512f7798_rt0_1920x1080_f34.bin"),
                         combine2Maximum,
                         combine2OverEight);
        ComPtr<ID3D11ShaderResourceView> combine2ShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(combine2.Get(), nullptr, &combine2ShaderView)),
                "combine2 SRV");

        ComPtr<ID3D11RenderTargetView> realignView;
        auto realign = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, realignView);
        Require(ngu.RunNativeChromaRealign(combine2ShaderView.Get(), realignView.Get()), ngu.LastError());
        const auto realignBytes = Download(device, context, realign.Get(), 8);
        int realignMaximum = 0;
        int realignOverEight = 0;
        const int realignDifferent =
            CompareWords(realignBytes,
                         ReadBytes(oracleRoot / "draw_00145_PS_1516_11f763dd_rt0_1920x1080_f36.bin"),
                         realignMaximum,
                         realignOverEight);
        ComPtr<ID3D11ShaderResourceView> realignShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(realign.Get(), nullptr, &realignShaderView)), "realign SRV");

        ComPtr<ID3D11RenderTargetView> ycbcrView;
        auto ycbcr = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, ycbcrView);
        Require(ngu.RunNativeChromaYCbCr(lumaShaderView.Get(), realignShaderView.Get(), ycbcrView.Get()),
                ngu.LastError());
        const auto ycbcrBytes = Download(device, context, ycbcr.Get(), 8);
        ComPtr<ID3D11ShaderResourceView> ycbcrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(ycbcr.Get(), nullptr, &ycbcrShaderView)), "YCbCr SRV");
        int ycbcrMaximum = 0;
        int ycbcrOverEight = 0;
        const int ycbcrDifferent =
            CompareWords(ycbcrBytes,
                         ReadBytes(oracleRoot / "draw_00146_PS_396_e3199f71_rt0_1920x1080_f36.bin"),
                         ycbcrMaximum,
                         ycbcrOverEight);
        std::printf("P010 1080 raw->PS_644 different=%d maximum=%d "
                    "raw->PS_1516 different=%d maximum=%d "
                    "raw->PS_396 different=%d maximum=%d\n",
                    combine2Different,
                    combine2Maximum,
                    realignDifferent,
                    realignMaximum,
                    ycbcrDifferent,
                    ycbcrMaximum);

        ComPtr<ID3D11RenderTargetView> downXView;
        auto downX = CreateOutput(device, width / 2, height, DXGI_FORMAT_R16G16B16A16_UNORM, downXView);
        Require(ngu.RunNativeSuperResDownX1080(ycbcrShaderView.Get(), downXView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> downXShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(downX.Get(), nullptr, &downXShaderView)),
                "SuperRes down-X SRV");
        ComPtr<ID3D11RenderTargetView> downYView;
        auto downY = CreateOutput(device, width / 2, height / 2, DXGI_FORMAT_R16G16B16A16_UNORM, downYView);
        Require(ngu.RunNativeSuperResDownY1080(downXShaderView.Get(), downYView.Get()), ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> downYShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(downY.Get(), nullptr, &downYShaderView)),
                "SuperRes down-Y SRV");
        ComPtr<ID3D11RenderTargetView> diffView;
        auto diff = CreateOutput(device, width / 2, height / 2, DXGI_FORMAT_R16G16_FLOAT, diffView);
        Require(ngu.RunNativeSuperResDiff1080(chromaShaderView.Get(), downYShaderView.Get(), diffView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> diffShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(diff.Get(), nullptr, &diffShaderView)), "SuperRes diff SRV");

        const auto downXBytes = Download(device, context, downX.Get(), 8);
        const auto downYBytes = Download(device, context, downY.Get(), 8);
        const auto diffBytes = Download(device, context, diff.Get(), 4);
        int downXMaximum = 0;
        int downYMaximum = 0;
        int diffMaximum = 0;
        int ignored = 0;
        const int downXDifferent =
            CompareWords(downXBytes,
                         ReadBytes(oracleRoot / "draw_00147_PS_520_4a9505a4_rt0_960x1080_f36.bin"),
                         downXMaximum,
                         ignored);
        ignored = 0;
        const int downYDifferent =
            CompareWords(downYBytes,
                         ReadBytes(oracleRoot / "draw_00148_PS_520_4ee2414c_rt0_960x540_f36.bin"),
                         downYMaximum,
                         ignored);
        ignored = 0;
        // PS_284 writes G16R16F; word deltas diagnose raw-bit movement only.
        const int diffDifferent = CompareWords(
            diffBytes, ReadBytes(oracleRoot / "input_00150_PS_4412_c39b49cf_s3.bin"), diffMaximum, ignored);

        const auto targetBaseYBytes = ReadBytes(oracleRoot / "input_00154_PS_4576_b7830945_s0.bin");
        ComPtr<ID3D11RenderTargetView> targetBaseYView;
        auto targetBaseY = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, targetBaseYView);
        context->UpdateSubresource(targetBaseY.Get(), 0, nullptr, targetBaseYBytes.data(), width * 8, 0);
        ComPtr<ID3D11ShaderResourceView> targetBaseYShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(targetBaseY.Get(), nullptr, &targetBaseYShaderView)),
                "target base-Y SRV");

        ComPtr<ID3D11RenderTargetView> mainView;
        auto main = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, mainView);
        Require(ngu.RunNativeSuperResToRgbMain1080(
                    targetBaseYShaderView.Get(), ycbcrShaderView.Get(), diffShaderView.Get(), mainView.Get()),
                ngu.LastError());
        const auto mainBytes = Download(device, context, main.Get(), 8);
        int mainMaximum = 0;
        int mainOverEight = 0;
        const int mainDifferent =
            CompareWords(mainBytes,
                         ReadBytes(oracleRoot / "draw_00150_PS_4412_c39b49cf_rt0_1920x1080_f36.bin"),
                         mainMaximum,
                         mainOverEight);
        std::printf("P010 1080 SuperRes downX different=%d maximum=%d "
                    "downY different=%d maximum=%d "
                    "diff different=%d maximum=%d "
                    "main different=%d maximum=%d\n",
                    downXDifferent,
                    downXMaximum,
                    downYDifferent,
                    downYMaximum,
                    diffDifferent,
                    diffMaximum,
                    mainDifferent,
                    mainMaximum);

        std::vector<uint8_t> expanded(size_t(width) * height * 8);
        for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
        {
            const uint16_t value = uint16_t(lumaBytes[pixel * 2]) | (uint16_t(lumaBytes[pixel * 2 + 1]) << 8);
            for (int channel = 0; channel < 3; ++channel)
            {
                const size_t offset = pixel * 8 + channel * 2;
                expanded[offset] = uint8_t(value);
                expanded[offset + 1] = uint8_t(value >> 8);
            }
            expanded[pixel * 8 + 6] = 0xFF;
            expanded[pixel * 8 + 7] = 0xFF;
        }
        ComPtr<ID3D11RenderTargetView> expandedView;
        auto expandedTexture = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, expandedView);
        context->UpdateSubresource(expandedTexture.Get(), 0, nullptr, expanded.data(), width * 8, 0);
        const std::filesystem::path oracle(oraclePath);
        const std::string oracleName = oracle.filename().string();
        const size_t drawDigits = oracleName.find("draw_");
        Require(drawDigits != std::string::npos && oracleName.size() >= drawDigits + 10, "oracle draw identity");
        const std::string drawPrefix = oracleName.substr(drawDigits + 5, 5);
        const std::string targetYs0 = "input_" + drawPrefix + "_PS_4576_b7830945_s0.bin";
        int expandedMaximum = 0;
        int expandedOverEight = 0;
        const int expandedDifferent =
            CompareWords(expanded, ReadBytes(oracle.parent_path() / targetYs0), expandedMaximum, expandedOverEight);
        std::printf("P010 1080 expanded-Y vs target s0 different=%d "
                    "maximum=%d diff_gt_8=%d\n",
                    expandedDifferent,
                    expandedMaximum,
                    expandedOverEight);
        ComPtr<ID3D11ShaderResourceView> expandedShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(expandedTexture.Get(), nullptr, &expandedShaderView)),
                "expanded luma SRV");

        ComPtr<ID3D11RenderTargetView> chainView;
        auto chain = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, chainView);
        Require(ngu.RunNativeChromaVeryHigh1080Full(chromaShaderView.Get(),
                                                    lumaShaderView.Get(),
                                                    chromaShaderView.Get(),
                                                    expandedShaderView.Get(),
                                                    expandedShaderView.Get(),
                                                    chainView.Get(),
                                                    true),
                ngu.LastError());
        const auto chainBytes = Download(device, context, chain.Get(), 8);
        if (const char* dumpPath = std::getenv("NGU_P010_1080_RAW_PS4576_DUMP"))
        {
            std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
            Require(bool(dump), "open PS_4576 dump");
            dump.write(reinterpret_cast<const char*>(chainBytes.data()), std::streamsize(chainBytes.size()));
            Require(bool(dump), "write PS_4576 dump");
        }
        int maximum = 0;
        int overEight = 0;
        const int chainDifferent = CompareWords(chainBytes, ReadBytes(oraclePath), maximum, overEight);
        std::printf("P010 1080 raw->PS_4576 different=%d maximum=%d "
                    "diff_gt_8=%d\n",
                    chainDifferent,
                    maximum,
                    overEight);
        Require(maximum <= 8 && overEight == 0, "P010 raw 1080 PS_4576 oracle");

        if (const char* finalOraclePath = std::getenv("NGU_P010_1080_FINAL_ORACLE"))
        {
            Require(ngu.PrepareNativeNgu1080FromPlanes(gpu), ngu.LastError());
            ComPtr<ID3D11RenderTargetView> finalView;
            auto final = CreateOutput(device, 3840, 2160, DXGI_FORMAT_B8G8R8A8_UNORM, finalView);
            const auto finalStart = std::chrono::steady_clock::now();
            Require(ngu.RunNativeNgu1080FromPlanes(chromaShaderView.Get(), lumaShaderView.Get(), finalView.Get(), true),
                    ngu.LastError());
            pl_gpu_finish(gpu);
            const double finalMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - finalStart).count();
            const auto finalBytes = Download(device, context, final.Get(), 4);
            if (const char* dumpPath = std::getenv("NGU_P010_1080_FINAL_DUMP"))
            {
                std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
                Require(bool(dump), "open final dump");
                dump.write(reinterpret_cast<const char*>(finalBytes.data()), std::streamsize(finalBytes.size()));
                Require(bool(dump), "write final dump");
            }
            int finalMaximum = 0;
            int finalOverOne = 0;
            const int finalDifferent = CompareRgb8(finalBytes, ReadBytes(finalOraclePath), finalMaximum, finalOverOne);
            std::printf("P010 1080 raw->PS_1352 different=%d maximum=%d "
                        "diff_gt_1_pixels=%d %.3f ms/frame\n",
                        finalDifferent,
                        finalMaximum,
                        finalOverOne,
                        finalMilliseconds);
            Require(finalMaximum <= 1 && finalOverOne == 0, "P010 raw 1080 PS_1352 oracle");

            if (const char* benchFramesText = std::getenv("NGU_P010_1080_BENCH_FRAMES"))
            {
                const int benchFrames = std::atoi(benchFramesText);
                Require(benchFrames > 0, "P010 1080 benchmark frame count");
                std::vector<double> samples;
                samples.reserve(size_t(benchFrames));
                for (int frame = 0; frame < benchFrames; ++frame)
                {
                    const auto start = std::chrono::steady_clock::now();
                    Require(ngu.RunNativeNgu1080FromPlanes(
                                chromaShaderView.Get(), lumaShaderView.Get(), finalView.Get(), true),
                            ngu.LastError());
                    pl_gpu_finish(gpu);
                    samples.push_back(
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                }
                const auto [minimum, maximumSample] = std::minmax_element(samples.begin(), samples.end());
                double total = 0.0;
                for (double sample : samples)
                    total += sample;
                std::printf("P010 1080 benchmark frames=%d min=%.3f avg=%.3f "
                            "max=%.3f ms/frame\n",
                            benchFrames,
                            *minimum,
                            total / double(samples.size()),
                            *maximumSample);
            }
        }
    }
}

void TestP010Hdr(NguScaler& ngu,
                 pl_gpu gpu,
                 ID3D11Device* device,
                 ID3D11DeviceContext* context,
                 const std::filesystem::path& sourcePath,
                 const std::filesystem::path& oraclePath,
                 UINT width,
                 UINT height,
                 UINT paddedWidth,
                 HdrTestProfile profile = HdrTestProfile::PqBt2020)
{
    const UINT chromaWidth = width / 2;
    const UINT chromaHeight = height / 2;
    const auto source = ReadBytes(sourcePath);
    Require(source.size() == size_t(paddedWidth) * height * 3, "HDR P010 fixture size");
    auto texture = CreateDecoderTexture(device, source, paddedWidth, height, DXGI_FORMAT_P010);

    ComPtr<ID3D11RenderTargetView> lumaView;
    auto luma = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, lumaView);
    ComPtr<ID3D11RenderTargetView> chromaView;
    auto chroma = CreateOutput(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16G16B16A16_UNORM, chromaView);
    Require(ngu.PrepareNativeDecoderSource(gpu), ngu.LastError());
    Require(ngu.RunNativeDecoderSourceCopy(texture.Get(), 0, width, height, lumaView.Get(), chromaView.Get()),
            ngu.LastError());
    const auto chromaBytes = Download(device, context, chroma.Get(), 8);
    if (const char* dumpPath = std::getenv("NGU_P010_HDR_CHROMA_DUMP"))
    {
        std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
        Require(bool(dump), "open HDR chroma dump");
        dump.write(reinterpret_cast<const char*>(chromaBytes.data()), std::streamsize(chromaBytes.size()));
        Require(bool(dump), "write HDR chroma dump");
    }

    ComPtr<ID3D11ShaderResourceView> lumaShaderView;
    ComPtr<ID3D11ShaderResourceView> chromaShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(luma.Get(), nullptr, &lumaShaderView)), "HDR luma SRV");
    Require(SUCCEEDED(device->CreateShaderResourceView(chroma.Get(), nullptr, &chromaShaderView)), "HDR chroma SRV");

    NguSourceFrame frame = {};
    frame.width = width;
    frame.height = height;
    frame.lumaBits = 10;
    frame.chromaBits = 10;
    frame.chromaWidth = chromaWidth;
    frame.chromaHeight = chromaHeight;
    frame.chromaLayout = NguChromaLayout::SemiPlanar420;
    frame.color = pl_color_space_hdr10;
    frame.repr.sys = PL_COLOR_SYSTEM_BT_2020_NC;
    if (profile == HdrTestProfile::HlgBt2020)
        frame.color.transfer = PL_COLOR_TRC_HLG;
    else if (profile == HdrTestProfile::PqBt709)
    {
        frame.color.primaries = PL_COLOR_PRIM_BT_709;
        frame.repr.sys = PL_COLOR_SYSTEM_BT_709;
    }
    frame.repr.levels = PL_COLOR_LEVELS_LIMITED;
    frame.repr.bits = {16, 10, 0};
    frame.fullRange = false;
    frame.topDown = true;
    frame.luma = lumaShaderView.Get();
    frame.chroma = chromaShaderView.Get();

    const NguTargetPlan plan = ngu.PlanNativeVeryHigh(frame, 3840, 2160);
    Require(plan.hdr && !plan.noLuma && !plan.double720 && !plan.outputHook, "HDR planner boundary");
    Require(plan.outputWidth == int(width) && plan.outputHeight == int(height), "HDR PS_4576 source geometry");
    Require(ngu.PrepareNativeVeryHigh(gpu, frame, plan), ngu.LastError());

    ComPtr<ID3D11RenderTargetView> outputView;
    auto output = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, outputView);
    Require(ngu.RunNativeVeryHigh(frame, plan, outputView.Get()), ngu.LastError());
    const auto outputBytes = Download(device, context, output.Get(), 8);
    if (const char* dumpPath = std::getenv("NGU_P010_1080_HDR_PS4576_DUMP"))
    {
        std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
        Require(bool(dump), "open HDR PS_4576 dump");
        dump.write(reinterpret_cast<const char*>(outputBytes.data()), std::streamsize(outputBytes.size()));
        Require(bool(dump), "write HDR PS_4576 dump");
    }
    int maximum = 0;
    int overEight = 0;
    const int different = CompareWords(outputBytes, ReadBytes(oraclePath), maximum, overEight);
    std::printf("P010 HDR %ux%u raw->PS_4576 different=%d maximum=%d diff_gt_8=%d\n",
                width,
                height,
                different,
                maximum,
                overEight);
    Require(different == 0 && maximum == 0 && overEight == 0, "P010 HDR PS_4576 exact oracle");
}

void TestP010Hdr1080Trace(NguScaler& ngu,
                          pl_gpu gpu,
                          ID3D11Device* device,
                          ID3D11DeviceContext* context,
                          const std::filesystem::path& sourcePath,
                          const std::filesystem::path& captureRoot)
{
    constexpr UINT width = 1920;
    constexpr UINT height = 1080;
    constexpr UINT chromaWidth = width / 2;
    constexpr UINT chromaHeight = height / 2;
    const auto source = ReadBytes(sourcePath);
    Require(source.size() == size_t(2048) * height * 3, "P010 HDR trace source size");
    auto texture = CreateDecoderTexture(device, source, 2048, height, DXGI_FORMAT_P010);
    ComPtr<ID3D11RenderTargetView> lumaView;
    auto luma = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, lumaView);
    ComPtr<ID3D11RenderTargetView> chromaView;
    auto chroma = CreateOutput(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16G16B16A16_UNORM, chromaView);
    Require(ngu.PrepareNativeDecoderSource(gpu), ngu.LastError());
    Require(ngu.RunNativeDecoderSourceCopy(texture.Get(), 0, int(width), int(height), lumaView.Get(), chromaView.Get()),
            ngu.LastError());
    const auto lumaBytes = Download(device, context, luma.Get(), 2);
    ComPtr<ID3D11ShaderResourceView> lumaShaderView;
    ComPtr<ID3D11ShaderResourceView> chromaShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(luma.Get(), nullptr, &lumaShaderView)),
            "P010 HDR trace luma SRV");
    Require(SUCCEEDED(device->CreateShaderResourceView(chroma.Get(), nullptr, &chromaShaderView)),
            "P010 HDR trace chroma SRV");

    NguSourceFrame frame = {};
    frame.width = width;
    frame.height = height;
    frame.lumaBits = 10;
    frame.chromaBits = 10;
    frame.chromaWidth = chromaWidth;
    frame.chromaHeight = chromaHeight;
    frame.chromaLayout = NguChromaLayout::SemiPlanar420;
    frame.color = pl_color_space_hdr10;
    frame.repr.sys = PL_COLOR_SYSTEM_BT_2020_NC;
    frame.repr.levels = PL_COLOR_LEVELS_LIMITED;
    frame.repr.bits = {16, 10, 0};
    frame.fullRange = false;
    frame.topDown = true;
    frame.luma = lumaShaderView.Get();
    frame.chroma = chromaShaderView.Get();
    const NguTargetPlan plan = ngu.PlanNativeVeryHigh(frame, 3840, 2160);
    Require(plan.hdr && plan.outputWidth == int(width) && plan.outputHeight == int(height), "HDR trace planner");
    Require(ngu.PrepareNativeVeryHigh(gpu, frame, plan), ngu.LastError());

    ComPtr<ID3D11RenderTargetView> cbView;
    auto cb = CreateOutput(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16_UNORM, cbView);
    ComPtr<ID3D11RenderTargetView> crView;
    auto cr = CreateOutput(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16_UNORM, crView);
    Require(ngu.RunNativeChromaSplit(chromaShaderView.Get(), chromaWidth, chromaHeight, cbView.Get(), crView.Get()),
            ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> cbShaderView;
    ComPtr<ID3D11ShaderResourceView> crShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(cb.Get(), nullptr, &cbShaderView)), "HDR trace Cb SRV");
    Require(SUCCEEDED(device->CreateShaderResourceView(cr.Get(), nullptr, &crShaderView)), "HDR trace Cr SRV");

    auto compare = [&](const char* targetName, const std::vector<uint8_t>& actual, const char* label)
    {
        int maximum = 0;
        int overEight = 0;
        const int different = CompareWords(actual, ReadBytes(captureRoot / targetName), maximum, overEight);
        std::printf(
            "P010 1080 HDR trace %s different=%d maximum=%d diff_gt_8=%d\n", label, different, maximum, overEight);
        Require(different == 0 && maximum == 0 && overEight == 0, label);
    };

    ComPtr<ID3D11RenderTargetView> denseView;
    auto dense = CreateOutput(device, chromaWidth + 2, chromaHeight + 2, DXGI_FORMAT_R16G16B16A16_FLOAT, denseView);
    Require(ngu.RunNativeChromaVeryHigh1080(cbShaderView.Get(), denseView.Get()), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> denseShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(dense.Get(), nullptr, &denseShaderView)), "HDR trace dense SRV");

    ComPtr<ID3D11RenderTargetView> scaleXView;
    auto scaleX = CreateOutput(device, width, chromaHeight, DXGI_FORMAT_R16_UNORM, scaleXView);
    Require(ngu.RunNativeChromaScaleX(cbShaderView.Get(), scaleXView.Get()), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> scaleXShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(scaleX.Get(), nullptr, &scaleXShaderView)),
            "HDR trace scale X SRV");
    ComPtr<ID3D11RenderTargetView> scaleYView;
    auto scaleY = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, scaleYView);
    Require(ngu.RunNativeChromaScaleY(scaleXShaderView.Get(), scaleYView.Get()), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> scaleYShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(scaleY.Get(), nullptr, &scaleYShaderView)),
            "HDR trace scale Y SRV");

    ComPtr<ID3D11RenderTargetView> combine1View;
    auto combine1 = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, combine1View);
    Require(ngu.RunNativeChromaCombine1(scaleYShaderView.Get(), denseShaderView.Get(), combine1View.Get()),
            ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> combine1ShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(combine1.Get(), nullptr, &combine1ShaderView)),
            "HDR trace combine1 SRV");
    const auto combine1Bytes = Download(device, context, combine1.Get(), 2);
    {
        int maximum = 0;
        int overEight = 0;
        const int different = CompareWords(combine1Bytes,
                                           ExtractL16(ReadBytes(captureRoot / "input_00184_PS_644_512f7798_s2.bin")),
                                           maximum,
                                           overEight);
        std::printf("P010 1080 HDR trace PS_564 different=%d maximum=%d diff_gt_8=%d\n", different, maximum, overEight);
    }

    ComPtr<ID3D11RenderTargetView> denseCrView;
    auto denseCr = CreateOutput(device, chromaWidth + 2, chromaHeight + 2, DXGI_FORMAT_R16G16B16A16_FLOAT, denseCrView);
    Require(ngu.RunNativeChromaVeryHigh1080(crShaderView.Get(), denseCrView.Get()), ngu.LastError());
    const auto denseCrBytes = Download(device, context, denseCr.Get(), 8);
    compare("draw_00183_PS_2672_9e6139d1_rt0_962x542_f113.bin", denseCrBytes, "PS_2672 denseCr");
    ComPtr<ID3D11ShaderResourceView> denseCrShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(denseCr.Get(), nullptr, &denseCrShaderView)),
            "HDR trace dense Cr SRV");
    ComPtr<ID3D11RenderTargetView> scaleXCrView;
    auto scaleXCr = CreateOutput(device, width, chromaHeight, DXGI_FORMAT_R16_UNORM, scaleXCrView);
    Require(ngu.RunNativeChromaScaleX(crShaderView.Get(), scaleXCrView.Get()), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> scaleXCrShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(scaleXCr.Get(), nullptr, &scaleXCrShaderView)),
            "HDR trace scale X Cr SRV");
    ComPtr<ID3D11RenderTargetView> scaleYCrView;
    auto scaleYCr = CreateOutput(device, width, height, DXGI_FORMAT_R16_UNORM, scaleYCrView);
    Require(ngu.RunNativeChromaScaleY(scaleXCrShaderView.Get(), scaleYCrView.Get()), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> scaleYCrShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(scaleYCr.Get(), nullptr, &scaleYCrShaderView)),
            "HDR trace scale Y Cr SRV");
    const auto scaleYCrBytes = Download(device, context, scaleYCr.Get(), 2);
    {
        int maximum = 0;
        int overEight = 0;
        const int different = CompareWords(scaleYCrBytes,
                                           ExtractL16(ReadBytes(captureRoot / "input_00184_PS_644_512f7798_s0.bin")),
                                           maximum,
                                           overEight);
        std::printf(
            "P010 1080 HDR trace PS_748 Cr different=%d maximum=%d diff_gt_8=%d\n", different, maximum, overEight);
    }
    ComPtr<ID3D11RenderTargetView> combine2View;
    auto combine2 = CreateOutput(device, width, height, DXGI_FORMAT_R16G16_UNORM, combine2View);
    Require(ngu.RunNativeChromaCombine2(
                scaleYCrShaderView.Get(), combine1ShaderView.Get(), denseCrShaderView.Get(), combine2View.Get()),
            ngu.LastError());
    const auto combine2Bytes = Download(device, context, combine2.Get(), 4);
    compare("draw_00184_PS_644_512f7798_rt0_1920x1080_f34.bin", combine2Bytes, "PS_644");
    ComPtr<ID3D11ShaderResourceView> combine2ShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(combine2.Get(), nullptr, &combine2ShaderView)),
            "HDR trace combine2 SRV");

    ComPtr<ID3D11RenderTargetView> realignView;
    auto realign = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, realignView);
    Require(ngu.RunNativeChromaRealign(combine2ShaderView.Get(), realignView.Get()), ngu.LastError());
    const auto realignBytes = Download(device, context, realign.Get(), 8);
    compare("draw_00185_PS_1516_11f763dd_rt0_1920x1080_f36.bin", realignBytes, "PS_1516");
    ComPtr<ID3D11ShaderResourceView> realignShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(realign.Get(), nullptr, &realignShaderView)),
            "HDR trace realign SRV");

    ComPtr<ID3D11RenderTargetView> ycbcrView;
    auto ycbcr = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, ycbcrView);
    Require(ngu.RunNativeChromaYCbCr(lumaShaderView.Get(), realignShaderView.Get(), ycbcrView.Get()), ngu.LastError());
    const auto ycbcrBytes = Download(device, context, ycbcr.Get(), 8);
    compare("draw_00186_PS_396_e3199f71_rt0_1920x1080_f36.bin", ycbcrBytes, "PS_396");

    std::vector<uint8_t> expanded(size_t(width) * height * 8);
    for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
    {
        const uint16_t value = uint16_t(lumaBytes[pixel * 2]) | (uint16_t(lumaBytes[pixel * 2 + 1]) << 8);
        for (int channel = 0; channel < 3; ++channel)
            memcpy(expanded.data() + pixel * 8 + channel * 2, &value, sizeof(value));
        expanded[pixel * 8 + 6] = 0xFF;
        expanded[pixel * 8 + 7] = 0xFF;
    }
    ComPtr<ID3D11RenderTargetView> expandedView;
    auto expandedTexture = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, expandedView);
    context->UpdateSubresource(expandedTexture.Get(), 0, nullptr, expanded.data(), width * 8, 0);
    ComPtr<ID3D11ShaderResourceView> expandedShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(expandedTexture.Get(), nullptr, &expandedShaderView)),
            "HDR trace expanded Y SRV");
    ComPtr<ID3D11RenderTargetView> chainView;
    auto chain = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, chainView);
    Require(ngu.RunNativeChromaVeryHigh1080Full(chromaShaderView.Get(),
                                                lumaShaderView.Get(),
                                                chromaShaderView.Get(),
                                                expandedShaderView.Get(),
                                                expandedShaderView.Get(),
                                                chainView.Get(),
                                                true),
            ngu.LastError());
    const auto chainBytes = Download(device, context, chain.Get(), 8);
    compare("draw_00194_PS_4576_b7830945_rt0_1920x1080_f36.bin", chainBytes, "PS_4576");
}

void TestP016720Double(NguScaler& ngu,
                       pl_gpu gpu,
                       ID3D11Device* device,
                       ID3D11DeviceContext* context,
                       const std::filesystem::path& sourcePath,
                       const std::filesystem::path& oraclePath)
{
    constexpr UINT width = 1280;
    constexpr UINT height = 720;
    constexpr UINT chromaWidth = width / 2;
    constexpr UINT chromaHeight = height / 2;
    const auto raw = ReadBytes(sourcePath);
    Require(raw.size() == size_t(width) * height * 3, "P016 Double source size");
    const size_t lumaBytes = size_t(width) * height * 2;
    std::vector<uint8_t> y(raw.begin(), raw.begin() + lumaBytes);
    std::vector<uint8_t> uv(size_t(chromaWidth) * chromaHeight * 4);
    for (size_t i = 0; i < size_t(chromaWidth) * chromaHeight; ++i)
    {
        uv[i * 4] = raw[lumaBytes + i * 4];
        uv[i * 4 + 1] = raw[lumaBytes + i * 4 + 1];
        uv[i * 4 + 2] = raw[lumaBytes + i * 4 + 2];
        uv[i * 4 + 3] = raw[lumaBytes + i * 4 + 3];
    }
    auto yTexture = CreateSource(device, width, height, DXGI_FORMAT_R16_UNORM, 2, y);
    auto uvTexture = CreateSource(device, chromaWidth, chromaHeight, DXGI_FORMAT_R16G16_UNORM, 4, uv);
    ComPtr<ID3D11ShaderResourceView> yView;
    ComPtr<ID3D11ShaderResourceView> uvView;
    Require(SUCCEEDED(device->CreateShaderResourceView(yTexture.Get(), nullptr, &yView)), "P016 Y SRV");
    Require(SUCCEEDED(device->CreateShaderResourceView(uvTexture.Get(), nullptr, &uvView)), "P016 UV SRV");

    NguSourceFrame frame = {};
    frame.width = width;
    frame.height = height;
    frame.lumaBits = 16;
    frame.chromaBits = 16;
    frame.chromaWidth = chromaWidth;
    frame.chromaHeight = chromaHeight;
    frame.chromaLayout = NguChromaLayout::SemiPlanar420;
    frame.color = pl_color_space_bt709;
    frame.repr.sys = PL_COLOR_SYSTEM_BT_709;
    frame.repr.levels = PL_COLOR_LEVELS_LIMITED;
    frame.repr.bits = {16, 16, 0};
    frame.fullRange = false;
    frame.topDown = true;
    frame.luma = yView.Get();
    frame.chroma = uvView.Get();
    const auto plan = ngu.PlanNativeVeryHigh(frame, 2560, 1440);
    Require(plan.double720 && plan.preFinalRgb16 && !plan.outputHook && plan.outputWidth == 2560 &&
                plan.outputHeight == 1440,
            "P016 Double pre-final planner");
    if (const char* ps4576Oracle = std::getenv("NGU_P016_720_DOUBLE_PS4576_ORACLE"))
    {
        std::vector<uint8_t> expanded(size_t(width) * height * 8);
        for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel)
        {
            memcpy(expanded.data() + pixel * 8, y.data() + pixel * 2, 2);
            memcpy(expanded.data() + pixel * 8 + 2, y.data() + pixel * 2, 2);
            memcpy(expanded.data() + pixel * 8 + 4, y.data() + pixel * 2, 2);
            expanded[pixel * 8 + 6] = 0xFF;
            expanded[pixel * 8 + 7] = 0xFF;
        }
        auto expandedTexture = CreateSource(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, 8, expanded);
        ComPtr<ID3D11ShaderResourceView> expandedView;
        Require(SUCCEEDED(device->CreateShaderResourceView(expandedTexture.Get(), nullptr, &expandedView)),
                "P016 expanded Y SRV");
        Require(ngu.PrepareNativeChromaVeryHigh720Full(gpu), ngu.LastError());
        ComPtr<ID3D11RenderTargetView> ps4576View;
        auto ps4576 = CreateOutput(device, width, height, DXGI_FORMAT_R16G16B16A16_UNORM, ps4576View);
        Require(
            ngu.RunNativeChromaVeryHigh720Full(
                frame.chroma, frame.luma, frame.chroma, expandedView.Get(), expandedView.Get(), ps4576View.Get(), true),
            ngu.LastError());
        const auto ps4576Bytes = Download(device, context, ps4576.Get(), 8);
        int ps4576Maximum = 0;
        int ps4576OverEight = 0;
        const int ps4576Different = CompareWords(ps4576Bytes, ReadBytes(ps4576Oracle), ps4576Maximum, ps4576OverEight);
        std::printf("P016 720 Double PS4576 different=%d maximum=%d diff_gt_8=%d\n",
                    ps4576Different,
                    ps4576Maximum,
                    ps4576OverEight);
    }
    Require(ngu.PrepareNativeVeryHigh(gpu, frame, plan), ngu.LastError());
    ComPtr<ID3D11RenderTargetView> outputView;
    auto output = CreateOutput(device, 2560, 1440, DXGI_FORMAT_R16G16B16A16_UNORM, outputView);
    Require(ngu.RunNativeVeryHigh(frame, plan, outputView.Get()), ngu.LastError());
    const auto actual = Download(device, context, output.Get(), 8);
    if (const char* dumpPath = std::getenv("NGU_P016_720_DOUBLE_DUMP"))
    {
        std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
        Require(bool(dump), "open P016 Double dump");
        dump.write(reinterpret_cast<const char*>(actual.data()), std::streamsize(actual.size()));
        Require(bool(dump), "write P016 Double dump");
    }
    int maximum = 0;
    int overEight = 0;
    const auto expected = ReadBytes(oraclePath);
    const int different = CompareWords(actual, expected, maximum, overEight);
    std::printf("P016 720 Double PS908 different=%d maximum=%d diff_gt_8=%d\n", different, maximum, overEight);
    Require(different == 0 && maximum == 0 && overEight == 0, "P016 720 Double PS908 pre-final oracle");
}

void TestHdr720SuperResStage(NguScaler& ngu,
                             pl_gpu gpu,
                             ID3D11Device* device,
                             ID3D11DeviceContext* context,
                             const std::filesystem::path& captureRoot)
{
    NguSourceFrame frame = {};
    frame.width = 1280;
    frame.height = 720;
    frame.lumaBits = 10;
    frame.chromaBits = 10;
    frame.chromaWidth = 640;
    frame.chromaHeight = 360;
    frame.chromaLayout = NguChromaLayout::SemiPlanar420;
    frame.color = pl_color_space_hdr10;
    frame.repr.sys = PL_COLOR_SYSTEM_BT_2020_NC;
    frame.repr.levels = PL_COLOR_LEVELS_LIMITED;
    frame.repr.bits = {16, 10, 0};
    frame.topDown = true;
    const NguTargetPlan plan = ngu.PlanNativeVeryHigh(frame, 3840, 2160);
    Require(ngu.PrepareNativeVeryHigh(gpu, frame, plan), ngu.LastError());

    auto load = [&](const char* name, UINT width, UINT height, DXGI_FORMAT format, UINT bytesPerPixel)
    {
        auto texture = CreateSource(device, width, height, format, bytesPerPixel, ReadBytes(captureRoot / name));
        ComPtr<ID3D11ShaderResourceView> view;
        Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)), "capture SRV");
        return std::pair(std::move(texture), std::move(view));
    };
    const auto originalL16 = ReadBytes(captureRoot / "input_0082_PS_4576_b7830945_s0_1280x720_f81_1843200.bin");
    Require(originalL16.size() == size_t(1280) * 720 * 2, "HDR original L16 size");
    std::vector<uint8_t> originalExpanded(size_t(1280) * 720 * 8);
    for (size_t pixel = 0; pixel < size_t(1280) * 720; ++pixel)
    {
        for (size_t channel = 0; channel < 3; ++channel)
            memcpy(originalExpanded.data() + pixel * 8 + channel * 2, originalL16.data() + pixel * 2, 2);
        originalExpanded[pixel * 8 + 6] = 0xFF;
        originalExpanded[pixel * 8 + 7] = 0xFF;
    }
    auto originalTexture = CreateSource(device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM, 8, originalExpanded);
    ComPtr<ID3D11ShaderResourceView> originalView;
    Require(SUCCEEDED(device->CreateShaderResourceView(originalTexture.Get(), nullptr, &originalView)),
            "expanded original SRV");
    auto [workTexture, workView] =
        load("draw_0074_PS_396_e3199f71_rt0_1280x720_f36_7372800.bin", 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM, 8);
    auto [diffTexture, diffView] =
        load("draw_0077_PS_284_e8e2a8eb_rt0_640x360_f112_921600.bin", 640, 360, DXGI_FORMAT_R16G16_FLOAT, 4);
    ComPtr<ID3D11RenderTargetView> outputView;
    auto output = CreateOutput(device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM, outputView);
    Require(ngu.RunNativeSuperResToRgbMain(originalView.Get(), workView.Get(), diffView.Get(), outputView.Get()),
            ngu.LastError());
    const auto outputBytes = Download(device, context, output.Get(), 8);
    if (const char* dumpPath = std::getenv("NGU_PS4412_720_HDR_STAGE_DUMP"))
    {
        std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
        Require(bool(dump), "open PS_4412 stage dump");
        dump.write(reinterpret_cast<const char*>(outputBytes.data()), std::streamsize(outputBytes.size()));
        Require(bool(dump), "write PS_4412 stage dump");
    }
    int maximum = 0;
    int overEight = 0;
    const int different =
        CompareWords(outputBytes,
                     ReadBytes(captureRoot / "draw_0078_PS_4412_c39b49cf_rt0_1280x720_f36_7372800.bin"),
                     maximum,
                     overEight);
    std::printf("P010 720 HDR PS_4412 stage different=%d maximum=%d diff_gt_8=%d\n", different, maximum, overEight);
    Require(different == 0 && maximum == 0 && overEight == 0, "P010 720 HDR PS_4412 exact stage oracle");

    auto [packedTexture, packedView] =
        load("input_0006_PS_260_cdfc6b55_s0_640x360_f36_1843200.bin", 640, 360, DXGI_FORMAT_R16G16B16A16_UNORM, 8);
    auto rawYTexture = CreateSource(device, 1280, 720, DXGI_FORMAT_R16_UNORM, 2, originalL16);
    ComPtr<ID3D11ShaderResourceView> rawYView;
    Require(SUCCEEDED(device->CreateShaderResourceView(rawYTexture.Get(), nullptr, &rawYView)), "raw Y SRV");
    ComPtr<ID3D11RenderTargetView> fullOutputView;
    auto fullOutput = CreateOutput(device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM, fullOutputView);
    Require(ngu.RunNativeChromaVeryHigh720Full(packedView.Get(),
                                               rawYView.Get(),
                                               packedView.Get(),
                                               originalView.Get(),
                                               originalView.Get(),
                                               fullOutputView.Get(),
                                               true),
            ngu.LastError());
    const auto fullOutputBytes = Download(device, context, fullOutput.Get(), 8);
    auto checkStage =
        [&](int stage, UINT width, UINT height, DXGI_FORMAT format, const char* targetName, const char* label)
    {
        ComPtr<ID3D11RenderTargetView> stageView;
        auto stageOutput = CreateOutput(device, width, height, format, stageView);
        Require(ngu.CopyNativeChromaFullStage720(stage, stageView.Get()), ngu.LastError());
        const auto stageBytes =
            Download(device, context, stageOutput.Get(), format == DXGI_FORMAT_R16G16_UNORM ? 4 : 8);
        int stageMaximum = 0;
        int stageOverEight = 0;
        const int stageDifferent =
            CompareWords(stageBytes, ReadBytes(captureRoot / targetName), stageMaximum, stageOverEight);
        std::printf("P010 720 HDR %s different=%d maximum=%d diff_gt_8=%d\n",
                    label,
                    stageDifferent,
                    stageMaximum,
                    stageOverEight);
        Require(stageDifferent == 0 && stageMaximum == 0 && stageOverEight == 0, label);
    };
    checkStage(2,
               642,
               362,
               DXGI_FORMAT_R16G16B16A16_FLOAT,
               "draw_0038_PS_2672_9e6139d1_rt0_642x362_f113_1859232.bin",
               "PS_2672 Cb");
    checkStage(3,
               642,
               362,
               DXGI_FORMAT_R16G16B16A16_FLOAT,
               "draw_0071_PS_2672_9e6139d1_rt0_642x362_f113_1859232.bin",
               "PS_2672 Cr");
    checkStage(
        9, 1280, 720, DXGI_FORMAT_R16G16_UNORM, "draw_0072_PS_644_512f7798_rt0_1280x720_f34_3686400.bin", "PS_644");
    checkStage(10,
               1280,
               720,
               DXGI_FORMAT_R16G16B16A16_UNORM,
               "draw_0073_PS_1516_11f763dd_rt0_1280x720_f36_7372800.bin",
               "PS_1516");
    checkStage(11,
               1280,
               720,
               DXGI_FORMAT_R16G16B16A16_UNORM,
               "draw_0074_PS_396_e3199f71_rt0_1280x720_f36_7372800.bin",
               "PS_396");
    if (const char* dumpPath = std::getenv("NGU_PS4576_720_HDR_DUMP"))
    {
        std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
        Require(bool(dump), "open full PS_4576 dump");
        dump.write(reinterpret_cast<const char*>(fullOutputBytes.data()), std::streamsize(fullOutputBytes.size()));
        Require(bool(dump), "write full PS_4576 dump");
    }
    maximum = 0;
    overEight = 0;
    const int fullDifferent =
        CompareWords(fullOutputBytes,
                     ReadBytes(captureRoot / "draw_0082_PS_4576_b7830945_rt0_1280x720_f36_7372800.bin"),
                     maximum,
                     overEight);
    std::printf(
        "P010 720 HDR PS_260->PS_4576 different=%d maximum=%d diff_gt_8=%d\n", fullDifferent, maximum, overEight);
    Require(different == 0 && maximum == 0 && overEight == 0, "P010 720 HDR full chroma exact oracle");
}

void TestHdr1080ChromaStages(NguScaler& ngu,
                             pl_gpu gpu,
                             ID3D11Device* device,
                             ID3D11DeviceContext* context,
                             const std::filesystem::path& captureRoot)
{
    auto load = [&](const char* name, UINT width, UINT height, DXGI_FORMAT format, UINT bytesPerPixel)
    {
        auto texture = CreateSource(device, width, height, format, bytesPerPixel, ReadBytes(captureRoot / name));
        ComPtr<ID3D11ShaderResourceView> view;
        Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)), "HDR 1080 SRV");
        return std::pair(std::move(texture), std::move(view));
    };
    auto loadL16 = [&](const char* name, UINT width, UINT height)
    {
        auto l16 = ExtractL16(ReadBytes(captureRoot / name));
        auto texture = CreateSource(device, width, height, DXGI_FORMAT_R16_UNORM, 2, l16);
        ComPtr<ID3D11ShaderResourceView> view;
        Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)), "HDR 1080 L16 SRV");
        return std::pair(std::move(texture), std::move(view));
    };
    auto check = [&](ID3D11Texture2D* actual, const char* targetName, UINT bytesPerPixel, const char* label)
    {
        const auto actualBytes = Download(device, context, actual, bytesPerPixel);
        int maximum = 0;
        int overEight = 0;
        const int different = CompareWords(actualBytes, ReadBytes(captureRoot / targetName), maximum, overEight);
        std::printf("P010 1080 HDR %s different=%d maximum=%d diff_gt_8=%d\n", label, different, maximum, overEight);
        Require(different == 0 && maximum == 0 && overEight == 0, label);
    };

    Require(ngu.PrepareNativeChromaCombines(gpu), ngu.LastError());
    auto [add, addView] = loadL16("input_00184_PS_644_512f7798_s0.bin", 1920, 1080);
    auto [cb, cbView] = loadL16("input_00184_PS_644_512f7798_s2.bin", 1920, 1080);
    auto [nn, nnView] = load("input_00184_PS_644_512f7798_s3.bin", 962, 542, DXGI_FORMAT_R16G16B16A16_FLOAT, 8);
    ComPtr<ID3D11RenderTargetView> combineView;
    auto combine = CreateOutput(device, 1920, 1080, DXGI_FORMAT_R16G16_UNORM, combineView);
    Require(ngu.RunNativeChromaCombine2(addView.Get(), cbView.Get(), nnView.Get(), combineView.Get()), ngu.LastError());
    check(combine.Get(), "draw_00184_PS_644_512f7798_rt0_1920x1080_f34.bin", 4, "PS_644");

    Require(ngu.PrepareNativeChromaRealign(gpu), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> combineShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(combine.Get(), nullptr, &combineShaderView)),
            "HDR 1080 combine SRV");
    ComPtr<ID3D11RenderTargetView> realignView;
    auto realign = CreateOutput(device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM, realignView);
    Require(ngu.RunNativeChromaRealign(combineShaderView.Get(), realignView.Get()), ngu.LastError());
    check(realign.Get(), "draw_00185_PS_1516_11f763dd_rt0_1920x1080_f36.bin", 8, "PS_1516");

    Require(ngu.PrepareNativeChromaYCbCr(gpu), ngu.LastError());
    ComPtr<ID3D11ShaderResourceView> realignShaderView;
    Require(SUCCEEDED(device->CreateShaderResourceView(realign.Get(), nullptr, &realignShaderView)),
            "HDR 1080 realign SRV");
    auto [y, yView] = loadL16("input_00186_PS_396_e3199f71_s0.bin", 1920, 1080);
    ComPtr<ID3D11RenderTargetView> ycbcrView;
    auto ycbcr = CreateOutput(device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM, ycbcrView);
    Require(ngu.RunNativeChromaYCbCr(yView.Get(), realignShaderView.Get(), ycbcrView.Get()), ngu.LastError());
    check(ycbcr.Get(), "draw_00186_PS_396_e3199f71_rt0_1920x1080_f36.bin", 8, "PS_396");
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || argc == 3 || argc == 4 || argc == 5,
                "usage: test_ngu_source_import <p010> <p016> "
                "[p010-1080-ps4576-oracle] | "
                "--p010-720-hdr <p010> <ps4576-oracle> | "
                "--p010-720-hlg <p010> <ps4576-oracle> | "
                "--p010-720-bt709pq <p010> <ps4576-oracle> | "
                "--p010-1080-hdr <p010> <ps4576-oracle> | "
                "--p010-1080-hlg <p010> <ps4576-oracle> | "
                "--p010-1080-bt709pq <p010> <ps4576-oracle> | "
                "--p010-1080-hdr-trace <p010> <capture-root> | "
                "--p016-720-double <p016> <ps908-oracle> | "
                "--ps4576-720-hdr-stage <capture-root> | "
                "--ps4576-1080-hdr-stage <capture-root> | "
                "--nv12-split <i420> <capture-root> | "
                "--nv12-orientation | "
                "--nv12-final <i420-1080> <out.bmp>");
        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);

        NguScaler ngu;
        if (argc == 2 && std::strcmp(argv[1], "--nv12-orientation") == 0)
        {
            TestNv12Orientation(ngu, d3d->gpu, d3d->device, context.Get());
            std::puts("PASS_PRODUCT NV12 orientation");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--nv12-split") == 0)
        {
            Require(ngu.PrepareNativeChromaSplit(d3d->gpu), ngu.LastError());
            TestNv12Split(ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3]);
            std::puts("PASS_PRODUCT NV12 split");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--nv12-final") == 0)
        {
            TestNv12Final(ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3]);
            std::puts("PASS_PRODUCT NV12 final");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p010-720-hdr") == 0)
        {
            TestP010Hdr(ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3], 1280, 720, 1280);
            std::puts("PASS_PRODUCT P010 720 HDR PS_4576");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p010-720-hlg") == 0)
        {
            TestP010Hdr(ngu,
                        d3d->gpu,
                        d3d->device,
                        context.Get(),
                        argv[2],
                        argv[3],
                        1280,
                        720,
                        1280,
                        HdrTestProfile::HlgBt2020);
            std::puts("PASS_PRODUCT P010 720 BT.2020/HLG PS_4576");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p010-720-bt709pq") == 0)
        {
            TestP010Hdr(
                ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3], 1280, 720, 1280, HdrTestProfile::PqBt709);
            std::puts("PASS_PRODUCT P010 720 BT.709/PQ PS_4576");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p010-1080-hdr") == 0)
        {
            TestP010Hdr(ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3], 1920, 1080, 2048);
            std::puts("PASS_PRODUCT P010 1080 HDR PS_4576");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p010-1080-hlg") == 0)
        {
            TestP010Hdr(ngu,
                        d3d->gpu,
                        d3d->device,
                        context.Get(),
                        argv[2],
                        argv[3],
                        1920,
                        1080,
                        2048,
                        HdrTestProfile::HlgBt2020);
            std::puts("PASS_PRODUCT P010 1080 BT.2020/HLG PS_4576");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p010-1080-bt709pq") == 0)
        {
            TestP010Hdr(
                ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3], 1920, 1080, 2048, HdrTestProfile::PqBt709);
            std::puts("PASS_PRODUCT P010 1080 BT.709/PQ PS_4576");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p010-1080-hdr-trace") == 0)
        {
            TestP010Hdr1080Trace(ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3]);
            std::puts("PASS_PRODUCT P010 1080 HDR raw stage trace");
            return 0;
        }
        if (argc == 4 && std::strcmp(argv[1], "--p016-720-double") == 0)
        {
            TestP016720Double(ngu, d3d->gpu, d3d->device, context.Get(), argv[2], argv[3]);
            std::puts("PASS_PRODUCT P016 720 Double");
            return 0;
        }
        if (argc == 3 && std::strcmp(argv[1], "--ps4576-720-hdr-stage") == 0)
        {
            TestHdr720SuperResStage(ngu, d3d->gpu, d3d->device, context.Get(), argv[2]);
            std::puts("PASS_PRODUCT P010 720 HDR PS_4412 stage");
            return 0;
        }
        if (argc == 3 && std::strcmp(argv[1], "--ps4576-1080-hdr-stage") == 0)
        {
            TestHdr1080ChromaStages(ngu, d3d->gpu, d3d->device, context.Get(), argv[2]);
            std::puts("PASS_PRODUCT P010 1080 HDR chroma stages");
            return 0;
        }
        Require(ngu.PrepareNativeDecoderSource(d3d->gpu), ngu.LastError());
        TestPacked(ngu,
                   d3d->gpu,
                   d3d->device,
                   context.Get(),
                   argv[1],
                   DXGI_FORMAT_P010,
                   2048,
                   argc == 4 ? 1920 : 1280,
                   argc == 4 ? 1080 : 720,
                   argc == 4 ? "P010 1080" : "P010",
                   argc == 4 ? argv[3] : nullptr);
        if (argc == 3)
        {
            TestPacked(
                ngu, d3d->gpu, d3d->device, context.Get(), argv[2], DXGI_FORMAT_P010, 512, 320, 180, "P016-layout");
        }
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
