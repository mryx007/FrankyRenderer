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

int CompareWords(const std::vector<uint8_t>& actual,
                 const std::vector<uint8_t>& expected,
                 int& maximum,
                 int& overEight)
{
    Require(actual.size() == expected.size(), "word comparison size");
    int different = 0;
    maximum = 0;
    overEight = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left = uint16_t(actual[offset]) |
                              (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right = uint16_t(expected[offset]) |
                               (uint16_t(expected[offset + 1]) << 8);
        const int delta = std::abs(int(left) - int(right));
        if (delta)
            ++different;
        maximum = std::max(maximum, delta);
        overEight += delta > 8;
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

    if (oraclePath)
    {
        Require(width == 1920 && height == 1080,
                "P010 chain oracle requires 1080p");
        Require(ngu.PrepareNativeChromaVeryHigh1080Full(gpu),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> lumaShaderView;
        ComPtr<ID3D11ShaderResourceView> chromaShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    luma.Get(), nullptr, &lumaShaderView)),
                "luma SRV");
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    chroma.Get(), nullptr, &chromaShaderView)),
                "chroma SRV");

        ComPtr<ID3D11RenderTargetView> cbView;
        auto cb = CreateOutput(
            device, width / 2, height / 2, DXGI_FORMAT_R16_UNORM, cbView);
        ComPtr<ID3D11RenderTargetView> crView;
        auto cr = CreateOutput(
            device, width / 2, height / 2, DXGI_FORMAT_R16_UNORM, crView);
        Require(ngu.RunNativeChromaSplit(chromaShaderView.Get(),
                                         width / 2,
                                         height / 2,
                                         cbView.Get(),
                                         crView.Get()),
                ngu.LastError());
        const auto cbBytes = Download(device, context, cb.Get(), 2);
        const auto crBytes = Download(device, context, cr.Get(), 2);
        ComPtr<ID3D11ShaderResourceView> cbShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    cb.Get(), nullptr, &cbShaderView)),
                "Cb SRV");
        ComPtr<ID3D11ShaderResourceView> crShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    cr.Get(), nullptr, &crShaderView)),
                "Cr SRV");
        const std::filesystem::path oracleRoot =
            std::filesystem::path(oraclePath).parent_path();
        int cbMaximum = 0;
        int cbOverEight = 0;
        int crMaximum = 0;
        int crOverEight = 0;
        const int cbDifferent = CompareWords(
            cbBytes,
            ExtractL16(ReadBytes(
                oracleRoot /
                "draw_00078_PS_260_cdfc6b55_rt0_960x540_f81.bin")),
            cbMaximum,
            cbOverEight);
        const int crDifferent = CompareWords(
            crBytes,
            ExtractL16(ReadBytes(
                oracleRoot /
                "draw_00078_PS_260_cdfc6b55_rt1_960x540_f81.bin")),
            crMaximum,
            crOverEight);
        std::printf("P010 1080 raw->PS_260 Cb different=%d maximum=%d "
                    "Cr different=%d maximum=%d\n",
                    cbDifferent,
                    cbMaximum,
                    crDifferent,
                    crMaximum);

        ComPtr<ID3D11RenderTargetView> scaleXView;
        auto scaleX = CreateOutput(
            device, width, height / 2, DXGI_FORMAT_R16_UNORM, scaleXView);
        Require(ngu.RunNativeChromaScaleX(cbShaderView.Get(),
                                          scaleXView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleXShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    scaleX.Get(), nullptr, &scaleXShaderView)),
                "scale-X SRV");
        ComPtr<ID3D11RenderTargetView> scaleYView;
        auto scaleY = CreateOutput(
            device, width, height, DXGI_FORMAT_R16_UNORM, scaleYView);
        Require(ngu.RunNativeChromaScaleY(scaleXShaderView.Get(),
                                          scaleYView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleYShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    scaleY.Get(), nullptr, &scaleYShaderView)),
                "scale-Y SRV");
        const auto scaleXBytes = Download(device, context, scaleX.Get(), 2);
        const auto scaleYBytes = Download(device, context, scaleY.Get(), 2);
        int scaleXMaximum = 0;
        int scaleXOverEight = 0;
        int scaleYMaximum = 0;
        int scaleYOverEight = 0;
        const int scaleXDifferent = CompareWords(
            scaleXBytes,
            ExtractL16(ReadBytes(
                oracleRoot /
                "draw_00079_PS_748_721dd04e_rt0_1920x540_f81.bin")),
            scaleXMaximum,
            scaleXOverEight);
        const int scaleYDifferent = CompareWords(
            scaleYBytes,
            ExtractL16(ReadBytes(
                oracleRoot /
                "draw_00080_PS_748_4085ea73_rt0_1920x1080_f81.bin")),
            scaleYMaximum,
            scaleYOverEight);
        std::printf("P010 1080 raw->PS_748 X different=%d maximum=%d "
                    "Y different=%d maximum=%d\n",
                    scaleXDifferent,
                    scaleXMaximum,
                    scaleYDifferent,
                    scaleYMaximum);

        ComPtr<ID3D11RenderTargetView> denseView;
        auto dense = CreateOutput(device,
                                  width / 2 + 2,
                                  height / 2 + 2,
                                  DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  denseView);
        Require(ngu.RunNativeChromaVeryHigh1080(cbShaderView.Get(),
                                                denseView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> denseShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    dense.Get(), nullptr, &denseShaderView)),
                "dense SRV");
        const auto denseBytes = Download(device, context, dense.Get(), 8);
        int denseMaximum = 0;
        int denseOverEight = 0;
        const int denseDifferent = CompareWords(
            denseBytes,
            ReadBytes(
                oracleRoot /
                "draw_00110_PS_2672_9e6139d1_rt0_962x542_f113.bin"),
            denseMaximum,
            denseOverEight);

        ComPtr<ID3D11RenderTargetView> combine1View;
        auto combine1 = CreateOutput(
            device, width, height, DXGI_FORMAT_R16_UNORM, combine1View);
        Require(ngu.RunNativeChromaCombine1(scaleYShaderView.Get(),
                                            denseShaderView.Get(),
                                            combine1View.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> combine1ShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    combine1.Get(), nullptr, &combine1ShaderView)),
                "combine1 SRV");
        const auto combine1Bytes =
            Download(device, context, combine1.Get(), 2);
        int combine1Maximum = 0;
        int combine1OverEight = 0;
        const int combine1Different = CompareWords(
            combine1Bytes,
            ExtractL16(ReadBytes(
                oracleRoot /
                "draw_00111_PS_564_500335c8_rt0_1920x1080_f81.bin")),
            combine1Maximum,
            combine1OverEight);
        std::printf("P010 1080 raw->PS_2672 different=%d maximum=%d "
                    "raw->PS_564 different=%d maximum=%d\n",
                    denseDifferent,
                    denseMaximum,
                    combine1Different,
                    combine1Maximum);

        ComPtr<ID3D11RenderTargetView> denseCrView;
        auto denseCr = CreateOutput(device,
                                    width / 2 + 2,
                                    height / 2 + 2,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                                    denseCrView);
        Require(ngu.RunNativeChromaVeryHigh1080(crShaderView.Get(),
                                                denseCrView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> denseCrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    denseCr.Get(), nullptr, &denseCrShaderView)),
                "Cr dense SRV");
        ComPtr<ID3D11RenderTargetView> scaleXCrView;
        auto scaleXCr = CreateOutput(
            device, width, height / 2, DXGI_FORMAT_R16_UNORM, scaleXCrView);
        Require(ngu.RunNativeChromaScaleX(crShaderView.Get(),
                                          scaleXCrView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleXCrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    scaleXCr.Get(), nullptr, &scaleXCrShaderView)),
                "Cr scale-X SRV");
        ComPtr<ID3D11RenderTargetView> scaleYCrView;
        auto scaleYCr = CreateOutput(
            device, width, height, DXGI_FORMAT_R16_UNORM, scaleYCrView);
        Require(ngu.RunNativeChromaScaleY(scaleXCrShaderView.Get(),
                                          scaleYCrView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> scaleYCrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    scaleYCr.Get(), nullptr, &scaleYCrShaderView)),
                "Cr scale-Y SRV");
        ComPtr<ID3D11RenderTargetView> combine2View;
        auto combine2 = CreateOutput(device,
                                     width,
                                     height,
                                     DXGI_FORMAT_R16G16_UNORM,
                                     combine2View);
        Require(ngu.RunNativeChromaCombine2(scaleYCrShaderView.Get(),
                                            combine1ShaderView.Get(),
                                            denseCrShaderView.Get(),
                                            combine2View.Get()),
                ngu.LastError());
        const auto combine2Bytes =
            Download(device, context, combine2.Get(), 4);
        int combine2Maximum = 0;
        int combine2OverEight = 0;
        const int combine2Different = CompareWords(
            combine2Bytes,
            ReadBytes(
                oracleRoot /
                "draw_00144_PS_644_512f7798_rt0_1920x1080_f34.bin"),
            combine2Maximum,
            combine2OverEight);
        ComPtr<ID3D11ShaderResourceView> combine2ShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    combine2.Get(), nullptr, &combine2ShaderView)),
                "combine2 SRV");

        ComPtr<ID3D11RenderTargetView> realignView;
        auto realign = CreateOutput(device,
                                    width,
                                    height,
                                    DXGI_FORMAT_R16G16B16A16_UNORM,
                                    realignView);
        Require(ngu.RunNativeChromaRealign(combine2ShaderView.Get(),
                                           realignView.Get()),
                ngu.LastError());
        const auto realignBytes =
            Download(device, context, realign.Get(), 8);
        int realignMaximum = 0;
        int realignOverEight = 0;
        const int realignDifferent = CompareWords(
            realignBytes,
            ReadBytes(
                oracleRoot /
                "draw_00145_PS_1516_11f763dd_rt0_1920x1080_f36.bin"),
            realignMaximum,
            realignOverEight);
        ComPtr<ID3D11ShaderResourceView> realignShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    realign.Get(), nullptr, &realignShaderView)),
                "realign SRV");

        ComPtr<ID3D11RenderTargetView> ycbcrView;
        auto ycbcr = CreateOutput(device,
                                  width,
                                  height,
                                  DXGI_FORMAT_R16G16B16A16_UNORM,
                                  ycbcrView);
        Require(ngu.RunNativeChromaYCbCr(lumaShaderView.Get(),
                                         realignShaderView.Get(),
                                         ycbcrView.Get()),
                ngu.LastError());
        const auto ycbcrBytes =
            Download(device, context, ycbcr.Get(), 8);
        ComPtr<ID3D11ShaderResourceView> ycbcrShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    ycbcr.Get(), nullptr, &ycbcrShaderView)),
                "YCbCr SRV");
        int ycbcrMaximum = 0;
        int ycbcrOverEight = 0;
        const int ycbcrDifferent = CompareWords(
            ycbcrBytes,
            ReadBytes(
                oracleRoot /
                "draw_00146_PS_396_e3199f71_rt0_1920x1080_f36.bin"),
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
        auto downX = CreateOutput(device,
                                  width / 2,
                                  height,
                                  DXGI_FORMAT_R16G16B16A16_UNORM,
                                  downXView);
        Require(ngu.RunNativeSuperResDownX1080(ycbcrShaderView.Get(),
                                               downXView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> downXShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    downX.Get(), nullptr, &downXShaderView)),
                "SuperRes down-X SRV");
        ComPtr<ID3D11RenderTargetView> downYView;
        auto downY = CreateOutput(device,
                                  width / 2,
                                  height / 2,
                                  DXGI_FORMAT_R16G16B16A16_UNORM,
                                  downYView);
        Require(ngu.RunNativeSuperResDownY1080(downXShaderView.Get(),
                                               downYView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> downYShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    downY.Get(), nullptr, &downYShaderView)),
                "SuperRes down-Y SRV");
        ComPtr<ID3D11RenderTargetView> diffView;
        auto diff = CreateOutput(device,
                                 width / 2,
                                 height / 2,
                                 DXGI_FORMAT_R16G16_FLOAT,
                                 diffView);
        Require(ngu.RunNativeSuperResDiff1080(chromaShaderView.Get(),
                                              downYShaderView.Get(),
                                              diffView.Get()),
                ngu.LastError());
        ComPtr<ID3D11ShaderResourceView> diffShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    diff.Get(), nullptr, &diffShaderView)),
                "SuperRes diff SRV");

        const auto downXBytes = Download(device, context, downX.Get(), 8);
        const auto downYBytes = Download(device, context, downY.Get(), 8);
        const auto diffBytes = Download(device, context, diff.Get(), 4);
        int downXMaximum = 0;
        int downYMaximum = 0;
        int diffMaximum = 0;
        int ignored = 0;
        const int downXDifferent = CompareWords(
            downXBytes,
            ReadBytes(
                oracleRoot /
                "draw_00147_PS_520_4a9505a4_rt0_960x1080_f36.bin"),
            downXMaximum,
            ignored);
        ignored = 0;
        const int downYDifferent = CompareWords(
            downYBytes,
            ReadBytes(
                oracleRoot /
                "draw_00148_PS_520_4ee2414c_rt0_960x540_f36.bin"),
            downYMaximum,
            ignored);
        ignored = 0;
        // PS_284 writes G16R16F; word deltas diagnose raw-bit movement only.
        const int diffDifferent = CompareWords(
            diffBytes,
            ReadBytes(
                oracleRoot /
                "input_00150_PS_4412_c39b49cf_s3.bin"),
            diffMaximum,
            ignored);

        const auto targetBaseYBytes = ReadBytes(
            oracleRoot /
            "input_00154_PS_4576_b7830945_s0.bin");
        ComPtr<ID3D11RenderTargetView> targetBaseYView;
        auto targetBaseY =
            CreateOutput(device,
                         width,
                         height,
                         DXGI_FORMAT_R16G16B16A16_UNORM,
                         targetBaseYView);
        context->UpdateSubresource(targetBaseY.Get(),
                                   0,
                                   nullptr,
                                   targetBaseYBytes.data(),
                                   width * 8,
                                   0);
        ComPtr<ID3D11ShaderResourceView> targetBaseYShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    targetBaseY.Get(),
                    nullptr,
                    &targetBaseYShaderView)),
                "target base-Y SRV");

        ComPtr<ID3D11RenderTargetView> mainView;
        auto main = CreateOutput(device,
                                 width,
                                 height,
                                 DXGI_FORMAT_R16G16B16A16_UNORM,
                                 mainView);
        Require(ngu.RunNativeSuperResToRgbMain1080(
                    targetBaseYShaderView.Get(),
                    ycbcrShaderView.Get(),
                    diffShaderView.Get(),
                    mainView.Get()),
                ngu.LastError());
        const auto mainBytes = Download(device, context, main.Get(), 8);
        int mainMaximum = 0;
        int mainOverEight = 0;
        const int mainDifferent = CompareWords(
            mainBytes,
            ReadBytes(
                oracleRoot /
                "draw_00150_PS_4412_c39b49cf_rt0_1920x1080_f36.bin"),
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
            const uint16_t value =
                uint16_t(lumaBytes[pixel * 2]) |
                (uint16_t(lumaBytes[pixel * 2 + 1]) << 8);
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
        auto expandedTexture =
            CreateOutput(device,
                         width,
                         height,
                         DXGI_FORMAT_R16G16B16A16_UNORM,
                         expandedView);
        context->UpdateSubresource(expandedTexture.Get(),
                                   0,
                                   nullptr,
                                   expanded.data(),
                                   width * 8,
                                   0);
        const std::filesystem::path oracle(oraclePath);
        const std::string oracleName = oracle.filename().string();
        const size_t drawDigits = oracleName.find("draw_");
        Require(drawDigits != std::string::npos &&
                    oracleName.size() >= drawDigits + 10,
                "oracle draw identity");
        const std::string drawPrefix =
            oracleName.substr(drawDigits + 5, 5);
        const std::string targetYs0 =
            "input_" + drawPrefix +
            "_PS_4576_b7830945_s0.bin";
        int expandedMaximum = 0;
        int expandedOverEight = 0;
        const int expandedDifferent =
            CompareWords(
                expanded,
                ReadBytes(
                    oracle.parent_path() / targetYs0),
                expandedMaximum,
                expandedOverEight);
        std::printf("P010 1080 expanded-Y vs target s0 different=%d "
                    "maximum=%d diff_gt_8=%d\n",
                    expandedDifferent,
                    expandedMaximum,
                    expandedOverEight);
        ComPtr<ID3D11ShaderResourceView> expandedShaderView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    expandedTexture.Get(), nullptr, &expandedShaderView)),
                "expanded luma SRV");

        ComPtr<ID3D11RenderTargetView> chainView;
        auto chain = CreateOutput(device,
                                  width,
                                  height,
                                  DXGI_FORMAT_R16G16B16A16_UNORM,
                                  chainView);
        Require(ngu.RunNativeChromaVeryHigh1080Full(
                    chromaShaderView.Get(),
                    lumaShaderView.Get(),
                    chromaShaderView.Get(),
                    expandedShaderView.Get(),
                    expandedShaderView.Get(),
                    chainView.Get(),
                    true),
                ngu.LastError());
        const auto chainBytes = Download(device, context, chain.Get(), 8);
        if (const char* dumpPath =
                std::getenv("NGU_P010_1080_RAW_PS4576_DUMP"))
        {
            std::ofstream dump(dumpPath, std::ios::binary | std::ios::trunc);
            Require(bool(dump), "open PS_4576 dump");
            dump.write(reinterpret_cast<const char*>(chainBytes.data()),
                       std::streamsize(chainBytes.size()));
            Require(bool(dump), "write PS_4576 dump");
        }
        int maximum = 0;
        int overEight = 0;
        const int chainDifferent =
            CompareWords(chainBytes, ReadBytes(oraclePath), maximum, overEight);
        std::printf("P010 1080 raw->PS_4576 different=%d maximum=%d "
                    "diff_gt_8=%d\n",
                    chainDifferent,
                    maximum,
                    overEight);
        Require(maximum <= 8 && overEight == 0,
                "P010 raw 1080 PS_4576 oracle");
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3 || argc == 4,
                "usage: test_ngu_source_import <p010> <p016> "
                "[p010-1080-ps4576-oracle]");
        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);

        NguScaler ngu;
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
        TestPacked(ngu,
                   d3d->gpu,
                   d3d->device,
                   context.Get(),
                   argv[2],
                   DXGI_FORMAT_P010,
                   512,
                   320,
                   180,
                   "P016-layout");
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
