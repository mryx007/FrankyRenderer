#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
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
constexpr UINT kSourceWidth = 1280;
constexpr UINT kSourceHeight = 720;
constexpr UINT kOutputWidth = 2560;
constexpr UINT kOutputHeight = 1440;
constexpr size_t kPixelBytes = 8;

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
    Require(size_t(file.tellg()) == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),
                           std::streamsize(expected))),
            "read binary");
    return bytes;
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      UINT width,
                                      UINT height,
                                      DXGI_FORMAT format,
                                      UINT bind,
                                      const void* initial = nullptr)
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
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = initial;
    data.SysMemPitch = UINT(width * kPixelBytes);
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, initial ? &data : nullptr, &texture)),
            "create texture");
    return texture;
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
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)),
            "create staging texture");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(
                staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging texture");
    const UINT pixelBytes =
        desc.Format == DXGI_FORMAT_R16_UNORM ? 2
        : desc.Format == DXGI_FORMAT_R16G16_UNORM ? 4
        : desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4
        : 8;
    const size_t rowBytes = size_t(desc.Width) * pixelBytes;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
    {
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        size_t(y) * mapped.RowPitch,
                    rowBytes);
    }
    context->Unmap(staging.Get(), 0);
    return bytes;
}

int CountComponents(const std::vector<uint8_t>& actual,
                    const std::vector<uint8_t>& expected,
                    int& maximum)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0;
    maximum = 0;
    for (size_t i = 0; i < actual.size(); i += 2)
    {
        const uint16_t left =
            uint16_t(actual[i]) | (uint16_t(actual[i + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[i]) | (uint16_t(expected[i + 1]) << 8);
        if (left != right)
        {
            ++different;
            maximum = std::max(
                maximum, std::abs(int(left) - int(right)));
        }
    }
    return different;
}

std::vector<uint8_t> FirstComponent(
    const std::vector<uint8_t>& rgba16)
{
    Require(rgba16.size() % 8 == 0, "first-component input");
    std::vector<uint8_t> component(rgba16.size() / 4);
    for (size_t pixel = 0; pixel < rgba16.size() / 8; ++pixel)
    {
        component[pixel * 2] = rgba16[pixel * 8];
        component[pixel * 2 + 1] = rgba16[pixel * 8 + 1];
    }
    return component;
}

int CountPixelDifferences(const std::vector<uint8_t>& actual,
                          const std::vector<uint8_t>& expected,
                          int& maximum,
                          int& overOne)
{
    Require(actual.size() == expected.size() && actual.size() % 4 == 0,
            "pixel comparison size");
    maximum = 0;
    overOne = 0;
    int different = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 4)
    {
        bool pixelOverOne = false;
        for (size_t channel = 0; channel < 4; ++channel)
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
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3,
                "usage: test_ngu_double_720 <luma_canonical27> <actual.bin>");
        const std::filesystem::path root(argv[1]);
        const std::filesystem::path actualPath(argv[2]);

        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Require(bool(d3d), "create D3D11 backend");
        pl_d3d11 d3d11 = pl_d3d11_get(d3d->gpu);
        Require(bool(d3d11) && bool(d3d11->device), "D3D11 device");
        ID3D11Device* device = d3d11->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        auto sourceBytes =
            ReadBytes(root / "input_03894_PS_460_bfe50921_s0.bin",
                      size_t(kSourceWidth) * kSourceHeight * kPixelBytes);
        auto expected =
            ReadBytes(root /
                          "draw_03917_PS_548_d6670d4b_rt0_2560x1440_f36.bin",
                      size_t(kOutputWidth) * kOutputHeight * kPixelBytes);

        auto source = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_SHADER_RESOURCE,
            sourceBytes.data());
        auto output = CreateTexture(
            device,
            kOutputWidth,
            kOutputHeight,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11ShaderResourceView> sourceView;
        ComPtr<ID3D11RenderTargetView> outputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    source.Get(), nullptr, &sourceView)),
                "source SRV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    output.Get(), nullptr, &outputView)),
                "output RTV");

        NguScaler ngu;
        Require(ngu.PrepareNativeLuma720Double(d3d->gpu),
                ngu.LastError());
        Require(ngu.RunNativeLuma720DoubleIntermediate(
                    sourceView.Get(),
                    kSourceWidth,
                    kSourceHeight,
                    outputView.Get()),
                ngu.LastError());

        auto splitOutput = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto splitCbCrOutput = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto scaleXOutput = CreateTexture(
            device,
            kOutputWidth,
            kSourceHeight,
            DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto scaleYOutput = CreateTexture(
            device,
            kOutputWidth,
            kOutputHeight,
            DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto initial0Output = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto initial1Output = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto initial2Output = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto finalLayer0Output = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto finalLayer1Output = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto finalLayer2Output = CreateTexture(
            device,
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto denseOutput = CreateTexture(
            device,
            kSourceWidth + 2,
            kSourceHeight + 2,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto addOutput = CreateTexture(
            device,
            kOutputWidth,
            kOutputHeight,
            DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto chromaXOutput = CreateTexture(
            device,
            kOutputWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto chromaYOutput = CreateTexture(
            device,
            kOutputWidth,
            kOutputHeight,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto finalOutput = CreateTexture(
            device,
            kOutputWidth,
            kOutputHeight,
            DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> splitView;
        ComPtr<ID3D11RenderTargetView> splitCbCrView;
        ComPtr<ID3D11RenderTargetView> scaleXView;
        ComPtr<ID3D11RenderTargetView> scaleYView;
        ComPtr<ID3D11RenderTargetView> initial0View;
        ComPtr<ID3D11RenderTargetView> initial1View;
        ComPtr<ID3D11RenderTargetView> initial2View;
        ComPtr<ID3D11RenderTargetView> finalLayer0View;
        ComPtr<ID3D11RenderTargetView> finalLayer1View;
        ComPtr<ID3D11RenderTargetView> finalLayer2View;
        ComPtr<ID3D11RenderTargetView> denseView;
        ComPtr<ID3D11RenderTargetView> addView;
        ComPtr<ID3D11RenderTargetView> chromaXView;
        ComPtr<ID3D11RenderTargetView> chromaYView;
        ComPtr<ID3D11RenderTargetView> finalView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    splitOutput.Get(), nullptr, &splitView)),
                "split RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    splitCbCrOutput.Get(), nullptr, &splitCbCrView)),
                "split CbCr RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    scaleXOutput.Get(), nullptr, &scaleXView)),
                "scale X RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    scaleYOutput.Get(), nullptr, &scaleYView)),
                "scale Y RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    initial0Output.Get(), nullptr, &initial0View)),
                "initial0 RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    initial1Output.Get(), nullptr, &initial1View)),
                "initial1 RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    initial2Output.Get(), nullptr, &initial2View)),
                "initial2 RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    finalLayer0Output.Get(), nullptr, &finalLayer0View)),
                "finalLayer0 RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    finalLayer1Output.Get(), nullptr, &finalLayer1View)),
                "finalLayer1 RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    finalLayer2Output.Get(), nullptr, &finalLayer2View)),
                "finalLayer2 RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    denseOutput.Get(), nullptr, &denseView)),
                "dense RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    addOutput.Get(), nullptr, &addView)),
                "add RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    chromaXOutput.Get(), nullptr, &chromaXView)),
                "chroma X RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    chromaYOutput.Get(), nullptr, &chromaYView)),
                "chroma Y RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    finalOutput.Get(), nullptr, &finalView)),
                "final RTV");
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::Add, addView.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::ChromaX, chromaXView.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::ChromaY, chromaYView.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::SplitLuma, splitView.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::SplitCbCr, splitCbCrView.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::ScaleX, scaleXView.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::ScaleY, scaleYView.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::Initial0, initial0View.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::Initial1, initial1View.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::Initial2, initial2View.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::FinalLayer0, finalLayer0View.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::FinalLayer1, finalLayer1View.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::FinalLayer2, finalLayer2View.Get()),
                ngu.LastError());
        Require(ngu.CopyNativeLuma720DoubleSurface(
                    Ngu720DoubleSurface::Dense, denseView.Get()),
                ngu.LastError());
        Require(ngu.RunNativeLuma720Double(
                    sourceView.Get(),
                    kSourceWidth,
                    kSourceHeight,
                    finalView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto actual = Download(device, context.Get(), output.Get());
        auto addActual = Download(device, context.Get(), addOutput.Get());
        auto chromaXActual =
            Download(device, context.Get(), chromaXOutput.Get());
        auto chromaYActual =
            Download(device, context.Get(), chromaYOutput.Get());
        auto splitActual = Download(device, context.Get(), splitOutput.Get());
        auto splitCbCrActual =
            Download(device, context.Get(), splitCbCrOutput.Get());
        auto scaleXActual =
            Download(device, context.Get(), scaleXOutput.Get());
        auto scaleYActual =
            Download(device, context.Get(), scaleYOutput.Get());
        auto initial0Actual = Download(device, context.Get(), initial0Output.Get());
        auto initial1Actual = Download(device, context.Get(), initial1Output.Get());
        auto initial2Actual = Download(device, context.Get(), initial2Output.Get());
        auto finalLayer0Actual =
            Download(device, context.Get(), finalLayer0Output.Get());
        auto finalLayer1Actual =
            Download(device, context.Get(), finalLayer1Output.Get());
        auto finalLayer2Actual =
            Download(device, context.Get(), finalLayer2Output.Get());
        auto denseActual = Download(device, context.Get(), denseOutput.Get());
        auto finalActual = Download(device, context.Get(), finalOutput.Get());
        auto writeStage = [&](const char* suffix,
                              const std::vector<uint8_t>& bytes) {
            std::ofstream file(actualPath.string() + suffix,
                               std::ios::binary);
            Require(bool(file), "open stage output");
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       std::streamsize(bytes.size()));
        };
        writeStage(".split", splitActual);
        writeStage(".split_cbcr", splitCbCrActual);
        writeStage(".scale_x", scaleXActual);
        writeStage(".scale_y", scaleYActual);
        writeStage(".initial0", initial0Actual);
        writeStage(".initial1", initial1Actual);
        writeStage(".initial2", initial2Actual);
        writeStage(".layer0", finalLayer0Actual);
        writeStage(".layer1", finalLayer1Actual);
        writeStage(".layer2", finalLayer2Actual);
        writeStage(".dense", denseActual);
        writeStage(".add", addActual);
        writeStage(".chroma_x", chromaXActual);
        writeStage(".chroma_y", chromaYActual);
        writeStage(".final", finalActual);
        int maximum = 0;
        const int different =
            CountComponents(actual, expected, maximum);
        int addMaximum = 0;
        const auto addExpectedFull =
            ReadBytes(root /
                          "draw_03914_PS_564_500335c8_rt0_2560x1440_f81.bin",
                      size_t(kOutputWidth) * kOutputHeight * kPixelBytes);
        const auto addExpected = FirstComponent(addExpectedFull);
        const int addDifferent = CountComponents(
            addActual, addExpected, addMaximum);
        int chromaXMaximum = 0;
        const int chromaXDifferent = CountComponents(
            chromaXActual,
            ReadBytes(root /
                          "draw_03915_PS_1284_386cc8d7_rt0_2560x720_f36.bin",
                      size_t(kOutputWidth) * kSourceHeight * kPixelBytes),
            chromaXMaximum);
        int chromaYMaximum = 0;
        const int chromaYDifferent = CountComponents(
            chromaYActual,
            ReadBytes(root /
                          "draw_03916_PS_1284_dc97a0d0_rt0_2560x1440_f36.bin",
                      size_t(kOutputWidth) * kOutputHeight * kPixelBytes),
            chromaYMaximum);
        int splitMaximum = 0;
        const int splitDifferent = CountComponents(
            splitActual,
            FirstComponent(ReadBytes(
                root / "draw_03894_PS_460_bfe50921_rt0_1280x720_f81.bin",
                size_t(kSourceWidth) * kSourceHeight * kPixelBytes)),
            splitMaximum);
        int splitCbCrMaximum = 0;
        const int splitCbCrDifferent = CountComponents(
            splitCbCrActual,
            ReadBytes(root /
                          "input_03915_PS_1284_386cc8d7_s0.bin",
                      size_t(kSourceWidth) * kSourceHeight * 4),
            splitCbCrMaximum);
        int scaleXMaximum = 0;
        const int scaleXDifferent = CountComponents(
            scaleXActual,
            FirstComponent(ReadBytes(
                root / "draw_03895_PS_748_721dd04e_rt0_2560x720_f81.bin",
                size_t(kOutputWidth) * kSourceHeight * kPixelBytes)),
            scaleXMaximum);
        int scaleYMaximum = 0;
        const int scaleYDifferent = CountComponents(
            scaleYActual,
            FirstComponent(ReadBytes(
                root / "draw_03896_PS_748_4085ea73_rt0_2560x1440_f81.bin",
                size_t(kOutputWidth) * kOutputHeight * kPixelBytes)),
            scaleYMaximum);
        auto compareStage = [&](const char* label,
                                const std::vector<uint8_t>& stageActual,
                                const std::filesystem::path& stageExpected,
                                size_t bytes) {
            int stageMaximum = 0;
            const int stageDifferent = CountComponents(
                stageActual, ReadBytes(stageExpected, bytes), stageMaximum);
            std::printf("%s different=%d maximum=%d\n",
                        label, stageDifferent, stageMaximum);
            return stageDifferent == 0 && stageMaximum == 0;
        };
        const bool initial0Ok = compareStage(
            "Initial0",
            initial0Actual,
            root / "draw_03897_PS_1412_aad01c2f_rt0_1280x720_f113.bin",
            size_t(kSourceWidth) * kSourceHeight * kPixelBytes);
        const bool initial1Ok = compareStage(
            "Initial1",
            initial1Actual,
            root / "draw_03897_PS_1412_aad01c2f_rt1_1280x720_f113.bin",
            size_t(kSourceWidth) * kSourceHeight * kPixelBytes);
        const bool initial2Ok = compareStage(
            "Initial2",
            initial2Actual,
            root / "draw_03897_PS_1412_aad01c2f_rt2_1280x720_f113.bin",
            size_t(kSourceWidth) * kSourceHeight * kPixelBytes);
        const bool initialOk = initial0Ok && initial1Ok && initial2Ok;
        const bool layerOk =
            compareStage("FinalLayer0",
                         finalLayer0Actual,
                         root / "draw_03910_PS_3588_80fb311e_rt0_1280x720_f113.bin",
                         size_t(kSourceWidth) * kSourceHeight * kPixelBytes) &&
            compareStage("FinalLayer1",
                         finalLayer1Actual,
                         root / "draw_03911_PS_3588_80fb311e_rt0_1280x720_f113.bin",
                         size_t(kSourceWidth) * kSourceHeight * kPixelBytes) &&
            compareStage("FinalLayer2",
                         finalLayer2Actual,
                         root / "draw_03912_PS_3588_80fb311e_rt0_1280x720_f113.bin",
                         size_t(kSourceWidth) * kSourceHeight * kPixelBytes);
        const bool denseOk =
            compareStage("Dense",
                         denseActual,
                         root / "draw_03913_PS_2100_ba9671a7_rt0_1282x722_f113.bin",
                         size_t(1282) * 722 * kPixelBytes);
        int finalMaximum = 0;
        int finalOverOne = 0;
        const int finalDifferent = CountPixelDifferences(
            finalActual,
            ReadBytes(root /
                          "draw_03918_PS_1352_6992f6f0_rt0_2560x1440_f21.bin",
                      size_t(kOutputWidth) * kOutputHeight * 4),
            finalMaximum,
            finalOverOne);
        const auto timingStart = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 5; ++frame)
        {
            Require(ngu.RunNativeLuma720Double(
                        sourceView.Get(),
                        kSourceWidth,
                        kSourceHeight,
                        finalView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
        }
        const double timingMs =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - timingStart)
                .count() /
            5.0;

        std::ofstream actualFile(actualPath, std::ios::binary);
        Require(bool(actualFile), "open actual output");
        actualFile.write(reinterpret_cast<const char*>(actual.data()),
                         std::streamsize(actual.size()));
        actualFile.close();
        std::printf("Double720 different=%d maximum=%d\n",
                    different, maximum);
        std::printf("Add720 different=%d maximum=%d\n",
                    addDifferent, addMaximum);
        std::printf("ChromaX720 different=%d maximum=%d\n",
                    chromaXDifferent, chromaXMaximum);
        std::printf("ChromaY720 different=%d maximum=%d\n",
                    chromaYDifferent, chromaYMaximum);
        std::printf("Split720 different=%d maximum=%d\n",
                    splitDifferent, splitMaximum);
        std::printf("SplitCbCr720 different=%d maximum=%d\n",
                    splitCbCrDifferent, splitCbCrMaximum);
        std::printf("ScaleX720 different=%d maximum=%d\n",
                    scaleXDifferent, scaleXMaximum);
        std::printf("ScaleY720 different=%d maximum=%d\n",
                    scaleYDifferent, scaleYMaximum);
        std::printf("Final720 different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    finalDifferent, finalMaximum, finalOverOne);
        std::printf("Timing720 %.3f ms/frame\n", timingMs);

        ngu.Reset();
        context.Reset();
        pl_d3d11_destroy(&d3d);
        return different == 0 && maximum == 0 &&
                       addDifferent == 0 && addMaximum == 0 &&
                       chromaXDifferent == 0 && chromaXMaximum == 0 &&
                       chromaYDifferent == 0 && chromaYMaximum == 0 &&
                       splitDifferent == 0 && splitMaximum == 0 &&
                       splitCbCrDifferent == 0 && splitCbCrMaximum == 0 &&
                       scaleXDifferent == 0 && scaleXMaximum == 0 &&
                       scaleYDifferent == 0 && scaleYMaximum == 0 &&
                       initialOk && layerOk && denseOk
                       && finalMaximum <= 1 && finalOverOne == 0
                   ? 0
                   : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
