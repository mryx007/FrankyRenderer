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
        }
        const bool pass = downXDifferences == 0 && downYDifferences == 0 && diffDifferences == 0 &&
                          mainDifferences == 0 && baseDifferences == 0 &&
                          chainMaximum <= 1 && schedulerDifferences == 0 && schedulerMaximum == 0 &&
                          unsupportedRejected;
        std::printf("%s native SuperChromaRes primitives\n", pass ? "PASS_PRODUCT" : "FAIL_PRODUCT");
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
        return pass ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
