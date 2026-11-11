#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
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
                           : format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4
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
    const UINT pixelBytes = desc.Format == DXGI_FORMAT_R16_UNORM   ? 2
                            : desc.Format == DXGI_FORMAT_R16G16_UNORM ? 4
                            : desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ? 4
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

int CountComponents(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int& maximum)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0, largest = 0;
    for (size_t i = 0; i < actual.size(); i += 2)
    {
        const uint16_t left = uint16_t(actual[i]) | (uint16_t(actual[i + 1]) << 8);
        const uint16_t right = uint16_t(expected[i]) | (uint16_t(expected[i + 1]) << 8);
        if (left != right)
        {
            ++different;
            largest = std::max(largest, std::abs(int(left) - int(right)));
        }
    }
    maximum = largest;
    return different;
}

struct Mt19937
{
    uint32_t state[624] = {};
    uint32_t index = 624;

    explicit Mt19937(uint32_t seed)
    {
        state[0] = seed;
        for (uint32_t i = 1; i < 624; ++i)
            state[i] = 1812433253u * (state[i - 1] ^ (state[i - 1] >> 30)) + i;
    }

    uint32_t Next()
    {
        static constexpr uint32_t magic[2] = {0u, 0x9908b0dfu};
        if (index >= 624)
        {
            for (uint32_t i = 0; i < 623; ++i)
            {
                const uint32_t mixed = ((state[i] ^ state[i + 1]) & 0x7fffffffu) ^ state[i];
                state[i] = state[(i + 397) % 624] ^ (mixed >> 1) ^ magic[mixed & 1u];
            }
            const uint32_t mixed = ((state[623] ^ state[0]) & 0x7fffffffu) ^ state[623];
            state[623] = state[396] ^ (mixed >> 1) ^ magic[mixed & 1u];
            index = 0;
        }
        uint32_t value = state[index++];
        value ^= value >> 11;
        value ^= (value & 0xff3a58adu) << 7;
        value ^= (value & 0xffffdf8cu) << 15;
        return value ^ (value >> 18);
    }
};

int CountPixelDifferences(const std::vector<uint8_t>& actual,
                          const std::vector<uint8_t>& expected,
                          int& maximum,
                          int& overOne)
{
    Require(actual.size() == expected.size() && actual.size() % 4 == 0, "final comparison size");
    maximum = 0;
    overOne = 0;
    int different = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 4)
    {
        bool pixelOverOne = false;
        for (size_t channel = 0; channel < 4; ++channel)
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
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: test_ngu_finish <capture-root> <actual-prefix>");
        const std::filesystem::path root(argv[1]);
        const std::wstring actualPrefix = std::filesystem::path(argv[2]).wstring();

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        NguScaler ngu;
        NguDither ditherPhases;
        const auto firstPhase = ditherPhases.NextPhase();
        const auto secondPhase = ditherPhases.NextPhase();
        Require(firstPhase.y == 41.0f / 32767.0f, "first dither y");
        Require(firstPhase.z == 18467.0f / 32767.0f, "first dither z");
        Require(secondPhase.y == 6334.0f / 32767.0f, "second dither y");
        Require(secondPhase.z == 26500.0f / 32767.0f, "second dither z");
        ditherPhases.Reset();
        Require(ditherPhases.NextPhase().y == firstPhase.y, "dither reset");
        const bool prepared = ngu.PrepareNativeLumaFinish(d3d->gpu);
        Require(prepared, ngu.LastError());

        const auto xInputBytes =
            ReadBytes(root / "input_31724_PS_1284_386cc8d7_s0.bin", size_t(1920) * 1080 * 4);
        std::vector<uint16_t> xInputExpanded(size_t(1920) * 1080 * 4);
        for (size_t i = 0; i < size_t(1920) * 1080; ++i)
        {
            xInputExpanded[i * 4] = uint16_t(xInputBytes[i * 4]) | (uint16_t(xInputBytes[i * 4 + 1]) << 8);
            xInputExpanded[i * 4 + 1] = uint16_t(xInputBytes[i * 4 + 2]) | (uint16_t(xInputBytes[i * 4 + 3]) << 8);
            xInputExpanded[i * 4 + 2] = 65535;
            xInputExpanded[i * 4 + 3] = 65535;
        }
        auto xInput = CreateTexture(
            device, 1920, 1080, DXGI_FORMAT_R16G16B16A16_UNORM, D3D11_BIND_SHADER_RESOURCE,
            xInputExpanded.data());
        ComPtr<ID3D11ShaderResourceView> xInputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(xInput.Get(), nullptr, &xInputView)), "X input SRV");
        auto xOutput = CreateTexture(
            device, 3840, 1080, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> xView;
        ComPtr<ID3D11ShaderResourceView> xSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(xOutput.Get(), nullptr, &xView)), "X RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(xOutput.Get(), nullptr, &xSrv)), "X SRV");
        const bool xRan = ngu.RunNativeLumaUpX(xInputView.Get(), xView.Get());
        Require(xRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto xActual = Download(device, context.Get(), xOutput.Get());
        int xMaximum = 0;
        const int xDifferent = CountComponents(
            xActual,
            ReadBytes(root / "draw_31724_PS_1284_386cc8d7_rt0_3840x1080_f36.bin", size_t(3840) * 1080 * 8),
            xMaximum);
        auto yOutput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> yView;
        ComPtr<ID3D11ShaderResourceView> ySrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(yOutput.Get(), nullptr, &yView)), "Y RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(yOutput.Get(), nullptr, &ySrv)), "Y SRV");
        const bool yRan = ngu.RunNativeLumaUpY(xSrv.Get(), yView.Get());
        Require(yRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto yActual = Download(device, context.Get(), yOutput.Get());
        int yMaximum = 0;
        const int yDifferent = CountComponents(
            yActual,
            ReadBytes(root / "draw_31725_PS_1284_dc97a0d0_rt0_3840x2160_f36.bin", size_t(3840) * 2160 * 8),
            yMaximum);

        const auto lumaCapture =
            ReadBytes(root / "input_31726_PS_548_d6670d4b_s0.bin", size_t(3840) * 2160 * 8);
        std::vector<uint16_t> luma(size_t(3840) * 2160);
        for (size_t i = 0; i < luma.size(); ++i)
            std::memcpy(&luma[i], lumaCapture.data() + i * 8, sizeof(uint16_t));
        auto lumaInput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16_UNORM, D3D11_BIND_SHADER_RESOURCE, luma.data());
        ComPtr<ID3D11ShaderResourceView> lumaSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(lumaInput.Get(), nullptr, &lumaSrv)), "luma SRV");
        auto mergeOutput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> mergeView;
        Require(SUCCEEDED(device->CreateRenderTargetView(mergeOutput.Get(), nullptr, &mergeView)), "merge RTV");
        const bool mergeRan = ngu.RunNativeLumaMerge(lumaSrv.Get(), ySrv.Get(), mergeView.Get());
        Require(mergeRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto mergeActual = Download(device, context.Get(), mergeOutput.Get());
        int mergeMaximum = 0;
        const int mergeDifferent = CountComponents(
            mergeActual,
            ReadBytes(root / "draw_31726_PS_548_d6670d4b_rt0_3840x2160_f36.bin", size_t(3840) * 2160 * 8),
            mergeMaximum);

        const bool finalPrepared = ngu.PrepareNativeLumaFinal(d3d->gpu);
        Require(finalPrepared, ngu.LastError());
        auto finalInputBytes = mergeActual;
        auto finalInput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_SHADER_RESOURCE, finalInputBytes.data());
        ComPtr<ID3D11ShaderResourceView> finalInputSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    finalInput.Get(), nullptr, &finalInputSrv)),
                "final input SRV");
        std::vector<uint8_t> ditherBytes(size_t(3840) * 2160 * 4);
        Mt19937 ditherRandom(1);
        for (size_t pixel = 0; pixel < size_t(3840) * 2160; ++pixel)
        {
            const uint32_t word = ditherRandom.Next();
            std::memcpy(ditherBytes.data() + pixel * 4, &word, sizeof(word));
        }
        auto dither = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_SHADER_RESOURCE, ditherBytes.data());
        ComPtr<ID3D11ShaderResourceView> ditherSrv;
        Require(SUCCEEDED(device->CreateShaderResourceView(dither.Get(), nullptr, &ditherSrv)),
                "dither SRV");
        auto finalOutput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_B8G8R8A8_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> finalView;
        Require(SUCCEEDED(device->CreateRenderTargetView(finalOutput.Get(), nullptr, &finalView)),
                "final RTV");
        const bool finalRan = ngu.RunNativeLumaFinal(finalInputSrv.Get(), ditherSrv.Get(), finalView.Get());
        Require(finalRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto finalActual = Download(device, context.Get(), finalOutput.Get());
        int finalMaximum = 0, finalOverOne = 0;
        const int finalDifferent = CountPixelDifferences(
            finalActual,
            ReadBytes(root / "draw_31727_PS_1352_6992f6f0_rt0_3840x2160_f21.bin", size_t(3840) * 2160 * 4),
            finalMaximum,
            finalOverOne);

        ngu.ResetDitherPhase();
        const bool dynamicFinalRan =
            ngu.RunNativeLumaFinalNextPhase(finalInputSrv.Get(), ditherSrv.Get(), finalView.Get());
        Require(dynamicFinalRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto dynamicFinalActual = Download(device, context.Get(), finalOutput.Get());
        int dynamicFinalMaximum = 0, dynamicFinalOverOne = 0;
        const int dynamicFinalDifferent = CountPixelDifferences(
            dynamicFinalActual,
            ReadBytes(root / "draw_31727_PS_1352_6992f6f0_rt0_3840x2160_f21.bin", size_t(3840) * 2160 * 4),
            dynamicFinalMaximum,
            dynamicFinalOverOne);

        ngu.ResetDitherPhase();
        const bool ownedFinalRan =
            ngu.RunNativeLumaFinalOwnedDither(finalInputSrv.Get(), finalView.Get());
        Require(ownedFinalRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto ownedFinalActual = Download(device, context.Get(), finalOutput.Get());
        int ownedFinalMaximum = 0, ownedFinalOverOne = 0;
        const int ownedFinalDifferent = CountPixelDifferences(
            ownedFinalActual,
            ReadBytes(root / "draw_31727_PS_1352_6992f6f0_rt0_3840x2160_f21.bin", size_t(3840) * 2160 * 4),
            ownedFinalMaximum,
            ownedFinalOverOne);

        const auto schedulerSourceCapture =
            ReadBytes(root / "input_31706_PS_1412_aad01c2f_s0.bin", size_t(1920) * 1080 * 8);
        std::vector<uint16_t> schedulerSource(size_t(1920) * 1080);
        for (size_t i = 0; i < schedulerSource.size(); ++i)
            std::memcpy(
                &schedulerSource[i], schedulerSourceCapture.data() + i * 8, sizeof(uint16_t));
        auto schedulerSourceTexture = CreateTexture(
            device,
            1920,
            1080,
            DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_SHADER_RESOURCE,
            schedulerSource.data());
        ComPtr<ID3D11ShaderResourceView> schedulerSourceView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    schedulerSourceTexture.Get(), nullptr, &schedulerSourceView)),
                "scheduler source SRV");
        Require(ngu.PrepareNativeLumaVeryHigh1080(d3d->gpu), ngu.LastError());
        const bool schedulerRan = ngu.RunNativeLumaVeryHigh1080(
            schedulerSourceView.Get(), xInputView.Get(), 1920, 1080, finalView.Get(), false);
        Require(schedulerRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto schedulerFinalActual = Download(device, context.Get(), finalOutput.Get());
        int schedulerFinalMaximum = 0, schedulerFinalOverOne = 0;
        const int schedulerFinalDifferent = CountPixelDifferences(
            schedulerFinalActual,
            ReadBytes(root / "draw_31727_PS_1352_6992f6f0_rt0_3840x2160_f21.bin", size_t(3840) * 2160 * 4),
            schedulerFinalMaximum,
            schedulerFinalOverOne);
        auto schedulerAddTexture = CreateTexture(
            device,
            3840,
            2160,
            DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> schedulerAddView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    schedulerAddTexture.Get(), nullptr, &schedulerAddView)),
                "scheduler add RTV");
        Require(ngu.CopyNativeLumaVeryHighAdd(schedulerAddView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto schedulerAddActual = Download(device, context.Get(), schedulerAddTexture.Get());
        int schedulerAddMaximum = 0;
        const auto schedulerAddCapture =
            ReadBytes(root / "draw_31723_PS_564_500335c8_rt0_3840x2160_f81.bin", size_t(3840) * 2160 * 8);
        std::vector<uint8_t> schedulerAddExpected(size_t(3840) * 2160 * 2);
        for (size_t i = 0; i < size_t(3840) * 2160; ++i)
            std::memcpy(schedulerAddExpected.data() + i * 2, schedulerAddCapture.data() + i * 8, 2);
        const int schedulerAddDifferent = CountComponents(
            schedulerAddActual,
            schedulerAddExpected,
            schedulerAddMaximum);
        auto schedulerUpXTexture = CreateTexture(
            device,
            3840,
            1080,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> schedulerUpXView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    schedulerUpXTexture.Get(), nullptr, &schedulerUpXView)),
                "scheduler UpX RTV");
        Require(ngu.CopyNativeLumaVeryHighUpX(schedulerUpXView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto schedulerUpXActual = Download(device, context.Get(), schedulerUpXTexture.Get());
        int schedulerUpXMaximum = 0;
        const int schedulerUpXDifferent = CountComponents(
            schedulerUpXActual,
            ReadBytes(root / "draw_31724_PS_1284_386cc8d7_rt0_3840x1080_f36.bin", size_t(3840) * 1080 * 8),
            schedulerUpXMaximum);
        auto schedulerUpYTexture = CreateTexture(
            device,
            3840,
            2160,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> schedulerUpYView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    schedulerUpYTexture.Get(), nullptr, &schedulerUpYView)),
                "scheduler UpY RTV");
        Require(ngu.CopyNativeLumaVeryHighUpY(schedulerUpYView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto schedulerUpYActual = Download(device, context.Get(), schedulerUpYTexture.Get());
        int schedulerUpYMaximum = 0;
        const int schedulerUpYDifferent = CountComponents(
            schedulerUpYActual,
            ReadBytes(root / "draw_31725_PS_1284_dc97a0d0_rt0_3840x2160_f36.bin", size_t(3840) * 2160 * 8),
            schedulerUpYMaximum);
        auto schedulerMergeTexture = CreateTexture(
            device,
            3840,
            2160,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> schedulerMergeView;
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    schedulerMergeTexture.Get(), nullptr, &schedulerMergeView)),
                "scheduler merge RTV");
        Require(ngu.CopyNativeLumaVeryHighMerge(schedulerMergeView.Get()), ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        auto schedulerMergeActual = Download(device, context.Get(), schedulerMergeTexture.Get());
        int schedulerMergeMaximum = 0;
        const int schedulerMergeDifferent = CountComponents(
            schedulerMergeActual,
            ReadBytes(root / "draw_31726_PS_548_d6670d4b_rt0_3840x2160_f36.bin", size_t(3840) * 2160 * 8),
            schedulerMergeMaximum);

        std::printf("UpX different=%d maximum=%d\n", xDifferent, xMaximum);
        std::printf("UpY different=%d maximum=%d\n", yDifferent, yMaximum);
        std::printf("Merge different=%d maximum=%d\n", mergeDifferent, mergeMaximum);
        std::printf("Final different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    finalDifferent, finalMaximum, finalOverOne);
        std::printf("FinalDynamic different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    dynamicFinalDifferent, dynamicFinalMaximum, dynamicFinalOverOne);
        std::printf("FinalOwned different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    ownedFinalDifferent, ownedFinalMaximum, ownedFinalOverOne);
        std::printf("SchedulerFinal different=%d maximum=%d diff_gt_1_pixels=%d\n",
                    schedulerFinalDifferent, schedulerFinalMaximum, schedulerFinalOverOne);
        std::printf("SchedulerAdd different=%d maximum=%d\n",
                    schedulerAddDifferent, schedulerAddMaximum);
        std::printf("SchedulerUpX different=%d maximum=%d\n",
                    schedulerUpXDifferent, schedulerUpXMaximum);
        std::printf("SchedulerUpY different=%d maximum=%d\n",
                    schedulerUpYDifferent, schedulerUpYMaximum);
        std::printf("SchedulerMerge different=%d maximum=%d\n",
                    schedulerMergeDifferent, schedulerMergeMaximum);
        const bool pass = xDifferent == 0 && yDifferent == 0 && mergeDifferent == 0 &&
                          finalMaximum <= 1 && finalOverOne == 0 &&
                          dynamicFinalMaximum <= 1 && dynamicFinalOverOne == 0 &&
                          ownedFinalMaximum <= 1 && ownedFinalOverOne == 0 &&
                          schedulerFinalMaximum <= 1 && schedulerFinalOverOne == 0 &&
                          schedulerAddDifferent == 0 && schedulerAddMaximum == 0 &&
                          schedulerUpXDifferent == 0 && schedulerUpXMaximum == 0 &&
                          schedulerUpYDifferent == 0 && schedulerUpYMaximum == 0 &&
                          schedulerMergeDifferent == 0 && schedulerMergeMaximum == 0;
        for (const auto& [name, bytes] :
             {std::pair{L"upx", xActual}, std::pair{L"upy", yActual},
              std::pair{L"merge", mergeActual}, std::pair{L"final", finalActual},
              std::pair{L"final_dynamic", dynamicFinalActual},
              std::pair{L"final_owned", ownedFinalActual},
              std::pair{L"final_scheduler", schedulerFinalActual},
              std::pair{L"final_scheduler_upx", schedulerUpXActual},
              std::pair{L"final_scheduler_upy", schedulerUpYActual},
              std::pair{L"final_scheduler_merge", schedulerMergeActual}})
        {
            std::ofstream file(actualPrefix + name + L".bin", std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        }
        std::printf("%s native NGU 4x reconstruct + merge + final\n",
                    pass ? "PASS_PRODUCT" : "FAIL_PRODUCT");
        ngu.Reset();
        finalView.Reset();
        finalOutput.Reset();
        ditherSrv.Reset();
        dither.Reset();
        finalInputSrv.Reset();
        finalInput.Reset();
        mergeView.Reset();
        mergeOutput.Reset();
        lumaSrv.Reset();
        lumaInput.Reset();
        ySrv.Reset();
        yView.Reset();
        yOutput.Reset();
        xSrv.Reset();
        xView.Reset();
        xOutput.Reset();
        xInputView.Reset();
        xInput.Reset();
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
