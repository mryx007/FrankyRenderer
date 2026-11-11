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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    Require(size_t(file.tellg()) == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(expected))),
            "read binary");
    return bytes;
}

void WriteBytes(const std::wstring& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    Require(bool(file), "write binary");
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    Require(bool(file), "finish binary");
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      int width,
                                      int height,
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
    const UINT pixelBytes = format == DXGI_FORMAT_R16_UNORM ? 2
                           : format == DXGI_FORMAT_R16G16_UNORM ? 4
                                                               : 8;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = initial;
    data.SysMemPitch = width * pixelBytes;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, initial ? &data : nullptr, &texture)),
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
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)),
            "map staging texture");
    const UINT pixelBytes = desc.Format == DXGI_FORMAT_R16_UNORM ? 2
                           : desc.Format == DXGI_FORMAT_R16G16_UNORM ? 4
                                                               : 8;
    const size_t rowBytes = size_t(desc.Width) * pixelBytes;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

std::vector<uint8_t> ExtractL16(const std::vector<uint8_t>& capture, size_t pixels)
{
    Require(capture.size() == pixels * 8, "L16 capture size");
    std::vector<uint8_t> bytes(pixels * 2);
    for (size_t i = 0; i < pixels; ++i)
        std::memcpy(bytes.data() + i * 2, capture.data() + i * 8, 2);
    return bytes;
}

int CountWords(const std::vector<uint8_t>& actual,
               const std::vector<uint8_t>& expected,
               int& maximum)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0;
    maximum = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left =
            uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        if (left != right)
        {
            ++different;
            maximum = std::max(maximum, std::abs(int(left) - int(right)));
        }
    }
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: test_ngu_luma_4x720 <capture-root> <actual-prefix>");
        const std::filesystem::path root(argv[1]);
        const std::wstring actualPrefix = std::filesystem::path(argv[2]).wstring();

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        NguScalerRequest request;
        request.sourceWidth = 1280;
        request.sourceHeight = 720;
        request.stage = NguScalerStage::VeryHighLuma4x720LowCore;
        Require(ProbeNguScaler(request) == NguScalerSupport::Ready,
                "720p 4x luma capability");
        request.sourceHeight = 721;
        Require(ProbeNguScaler(request) == NguScalerSupport::UnsupportedGeometry,
                "720p 4x luma geometry gate");
        request.sourceHeight = 720;
        request.quality = NguQuality::High;
        Require(ProbeNguScaler(request) == NguScalerSupport::ExperimentalUnimplemented,
                "720p 4x luma quality gate");
        request.quality = NguQuality::VeryHigh;
        request.stage = NguScalerStage::VeryHighLuma4x720HighCore;
        Require(ProbeNguScaler(request) == NguScalerSupport::Ready,
                "720p 4x high-core capability");
        request.stage = NguScalerStage::VeryHighLuma4x720TailYcbcr;
        Require(ProbeNguScaler(request) == NguScalerSupport::Ready,
                "720p 4x tail YCbCr capability");

        const auto sourceBytes =
            ReadBytes(root / "input_00224_PS_460_bfe50921_s0.bin", size_t(1280) * 720 * 8);
        auto source = CreateTexture(device,
                                    1280,
                                    720,
                                    DXGI_FORMAT_R16G16B16A16_UNORM,
                                    D3D11_BIND_SHADER_RESOURCE,
                                    sourceBytes.data());
        ComPtr<ID3D11ShaderResourceView> sourceView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    source.Get(), nullptr, &sourceView)),
                "source SRV");

        NguScaler ngu;
        Require(ngu.PrepareNativeLuma4x720Low(d3d->gpu), ngu.LastError());
        Require(ngu.RunNativeLuma4x720Low(sourceView.Get(), 1280, 720), ngu.LastError());
        pl_gpu_finish(d3d->gpu);

        auto split = CreateTexture(device,
                                   1280,
                                   720,
                                   DXGI_FORMAT_R16_UNORM,
                                   D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto scaleX = CreateTexture(device,
                                    5120,
                                    720,
                                    DXGI_FORMAT_R16_UNORM,
                                    D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto scaleY = CreateTexture(device,
                                    5120,
                                    2880,
                                    DXGI_FORMAT_R16_UNORM,
                                    D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto initial = CreateTexture(device,
                                     1280,
                                     720,
                                     DXGI_FORMAT_R16G16B16A16_FLOAT,
                                     D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        auto lowLayer = CreateTexture(device,
                                      1280,
                                      720,
                                      DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> splitView, scaleXView, scaleYView, initialView, lowLayerView;
        Require(SUCCEEDED(device->CreateRenderTargetView(split.Get(), nullptr, &splitView)),
                "split RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(scaleX.Get(), nullptr, &scaleXView)),
                "scale X RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(scaleY.Get(), nullptr, &scaleYView)),
                "scale Y RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(initial.Get(), nullptr, &initialView)),
                "initial RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(lowLayer.Get(), nullptr, &lowLayerView)),
                "low layer RTV");
        Require(ngu.CopyNativeLuma4x720SplitY(splitView.Get()), ngu.LastError());
        Require(ngu.CopyNativeLuma4x720ScaleX(scaleXView.Get()), ngu.LastError());
        Require(ngu.CopyNativeLuma4x720ScaleY(scaleYView.Get()), ngu.LastError());
        Require(ngu.CopyNativeLuma4x720Initial(initialView.Get()), ngu.LastError());
        Require(ngu.CopyNativeLuma4x720LowLayer(lowLayerView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);

        auto splitActual = Download(device, context.Get(), split.Get());
        auto scaleXActual = Download(device, context.Get(), scaleX.Get());
        auto scaleYActual = Download(device, context.Get(), scaleY.Get());
        auto initialActual = Download(device, context.Get(), initial.Get());
        auto lowLayerActual = Download(device, context.Get(), lowLayer.Get());

        int splitMaximum = 0, scaleXMaximum = 0, scaleYMaximum = 0;
        int initialMaximum = 0, lowLayerMaximum = 0;
        const int splitDifferent = CountWords(
            splitActual,
            ExtractL16(ReadBytes(root / "draw_00224_PS_460_bfe50921_rt0_1280x720_f81.bin",
                                 size_t(1280) * 720 * 8),
                       size_t(1280) * 720),
            splitMaximum);
        const int scaleXDifferent = CountWords(
            scaleXActual,
            ExtractL16(ReadBytes(root / "draw_00225_PS_520_4a9505a4_rt0_5120x720_f81.bin",
                                 size_t(5120) * 720 * 8),
                       size_t(5120) * 720),
            scaleXMaximum);
        const int scaleYDifferent = CountWords(
            scaleYActual,
            ExtractL16(ReadBytes(root / "draw_00226_PS_520_4ee2414c_rt0_5120x2880_f81.bin",
                                 size_t(5120) * 2880 * 8),
                       size_t(5120) * 2880),
            scaleYMaximum);
        const int initialDifferent = CountWords(
            initialActual,
            ReadBytes(root / "draw_00227_PS_1412_aad01c2f_rt0_1280x720_f113.bin",
                      size_t(1280) * 720 * 8),
            initialMaximum);
        const int lowLayerDifferent = CountWords(
            lowLayerActual,
            ReadBytes(root / "draw_00242_PS_3588_80fb311e_rt0_1280x720_f113.bin",
                      size_t(1280) * 720 * 8),
            lowLayerMaximum);

        constexpr int frames = 10;
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame)
            Require(ngu.RunNativeLuma4x720Low(sourceView.Get(), 1280, 720),
                    ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const double milliseconds =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count() / frames;

        Require(ngu.PrepareNativeLuma4x720High(d3d->gpu), ngu.LastError());
        Require(ngu.RunNativeLuma4x720High(sourceView.Get(), 1280, 720), ngu.LastError());
        pl_gpu_finish(d3d->gpu);

        std::array<std::array<ComPtr<ID3D11Texture2D>, 4>, 3> upTextures;
        std::array<ComPtr<ID3D11Texture2D>, 3> convertTextures;
        std::array<ComPtr<ID3D11Texture2D>, 3> highTextures;
        std::array<std::array<ComPtr<ID3D11RenderTargetView>, 4>, 3> upViews;
        std::array<ComPtr<ID3D11RenderTargetView>, 3> convertViews;
        std::array<ComPtr<ID3D11RenderTargetView>, 3> highViews;
        for (size_t feature = 0; feature < 3; ++feature)
        {
            for (size_t part = 0; part < 4; ++part)
            {
                upTextures[feature][part] =
                    CreateTexture(device,
                                  1282,
                                  722,
                                  DXGI_FORMAT_R16G16B16A16_FLOAT,
                                  D3D11_BIND_RENDER_TARGET);
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            upTextures[feature][part].Get(),
                            nullptr,
                            &upViews[feature][part])),
                        "upsample RTV");
                Require(ngu.CopyNativeLuma4x720Upsample(
                            int(feature), int(part), upViews[feature][part].Get()),
                        ngu.LastError());
            }
            convertTextures[feature] = CreateTexture(device,
                                                     2560,
                                                     1440,
                                                     DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                     D3D11_BIND_RENDER_TARGET);
            highTextures[feature] = CreateTexture(device,
                                                  2560,
                                                  1440,
                                                  DXGI_FORMAT_R16G16B16A16_FLOAT,
                                                  D3D11_BIND_RENDER_TARGET);
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        convertTextures[feature].Get(), nullptr, &convertViews[feature])),
                    "convert RTV");
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        highTextures[feature].Get(), nullptr, &highViews[feature])),
                    "high layer RTV");
            Require(ngu.CopyNativeLuma4x720HighLayer(
                        int(feature), highViews[feature].Get()),
                    ngu.LastError());
            Require(ngu.CopyNativeLuma4x720Convert(
                        int(feature), convertViews[feature].Get()),
                    ngu.LastError());
        }
        pl_gpu_finish(d3d->gpu);

        const char* upNames[3][4] = {
            {
                "input_00246_PS_912_86f81c64_s0.bin",
                "input_00246_PS_912_86f81c64_s2.bin",
                "input_00246_PS_912_86f81c64_s3.bin",
                "input_00246_PS_912_86f81c64_s4.bin",
            },
            {
                "input_00247_PS_912_86f81c64_s0.bin",
                "input_00247_PS_912_86f81c64_s2.bin",
                "input_00247_PS_912_86f81c64_s3.bin",
                "input_00247_PS_912_86f81c64_s4.bin",
            },
            {
                "input_00248_PS_912_86f81c64_s0.bin",
                "input_00248_PS_912_86f81c64_s2.bin",
                "input_00248_PS_912_86f81c64_s3.bin",
                "input_00248_PS_912_86f81c64_s4.bin",
            },
        };
        const char* convertNames[3] = {
            "draw_00246_PS_912_86f81c64_rt0_2560x1440_f113.bin",
            "draw_00247_PS_912_86f81c64_rt0_2560x1440_f113.bin",
            "draw_00248_PS_912_86f81c64_rt0_2560x1440_f113.bin",
        };
        const char* highNames[3] = {
            "draw_00252_PS_3588_80fb311e_rt0_2560x1440_f113.bin",
            "draw_00253_PS_3588_80fb311e_rt0_2560x1440_f113.bin",
            "draw_00254_PS_3588_80fb311e_rt0_2560x1440_f113.bin",
        };
        std::array<std::array<std::vector<uint8_t>, 4>, 3> upActual;
        std::array<std::vector<uint8_t>, 3> convertActual;
        std::array<std::vector<uint8_t>, 3> highActual;
        bool highPass = true;
        for (size_t feature = 0; feature < 3; ++feature)
        {
            convertActual[feature] =
                Download(device, context.Get(), convertTextures[feature].Get());
            highActual[feature] = Download(device, context.Get(), highTextures[feature].Get());
            bool upExact = true;
            for (size_t part = 0; part < 4; ++part)
            {
                upActual[feature][part] =
                    Download(device, context.Get(), upTextures[feature][part].Get());
                int upMax = 0;
                const int upDifferent = CountWords(
                    upActual[feature][part],
                    ReadBytes(root / upNames[feature][part], size_t(1282) * 722 * 8),
                    upMax);
                std::printf("Upsample%d.%d different=%d maximum=%d\n",
                            int(feature), int(part), upDifferent, upMax);
                upExact = upExact && upDifferent == 0 && upMax == 0;
            }
            int convertMax = 0, highMax = 0;
            const int convertDifferent = CountWords(
                convertActual[feature],
                ReadBytes(root / convertNames[feature], size_t(2560) * 1440 * 8),
                convertMax);
            const int highDifferent = CountWords(
                highActual[feature],
                ReadBytes(root / highNames[feature], size_t(2560) * 1440 * 8),
                highMax);
            std::printf("Convert%d different=%d maximum=%d\n",
                        int(feature), convertDifferent, convertMax);
            std::printf("HighLayer%d different=%d maximum=%d\n",
                        int(feature), highDifferent, highMax);
            highPass = highPass && upExact &&
                       convertDifferent == 0 && convertMax == 0 &&
                       highDifferent == 0 && highMax == 0;
        }
        const auto highStart = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame)
            Require(ngu.RunNativeLuma4x720High(sourceView.Get(), 1280, 720),
                    ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const double highMilliseconds =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - highStart).count() / frames;

        request.stage = NguScalerStage::VeryHighLuma4x720DenseAdd;
        Require(ProbeNguScaler(request) == NguScalerSupport::Ready,
                "720p 4x dense/add capability");
        Require(ngu.PrepareNativeLuma4x720DenseAdd(d3d->gpu), ngu.LastError());
        Require(ngu.RunNativeLuma4x720DenseAdd(sourceView.Get(), 1280, 720),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);

        auto dense = CreateTexture(device,
                                   2562,
                                   1442,
                                   DXGI_FORMAT_R16G16B16A16_FLOAT,
                                   D3D11_BIND_RENDER_TARGET);
        auto add = CreateTexture(device,
                                 5120,
                                 2880,
                                 DXGI_FORMAT_R16_UNORM,
                                 D3D11_BIND_RENDER_TARGET);
        ComPtr<ID3D11RenderTargetView> denseView, addView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    dense.Get(), nullptr, &denseView)),
                "dense RTV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    add.Get(), nullptr, &addView)),
                "add RTV");
        Require(ngu.CopyNativeLuma4x720Dense(denseView.Get()), ngu.LastError());
        Require(ngu.CopyNativeLuma4x720Add(addView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto denseActual = Download(device, context.Get(), dense.Get());
        const auto addActual = Download(device, context.Get(), add.Get());
        int denseMaximum = 0, addMaximum = 0;
        const int denseDifferent = CountWords(
            denseActual,
            ReadBytes(root / "draw_00249_PS_2100_ba9671a7_rt0_2562x1442_f113.bin",
                      size_t(2562) * 1442 * 8),
            denseMaximum);
        const int addDifferent = CountWords(
            addActual,
            ExtractL16(ReadBytes(root / "draw_00250_PS_564_500335c8_rt0_5120x2880_f81.bin",
                                 size_t(5120) * 2880 * 8),
                       size_t(5120) * 2880),
            addMaximum);
        const auto denseAddStart = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame)
            Require(ngu.RunNativeLuma4x720DenseAdd(sourceView.Get(), 1280, 720),
                    ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const double denseAddMilliseconds =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - denseAddStart).count() / frames;
        const bool denseAddPass =
            denseDifferent == 0 && denseMaximum == 0 &&
            addDifferent == 0 && addMaximum == 0;

        Require(ngu.PrepareNativeLuma4x720Tail(d3d->gpu), ngu.LastError());
        const auto ycbcrInputBytes = ExtractL16(
            ReadBytes(root / "input_00251_PS_536_057fb11f_s0.bin",
                      size_t(5120) * 2880 * 8),
            size_t(5120) * 2880);
        auto ycbcrInput = CreateTexture(device,
                                        5120,
                                        2880,
                                        DXGI_FORMAT_R16_UNORM,
                                        D3D11_BIND_SHADER_RESOURCE,
                                        ycbcrInputBytes.data());
        ComPtr<ID3D11ShaderResourceView> ycbcrInputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    ycbcrInput.Get(), nullptr, &ycbcrInputView)),
                "tail YCbCr input SRV");
        auto ycbcr = CreateTexture(device,
                                   5120,
                                   2880,
                                   DXGI_FORMAT_R16G16B16A16_UNORM,
                                   D3D11_BIND_RENDER_TARGET);
        ComPtr<ID3D11RenderTargetView> ycbcrView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    ycbcr.Get(), nullptr, &ycbcrView)),
                "tail YCbCr RTV");
        Require(ngu.RunNativeLuma4x720TailYcbcr(
                    ycbcrInputView.Get(), ycbcrView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto ycbcrActual = Download(device, context.Get(), ycbcr.Get());
        int ycbcrMaximum = 0;
        const int ycbcrDifferent = CountWords(
            ycbcrActual,
            ReadBytes(root / "draw_00251_PS_536_057fb11f_rt0_5120x2880_f36.bin",
                      size_t(5120) * 2880 * 8),
            ycbcrMaximum);

        std::printf("Split different=%d maximum=%d\n", splitDifferent, splitMaximum);
        std::printf("ScaleX different=%d maximum=%d\n", scaleXDifferent, scaleXMaximum);
        std::printf("ScaleY different=%d maximum=%d\n", scaleYDifferent, scaleYMaximum);
        std::printf("Initial different=%d maximum=%d\n", initialDifferent, initialMaximum);
        std::printf("LowLayer different=%d maximum=%d\n", lowLayerDifferent, lowLayerMaximum);
        std::printf("Core %.3f ms/frame\n", milliseconds);
        std::printf("HighCore %.3f ms/frame\n", highMilliseconds);
        std::printf("Dense different=%d maximum=%d\n", denseDifferent, denseMaximum);
        std::printf("Add different=%d maximum=%d\n", addDifferent, addMaximum);
        std::printf("DenseAddCore %.3f ms/frame\n", denseAddMilliseconds);
        std::printf("TailYcbcr different=%d maximum=%d\n",
                    ycbcrDifferent, ycbcrMaximum);

        if (!actualPrefix.empty())
        {
            WriteBytes(actualPrefix + L"split.bin", splitActual);
            WriteBytes(actualPrefix + L"scale_x.bin", scaleXActual);
            WriteBytes(actualPrefix + L"scale_y.bin", scaleYActual);
            WriteBytes(actualPrefix + L"initial.bin", initialActual);
            WriteBytes(actualPrefix + L"low_layer.bin", lowLayerActual);
            for (size_t feature = 0; feature < 3; ++feature)
            {
                for (size_t part = 0; part < 4; ++part)
                    WriteBytes(actualPrefix + L"upsample_" +
                                   std::to_wstring(feature) + L"_" +
                                   std::to_wstring(part) + L".bin",
                               upActual[feature][part]);
                WriteBytes(actualPrefix + L"convert_" +
                               std::to_wstring(feature) + L".bin",
                           convertActual[feature]);
                WriteBytes(actualPrefix + L"high_layer_" +
                               std::to_wstring(feature) + L".bin",
                           highActual[feature]);
            }
            WriteBytes(actualPrefix + L"dense.bin", denseActual);
            WriteBytes(actualPrefix + L"add.bin", addActual);
            WriteBytes(actualPrefix + L"tail_ycbcr.bin", ycbcrActual);
        }
        const bool pass = splitDifferent == 0 && splitMaximum == 0 &&
                          scaleXDifferent == 0 && scaleXMaximum == 0 &&
                          scaleYDifferent == 0 && scaleYMaximum == 0 &&
                          initialDifferent == 0 && initialMaximum == 0 &&
                          lowLayerDifferent == 0 && lowLayerMaximum == 0 &&
                          highPass && denseAddPass &&
                          ycbcrDifferent == 0 && ycbcrMaximum == 0;
        std::printf("%s native NGU 720p VeryHigh 4x luma tail\n",
                    pass ? "PASS_PRODUCT" : "FAIL_PRODUCT");

        ngu.Reset();
        ycbcrView.Reset();
        ycbcr.Reset();
        ycbcrInputView.Reset();
        ycbcrInput.Reset();
        addView.Reset();
        add.Reset();
        denseView.Reset();
        dense.Reset();
        for (size_t feature = 0; feature < 3; ++feature)
        {
            highViews[feature].Reset();
            highTextures[feature].Reset();
            convertViews[feature].Reset();
            convertTextures[feature].Reset();
            for (size_t part = 0; part < 4; ++part)
            {
                upViews[feature][part].Reset();
                upTextures[feature][part].Reset();
            }
        }
        lowLayerView.Reset();
        lowLayer.Reset();
        initialView.Reset();
        initial.Reset();
        scaleYView.Reset();
        scaleY.Reset();
        scaleXView.Reset();
        scaleX.Reset();
        splitView.Reset();
        split.Reset();
        sourceView.Reset();
        source.Reset();
        context.Reset();
        pl_d3d11_destroy(&d3d);
        return pass ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
