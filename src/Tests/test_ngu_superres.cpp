#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    Require(size_t(file.tellg()) == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(expected))), "read binary");
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
                           : format == DXGI_FORMAT_R16G16_FLOAT ? 4
                           : format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4
                           : format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16
                                                               : 8;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = initial;
    data.SysMemPitch = width * pixelBytes;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, initial ? &data : nullptr, &texture)), "create texture");
    return texture;
}

std::vector<uint8_t> Download(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source)
{
    D3D11_TEXTURE2D_DESC desc = {};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "create staging texture");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map staging texture");
    const UINT pixelBytes = desc.Format == DXGI_FORMAT_R16_UNORM ? 2
                           : desc.Format == DXGI_FORMAT_R16G16_FLOAT ? 4
                           : desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4
                           : desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16
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

int CountWords(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int& maximum)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0;
    maximum = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left = uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right = uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        if (left != right)
        {
            ++different;
            maximum = std::max(maximum, std::abs(int(left) - int(right)));
        }
    }
    return different;
}

int CompareRgb8(const std::vector<uint8_t>& actual,
                const std::vector<uint8_t>& expected,
                int& maximum,
                int& overOne)
{
    Require(actual.size() == expected.size() && actual.size() % 4 == 0,
            "RGB8 comparison size");
    int different = 0;
    maximum = 0;
    overOne = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 4)
    {
        bool pixelOverOne = false;
        for (int channel = 0; channel < 3; ++channel)
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
        Require(argc == 2 || argc == 3, "usage: test_ngu_superres <capture-root> [actual-prefix]");
        const std::filesystem::path root = std::filesystem::path(argv[1]);
        const std::wstring actualPrefix = argc == 3 ? std::filesystem::path(argv[2]).wstring() : L"";
        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        NguScalerRequest schedulerRequest;
        schedulerRequest.sourceWidth = 1280;
        schedulerRequest.sourceHeight = 720;
        schedulerRequest.stage = NguScalerStage::VeryHighSuperResStrength3;
        Require(ProbeNguScaler(schedulerRequest) == NguScalerSupport::Ready,
                "strength-3 scheduler capability");
        schedulerRequest.sourceHeight = 721;
        Require(ProbeNguScaler(schedulerRequest) == NguScalerSupport::UnsupportedGeometry,
                "strength-3 scheduler geometry gate");
        schedulerRequest.sourceHeight = 720;
        schedulerRequest.quality = NguQuality::High;
        Require(ProbeNguScaler(schedulerRequest) == NguScalerSupport::ExperimentalUnimplemented,
                "strength-3 scheduler quality gate");

        NguScaler ngu;
        Require(ngu.PrepareNativeSuperResPrimitives(d3d->gpu), ngu.LastError());

        const auto sourceBytes =
            ReadBytes(root / "input_00157_PS_520_4a9505a4_s0.bin", size_t(1280) * 720 * 8);
        auto source = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, sourceBytes.data());
        ComPtr<ID3D11ShaderResourceView> sourceSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(source.Get(), nullptr, &sourceSrv)), "source SRV");

        auto downXTexture = CreateTexture(
            device, 640, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> downXView;
        ComPtr<ID3D11ShaderResourceView> downXSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(downXTexture.Get(), nullptr, &downXView)), "X RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(downXTexture.Get(), nullptr, &downXSrv)), "X SRV");
        Require(ngu.RunNativeSuperResDownX(sourceSrv.Get(), downXView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto downXActual = Download(device, context.Get(), downXTexture.Get());
        int downXMax = 0;
        const int downXDifferences = CountWords(
            downXActual,
            ReadBytes(root / "draw_00157_PS_520_4a9505a4_rt0_640x720_f36.bin", size_t(640) * 720 * 8),
            downXMax);

        auto downYTexture = CreateTexture(
            device, 640, 360, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> downYView;
        ComPtr<ID3D11ShaderResourceView> downYSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(downYTexture.Get(), nullptr, &downYView)), "Y RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(downYTexture.Get(), nullptr, &downYSrv)), "Y SRV");
        Require(ngu.RunNativeSuperResDownY(downXSrv.Get(), downYView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto downYActual = Download(device, context.Get(), downYTexture.Get());
        int downYMax = 0;
        const int downYDifferences = CountWords(
            downYActual,
            ReadBytes(root / "draw_00158_PS_520_4ee2414c_rt0_640x360_f36.bin", size_t(640) * 360 * 8),
            downYMax);

        const auto originalBytes =
            ReadBytes(root / "input_00159_PS_284_e8e2a8eb_s0.bin", size_t(640) * 360 * 4);
        auto original = CreateTexture(
            device, 640, 360, DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_SHADER_RESOURCE, originalBytes.data());
        ComPtr<ID3D11ShaderResourceView> originalSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(original.Get(), nullptr, &originalSrv)), "original SRV");
        auto diffTexture = CreateTexture(
            device, 640, 360, DXGI_FORMAT_R16G16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> diffView;
        ComPtr<ID3D11ShaderResourceView> diffSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(diffTexture.Get(), nullptr, &diffView)), "diff RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(diffTexture.Get(), nullptr, &diffSrv)), "diff SRV");
        Require(ngu.RunNativeSuperResDiff(originalSrv.Get(), downYSrv.Get(), diffView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto diffActual = Download(device, context.Get(), diffTexture.Get());
        int diffMaximum = 0;
        const int diffDifferences = CountWords(
            diffActual,
            ReadBytes(root / "draw_00159_PS_284_e8e2a8eb_rt0_640x360_f112.bin", size_t(640) * 360 * 4),
            diffMaximum);

        const auto mainOriginalBytes =
            ReadBytes(root / "input_00160_PS_4412_c39b49cf_s0.bin", size_t(1280) * 720 * 8);
        auto mainOriginal = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, mainOriginalBytes.data());
        ComPtr<ID3D11ShaderResourceView> mainOriginalSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    mainOriginal.Get(), nullptr, &mainOriginalSrv)),
                "main original SRV");
        const auto mainWorkBytes =
            ReadBytes(root / "input_00160_PS_4412_c39b49cf_s2.bin", size_t(1280) * 720 * 8);
        auto mainWork = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, mainWorkBytes.data());
        ComPtr<ID3D11ShaderResourceView> mainWorkSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(mainWork.Get(), nullptr, &mainWorkSrv)),
                "main work SRV");
        const auto mainDifferenceBytes =
            ReadBytes(root / "input_00160_PS_4412_c39b49cf_s3.bin", size_t(640) * 360 * 4);
        auto mainDifference = CreateTexture(
            device, 640, 360, DXGI_FORMAT_R16G16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE, mainDifferenceBytes.data());
        ComPtr<ID3D11ShaderResourceView> mainDifferenceSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    mainDifference.Get(), nullptr, &mainDifferenceSrv)),
                "main difference SRV");
        auto mainOutput = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> mainOutputView;
        ComPtr<ID3D11ShaderResourceView> mainOutputSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(mainOutput.Get(), nullptr, &mainOutputView)),
                "main RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(mainOutput.Get(), nullptr, &mainOutputSrv)),
                "main SRV");
        Require(ngu.RunNativeSuperResToRgbMain(
                    mainOriginalSrv.Get(), mainWorkSrv.Get(),
                    mainDifferenceSrv.Get(), mainOutputView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto mainActual = Download(device, context.Get(), mainOutput.Get());
        int mainMaximum = 0;
        const int mainDifferences = CountWords(
            mainActual,
            ReadBytes(root / "draw_00160_PS_4412_c39b49cf_rt0_1280x720_f36.bin", size_t(1280) * 720 * 8),
            mainMaximum);
        auto mainFloat = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R32G32B32A32_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> mainFloatView;
        Require(SUCCEEDED(device->CreateRenderTargetView(mainFloat.Get(), nullptr, &mainFloatView)),
                "main float RTV");
        Require(ngu.RunNativeSuperResToRgbMain(
                    mainOriginalSrv.Get(), mainWorkSrv.Get(),
                    mainDifferenceSrv.Get(), mainFloatView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto mainFloatActual = Download(device, context.Get(), mainFloat.Get());

        const auto baseOriginalBytes =
            ReadBytes(root / "input_00164_PS_4576_b7830945_s0.bin", size_t(1280) * 720 * 8);
        auto baseOriginal = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, baseOriginalBytes.data());
        ComPtr<ID3D11ShaderResourceView> baseOriginalSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    baseOriginal.Get(), nullptr, &baseOriginalSrv)),
                "base original SRV");
        const auto baseWorkBytes =
            ReadBytes(root / "input_00164_PS_4576_b7830945_s2.bin", size_t(1280) * 720 * 8);
        auto baseWork = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, baseWorkBytes.data());
        ComPtr<ID3D11ShaderResourceView> baseWorkSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(baseWork.Get(), nullptr, &baseWorkSrv)),
                "base work SRV");
        const auto baseDifferenceBytes =
            ReadBytes(root / "input_00164_PS_4576_b7830945_s3.bin", size_t(640) * 360 * 4);
        auto baseDifference = CreateTexture(
            device, 640, 360, DXGI_FORMAT_R16G16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE, baseDifferenceBytes.data());
        ComPtr<ID3D11ShaderResourceView> baseDifferenceSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    baseDifference.Get(), nullptr, &baseDifferenceSrv)),
                "base difference SRV");
        auto baseOutput = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> baseOutputView;
        Require(SUCCEEDED(device->CreateRenderTargetView(baseOutput.Get(), nullptr, &baseOutputView)),
                "base RTV");
        Require(ngu.RunNativeSuperResToRgbBase(
                    baseOriginalSrv.Get(), baseWorkSrv.Get(),
                    baseDifferenceSrv.Get(), baseOutputView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto baseActual = Download(device, context.Get(), baseOutput.Get());
        int baseMaximum = 0;
        const int baseDifferences = CountWords(
            baseActual,
            ReadBytes(root / "draw_00164_PS_4576_b7830945_rt0_1280x720_f36.bin", size_t(1280) * 720 * 8),
            baseMaximum);

        Require(ngu.RunNativeSuperResDownX(sourceSrv.Get(), downXView.Get()), ngu.LastError());
        Require(ngu.RunNativeSuperResDownY(downXSrv.Get(), downYView.Get()), ngu.LastError());
        Require(ngu.RunNativeSuperResDiff(originalSrv.Get(), downYSrv.Get(), diffView.Get()),
                ngu.LastError());
        Require(ngu.RunNativeSuperResToRgbMain(
                    mainOriginalSrv.Get(), sourceSrv.Get(), diffSrv.Get(), mainOutputView.Get()),
                ngu.LastError());
        Require(ngu.RunNativeSuperResDownX(mainOutputSrv.Get(), downXView.Get()), ngu.LastError());
        Require(ngu.RunNativeSuperResDownY(downXSrv.Get(), downYView.Get()), ngu.LastError());
        Require(ngu.RunNativeSuperResDiff(originalSrv.Get(), downYSrv.Get(), diffView.Get()),
                ngu.LastError());
        Require(ngu.RunNativeSuperResToRgbBase(
                    baseOriginalSrv.Get(), mainOutputSrv.Get(), diffSrv.Get(), baseOutputView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto chainActual = Download(device, context.Get(), baseOutput.Get());
        int chainMaximum = 0;
        const int chainDifferences = CountWords(
            chainActual,
            ReadBytes(root / "draw_00164_PS_4576_b7830945_rt0_1280x720_f36.bin", size_t(1280) * 720 * 8),
            chainMaximum);

        Require(ngu.PrepareNativeSuperResStrength3(d3d->gpu), ngu.LastError());
        auto schedulerOutput = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> schedulerOutputView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    schedulerOutput.Get(), nullptr, &schedulerOutputView)),
                "scheduler output RTV");
        Require(ngu.RunNativeSuperResStrength3(sourceSrv.Get(),
                                               originalSrv.Get(),
                                               mainOriginalSrv.Get(),
                                               baseOriginalSrv.Get(),
                                               schedulerOutputView.Get()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto schedulerActual = Download(device, context.Get(), schedulerOutput.Get());
        int schedulerMaximum = 0;
        const int schedulerDifferences = CountWords(
            schedulerActual,
            ReadBytes(root / "draw_00164_PS_4576_b7830945_rt0_1280x720_f36.bin", size_t(1280) * 720 * 8),
            schedulerMaximum);

        constexpr int benchmarkFrames = 20;
        const auto benchmarkStart = std::chrono::steady_clock::now();
        for (int frame = 0; frame < benchmarkFrames; ++frame)
            Require(ngu.RunNativeSuperResStrength3(sourceSrv.Get(),
                                                   originalSrv.Get(),
                                                   mainOriginalSrv.Get(),
                                                   baseOriginalSrv.Get(),
                                                   schedulerOutputView.Get()),
                    ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const double schedulerMilliseconds =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - benchmarkStart).count() / benchmarkFrames;

        int scheduler1080Differences = 0;
        int scheduler1080Maximum = 0;
        bool scheduler1080Ran = false;
        std::vector<uint8_t> scheduler1080Actual;
        if (const char* root1080Env = std::getenv("NGU_1080_CAPTURE_ROOT"))
        {
            NguScalerRequest probe1080;
            probe1080.sourceWidth = 1920;
            probe1080.sourceHeight = 1080;
            probe1080.stage = NguScalerStage::VeryHighLuma1080SuperResFull;
            Require(ProbeNguScaler(probe1080) == NguScalerSupport::Ready,
                    "1080p strength-3 capability");

            const auto root1080 = std::filesystem::path(root1080Env);
            const auto source1080Bytes =
                ReadBytes(root1080 / "input_00146_PS_520_4a9505a4_s0.bin",
                          size_t(1920) * 1080 * 8);
            auto source1080 = CreateTexture(
                device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_SHADER_RESOURCE, source1080Bytes.data());
            ComPtr<ID3D11ShaderResourceView> source1080Srv;
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        source1080.Get(), nullptr, &source1080Srv)),
                    "1080p source SRV");

            const auto original1080Bytes =
                ReadBytes(root1080 / "input_00148_PS_284_e8e2a8eb_s0.bin",
                          size_t(960) * 540 * 4);
            auto original1080 = CreateTexture(
                device, 960, 540, DXGI_FORMAT_B8G8R8A8_UNORM,
                D3D11_BIND_SHADER_RESOURCE, original1080Bytes.data());
            ComPtr<ID3D11ShaderResourceView> original1080Srv;
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        original1080.Get(), nullptr, &original1080Srv)),
                    "1080p original SRV");

            const auto main1080Bytes =
                ReadBytes(root1080 / "input_00149_PS_4412_c39b49cf_s0.bin",
                          size_t(1920) * 1080 * 8);
            auto main1080 = CreateTexture(
                device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_SHADER_RESOURCE, main1080Bytes.data());
            ComPtr<ID3D11ShaderResourceView> main1080Srv;
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        main1080.Get(), nullptr, &main1080Srv)),
                    "1080p main original SRV");

            const auto base1080Bytes =
                ReadBytes(root1080 / "input_00153_PS_4576_b7830945_s0.bin",
                          size_t(1920) * 1080 * 8);
            auto base1080 = CreateTexture(
                device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_SHADER_RESOURCE, base1080Bytes.data());
            ComPtr<ID3D11ShaderResourceView> base1080Srv;
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        base1080.Get(), nullptr, &base1080Srv)),
                    "1080p base original SRV");

            auto downX1080Texture = CreateTexture(
                device, 960, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            auto downY1080Texture = CreateTexture(
                device, 960, 540, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            auto diff1080Texture = CreateTexture(
                device, 960, 540, DXGI_FORMAT_R16G16_FLOAT,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            auto main1080Texture = CreateTexture(
                device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            auto baseOutput1080Texture = CreateTexture(
                device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            ComPtr<ID3D11RenderTargetView> downX1080View;
            ComPtr<ID3D11RenderTargetView> downY1080View;
            ComPtr<ID3D11RenderTargetView> diff1080View;
            ComPtr<ID3D11RenderTargetView> main1080View;
            ComPtr<ID3D11RenderTargetView> baseOutput1080View;
            ComPtr<ID3D11ShaderResourceView> downX1080Srv;
            ComPtr<ID3D11ShaderResourceView> downY1080Srv;
            ComPtr<ID3D11ShaderResourceView> diff1080Srv;
            ComPtr<ID3D11ShaderResourceView> main1080OutputSrv;
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        downX1080Texture.Get(), nullptr, &downX1080View)) &&
                        SUCCEEDED(device->CreateRenderTargetView(
                            downY1080Texture.Get(), nullptr, &downY1080View)) &&
                        SUCCEEDED(device->CreateRenderTargetView(
                            diff1080Texture.Get(), nullptr, &diff1080View)) &&
                        SUCCEEDED(device->CreateRenderTargetView(
                            main1080Texture.Get(), nullptr, &main1080View)) &&
                        SUCCEEDED(device->CreateRenderTargetView(
                            baseOutput1080Texture.Get(), nullptr, &baseOutput1080View)),
                    "1080p stage RTVs");
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        downX1080Texture.Get(), nullptr, &downX1080Srv)) &&
                        SUCCEEDED(device->CreateShaderResourceView(
                            downY1080Texture.Get(), nullptr, &downY1080Srv)) &&
                        SUCCEEDED(device->CreateShaderResourceView(
                            diff1080Texture.Get(), nullptr, &diff1080Srv)) &&
                        SUCCEEDED(device->CreateShaderResourceView(
                            main1080Texture.Get(), nullptr, &main1080OutputSrv)),
                    "1080p stage SRVs");
            Require(ngu.RunNativeSuperResDownX1080(source1080Srv.Get(), downX1080View.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto downX1080Actual =
                Download(device, context.Get(), downX1080Texture.Get());
            if (!actualPrefix.empty())
                WriteBytes(actualPrefix + L"1080_downx.bin", downX1080Actual);
            int downX1080Maximum = 0;
            const int downX1080Differences = CountWords(
                downX1080Actual,
                ReadBytes(root1080 / "draw_00146_PS_520_4a9505a4_rt0_960x1080_f36.bin",
                          size_t(960) * 1080 * 8),
                downX1080Maximum);
            Require(ngu.RunNativeSuperResDownY1080(
                        downX1080Srv.Get(), downY1080View.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto downY1080Actual =
                Download(device, context.Get(), downY1080Texture.Get());
            if (!actualPrefix.empty())
                WriteBytes(actualPrefix + L"1080_downy.bin", downY1080Actual);
            int downY1080Maximum = 0;
            const int downY1080Differences = CountWords(
                downY1080Actual,
                ReadBytes(root1080 / "draw_00147_PS_520_4ee2414c_rt0_960x540_f36.bin",
                          size_t(960) * 540 * 8),
                downY1080Maximum);
            Require(ngu.RunNativeSuperResDiff1080(
                        original1080Srv.Get(), downY1080Srv.Get(), diff1080View.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto diff1080Actual =
                Download(device, context.Get(), diff1080Texture.Get());
            if (!actualPrefix.empty())
                WriteBytes(actualPrefix + L"1080_diff.bin", diff1080Actual);
            int diff1080Maximum = 0;
            const int diff1080Differences = CountWords(
                diff1080Actual,
                ReadBytes(root1080 / "draw_00148_PS_284_e8e2a8eb_rt0_960x540_f112.bin",
                          size_t(960) * 540 * 4),
                diff1080Maximum);
            Require(ngu.RunNativeSuperResToRgbMain1080(
                        main1080Srv.Get(), source1080Srv.Get(),
                        diff1080Srv.Get(), main1080View.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto main1080Actual =
                Download(device, context.Get(), main1080Texture.Get());
            if (!actualPrefix.empty())
                WriteBytes(actualPrefix + L"1080_main.bin", main1080Actual);
            int main1080Maximum = 0;
            const int main1080Differences = CountWords(
                main1080Actual,
                ReadBytes(root1080 / "draw_00149_PS_4412_c39b49cf_rt0_1920x1080_f36.bin",
                          size_t(1920) * 1080 * 8),
                main1080Maximum);
            Require(ngu.RunNativeSuperResDownX1080(
                        main1080OutputSrv.Get(), downX1080View.Get()),
                    ngu.LastError());
            Require(ngu.RunNativeSuperResDownY1080(
                        downX1080Srv.Get(), downY1080View.Get()),
                    ngu.LastError());
            Require(ngu.RunNativeSuperResDiff1080(
                        original1080Srv.Get(), downY1080Srv.Get(), diff1080View.Get()),
                    ngu.LastError());
            Require(ngu.RunNativeSuperResToRgbBase1080(
                        base1080Srv.Get(), main1080OutputSrv.Get(),
                        diff1080Srv.Get(), baseOutput1080View.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto base1080Actual =
                Download(device, context.Get(), baseOutput1080Texture.Get());
            if (!actualPrefix.empty())
                WriteBytes(actualPrefix + L"1080_base.bin", base1080Actual);
            int base1080Maximum = 0;
            const int base1080Differences = CountWords(
                base1080Actual,
                ReadBytes(root1080 / "draw_00153_PS_4576_b7830945_rt0_1920x1080_f36.bin",
                          size_t(1920) * 1080 * 8),
                base1080Maximum);
            std::printf("1080 DownX different=%d maximum=%d\n",
                        downX1080Differences, downX1080Maximum);
            std::printf("1080 DownY different=%d maximum=%d\n",
                        downY1080Differences, downY1080Maximum);
            std::printf("1080 Diff different=%d maximum=%d\n",
                        diff1080Differences, diff1080Maximum);
            std::printf("1080 Main different=%d maximum=%d\n",
                        main1080Differences, main1080Maximum);
            std::printf("1080 Base different=%d maximum=%d\n",
                        base1080Differences, base1080Maximum);
            Require(downX1080Differences == 0 && downY1080Differences == 0 &&
                        diff1080Differences == 0 && main1080Differences == 0 &&
                        base1080Differences == 0,
                    "1080p SuperChromaRes stage comparison");

            Require(ngu.PrepareNativeLumaVeryHigh1080SuperResFull(d3d->gpu),
                    ngu.LastError());
            auto composed1080Output = CreateTexture(
                device, 3840, 2160, DXGI_FORMAT_B8G8R8A8_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            ComPtr<ID3D11RenderTargetView> composed1080OutputView;
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        composed1080Output.Get(), nullptr, &composed1080OutputView)),
                    "1080p composed output RTV");
            Require(ngu.RunNativeLumaVeryHigh1080SuperResFull(
                        source1080Srv.Get(),
                        original1080Srv.Get(),
                        main1080Srv.Get(),
                        base1080Srv.Get(),
                        composed1080OutputView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto composed1080Actual =
                Download(device, context.Get(), composed1080Output.Get());
            const bool composed1080NonBlack = std::any_of(
                composed1080Actual.begin(), composed1080Actual.end(),
                [](uint8_t value) { return value != 0; });
            Require(composed1080NonBlack, "1080p composed output is black");
            std::printf("1080 Composed nonblack=1\n");
            if (const char* finalOraclePath = std::getenv("NGU_1080_FINAL_ORACLE"))
            {
                const auto finalOracle = ReadBytes(finalOraclePath, size_t(3840) * 2160 * 4);
                int finalMaximum = 0;
                int finalOverOne = 0;
                const int finalDifferent =
                    CompareRgb8(composed1080Actual, finalOracle, finalMaximum, finalOverOne);
                std::printf("1080 Final different=%d maximum=%d diff_gt_1_pixels=%d\n",
                            finalDifferent, finalMaximum, finalOverOne);
                Require(finalMaximum <= 1 && finalOverOne == 0,
                        "1080p final composition comparison");
            }
            else
                std::printf("1080 Final skipped (NGU_1080_FINAL_ORACLE unset)\n");

            Require(ngu.PrepareNativeSuperResStrength3_1080(d3d->gpu), ngu.LastError());
            auto scheduler1080Output = CreateTexture(
                device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            ComPtr<ID3D11RenderTargetView> scheduler1080OutputView;
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        scheduler1080Output.Get(), nullptr, &scheduler1080OutputView)),
                    "1080p scheduler output RTV");
            Require(ngu.RunNativeSuperResStrength3_1080(
                        source1080Srv.Get(),
                        original1080Srv.Get(),
                        main1080Srv.Get(),
                        base1080Srv.Get(),
                        scheduler1080OutputView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            scheduler1080Actual =
                Download(device, context.Get(), scheduler1080Output.Get());
            scheduler1080Differences = CountWords(
                scheduler1080Actual,
                ReadBytes(root1080 / "draw_00153_PS_4576_b7830945_rt0_1920x1080_f36.bin",
                          size_t(1920) * 1080 * 8),
                scheduler1080Maximum);
            scheduler1080Ran = true;
        }

        auto unsupportedOutput = CreateTexture(
            device, 1280, 720, DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> unsupportedOutputView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    unsupportedOutput.Get(), nullptr, &unsupportedOutputView)),
                "unsupported output RTV");
        const bool unsupportedRejected =
            !ngu.RunNativeSuperResStrength3(sourceSrv.Get(),
                                            originalSrv.Get(),
                                            mainOriginalSrv.Get(),
                                            baseOriginalSrv.Get(),
                                            unsupportedOutputView.Get()) &&
            *ngu.LastError();

        std::printf("DownX different=%d maximum=%d\n", downXDifferences, downXMax);
        std::printf("DownY different=%d maximum=%d\n", downYDifferences, downYMax);
        std::printf("Diff different=%d maximum=%d\n", diffDifferences, diffMaximum);
        std::printf("Main different=%d maximum=%d\n", mainDifferences, mainMaximum);
        std::printf("Base different=%d maximum=%d\n", baseDifferences, baseMaximum);
        std::printf("Chain different=%d maximum=%d\n", chainDifferences, chainMaximum);
        std::printf("Scheduler different=%d maximum=%d %.3f ms/frame\n",
                    schedulerDifferences, schedulerMaximum, schedulerMilliseconds);
        if (scheduler1080Ran)
            std::printf("Scheduler1080 different=%d maximum=%d\n",
                        scheduler1080Differences, scheduler1080Maximum);
        else
            std::printf("Scheduler1080 skipped (NGU_1080_CAPTURE_ROOT unset)\n");
        std::printf("SchedulerUnsupported rejected=%d\n", unsupportedRejected);
        if (!actualPrefix.empty())
        {
            WriteBytes(actualPrefix + L"downx.bin", downXActual);
            WriteBytes(actualPrefix + L"downy.bin", downYActual);
            WriteBytes(actualPrefix + L"diff.bin", diffActual);
            WriteBytes(actualPrefix + L"main.bin", mainActual);
            WriteBytes(actualPrefix + L"main_float.bin", mainFloatActual);
            WriteBytes(actualPrefix + L"base.bin", baseActual);
            WriteBytes(actualPrefix + L"chain.bin", chainActual);
            WriteBytes(actualPrefix + L"scheduler.bin", schedulerActual);
            if (scheduler1080Ran)
                WriteBytes(actualPrefix + L"scheduler_1080.bin", scheduler1080Actual);
        }
        const bool pass = downXDifferences == 0 && downYDifferences == 0 && diffDifferences == 0 &&
                          mainDifferences == 0 && baseDifferences == 0 &&
                          chainMaximum <= 1 && schedulerDifferences == 0 && schedulerMaximum == 0 &&
                          unsupportedRejected;
        const bool scheduler1080Pass =
            !scheduler1080Ran ||
            (scheduler1080Differences == 0 && scheduler1080Maximum == 0);
        std::printf("%s native SuperChromaRes primitives\n",
                    pass && scheduler1080Pass ? "PASS_PRODUCT" : "FAIL_PRODUCT");
        ngu.Reset();
        unsupportedOutputView.Reset();
        unsupportedOutput.Reset();
        schedulerOutputView.Reset();
        schedulerOutput.Reset();
        mainOutputSrv.Reset();
        baseOutputView.Reset();
        baseOutput.Reset();
        baseDifferenceSrv.Reset();
        baseDifference.Reset();
        baseWorkSrv.Reset();
        baseWork.Reset();
        baseOriginalSrv.Reset();
        baseOriginal.Reset();
        mainOutputView.Reset();
        mainFloatView.Reset();
        mainFloat.Reset();
        mainOutput.Reset();
        mainDifferenceSrv.Reset();
        mainDifference.Reset();
        mainWorkSrv.Reset();
        mainWork.Reset();
        mainOriginalSrv.Reset();
        mainOriginal.Reset();
        diffView.Reset();
        diffSrv.Reset();
        diffTexture.Reset();
        originalSrv.Reset();
        original.Reset();
        downYSrv.Reset();
        downYView.Reset();
        downYTexture.Reset();
        downXSrv.Reset();
        downXView.Reset();
        downXTexture.Reset();
        sourceSrv.Reset();
        source.Reset();
        context.Reset();
        pl_d3d11_destroy(&d3d);
        return pass && scheduler1080Pass ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
