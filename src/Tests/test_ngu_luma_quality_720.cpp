#define NOMINMAX
#include "Renderer/NguScaler.h"
#include <libplacebo/d3d11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdlib>
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
void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path, size_t bytes)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), path.string() + ": open failed");
    Require(size_t(file.tellg()) == bytes, path.string() + ": unexpected size");
    file.seekg(0);
    std::vector<uint8_t> data(bytes);
    Require(bool(file.read(reinterpret_cast<char*>(data.data()), std::streamsize(bytes))),
            path.string() + ": read failed");
    return data;
}

ComPtr<ID3D11Texture2D>
CreateTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, const void* data)
{
    const UINT pixelBytes = format == DXGI_FORMAT_R16_UNORM ? 2 : 8;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = data;
    initial.SysMemPitch = width * pixelBytes;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture)), "CreateTexture2D failed");
    return texture;
}

std::vector<uint8_t> Download(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    const UINT pixelBytes =
        desc.Format == DXGI_FORMAT_R16_UNORM ? 2 : desc.Format == DXGI_FORMAT_R16G16B16A16_UNORM ||
                                                     desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT
                                                 ? 8
                                                 : 0;
    Require(pixelBytes != 0, "unsupported download format");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "staging texture failed");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map failed");
    const size_t rowBytes = size_t(desc.Width) * pixelBytes;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

struct SurfaceGeometry
{
    UINT width;
    UINT height;
    DXGI_FORMAT format;
};

SurfaceGeometry Geometry(NguLumaQuality720Surface surface)
{
    switch (surface)
    {
    case NguLumaQuality720Surface::Initial:
    case NguLumaQuality720Surface::Mixer:
    case NguLumaQuality720Surface::FirstGroup0:
    case NguLumaQuality720Surface::FirstGroup1:
        return {1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT};
    case NguLumaQuality720Surface::Upsample:
        return {1282, 722, DXGI_FORMAT_R16G16B16A16_FLOAT};
    case NguLumaQuality720Surface::Convert:
    case NguLumaQuality720Surface::SecondGroup0:
    case NguLumaQuality720Surface::SecondGroup1:
        return {2560, 1440, DXGI_FORMAT_R16G16B16A16_FLOAT};
    case NguLumaQuality720Surface::Dense:
        return {2562, 1442, DXGI_FORMAT_R16G16B16A16_FLOAT};
    case NguLumaQuality720Surface::Add:
        return {5120, 2880, DXGI_FORMAT_R16_UNORM};
    case NguLumaQuality720Surface::TailX:
        return {3840, 2880, DXGI_FORMAT_R16G16B16A16_FLOAT};
    case NguLumaQuality720Surface::TailY:
        return {3840, 2160, DXGI_FORMAT_R16G16B16A16_FLOAT};
    }
    throw std::runtime_error("unknown surface geometry");
}

struct Difference
{
    size_t different = 0;
    uint16_t maximum = 0;
};

Difference Measure(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
{
    Require(actual.size() == expected.size(), "comparison size");
    Difference difference;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left = uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right = uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        if (left == right)
            continue;
        ++difference.different;
        difference.maximum =
            std::max(difference.maximum, uint16_t(std::abs(int(left) - int(right))));
    }
    return difference;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        std::string selected;
        if (argc == 3 && std::string(argv[1]) == "--quality")
            selected = argv[2];
        Require(argc == 1 || !selected.empty(), "usage: test_ngu_luma_quality_720 [--quality low|med|high]");

        const auto captureRoot =
            std::filesystem::path(LR"(E:\OpenMadVR\artifacts_20261031\openmadvr\ngu_chroma_capture)");
        const auto source = ReadBytes(
            captureRoot / L"luma_quality_fixture_720_v1/low/draw_0139_PS_460_bfe50921_rt0_1280x720_f81_1843200.bin",
            size_t(1280) * 720 * 2);

        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Require(d3d != nullptr, "D3D11 device unavailable");
        ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);

        auto input = CreateTexture(d3d->device, 1280, 720, DXGI_FORMAT_R16_UNORM, source.data());
        ComPtr<ID3D11ShaderResourceView> inputView;
        Require(SUCCEEDED(d3d->device->CreateShaderResourceView(input.Get(), nullptr, &inputView)), "input SRV");

        NguScaler ngu;

        struct Surface
        {
            NguLumaQuality720Surface surface;
            int index;
            std::filesystem::path expected;
        };

        struct Case
        {
            NguQuality quality;
            const char* name;
            std::vector<Surface> surfaces;
        };

        const auto lowRoot = captureRoot / L"luma_quality_targets_720_v11/low";
        const auto medRoot = captureRoot / L"luma_quality_targets_720_v4/med";
        const auto medTailRoot = captureRoot / L"luma_quality_targets_720_v10/med";
        const auto highRoot = captureRoot / L"luma_quality_targets_720_v8/high";
        const auto highTailRoot = captureRoot / L"luma_quality_targets_720_v16/high";
        const std::vector<Case> cases = {
            {NguQuality::Low,
             "low",
             {
                 {NguLumaQuality720Surface::Initial, 0,
                  lowRoot / L"draw_0140_PS_1412_aad01c2f_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Initial, 1,
                  lowRoot / L"draw_0140_PS_1412_aad01c2f_rt1_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Initial, 2,
                  lowRoot / L"draw_0140_PS_1412_aad01c2f_rt2_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 0,
                  lowRoot / L"draw_0143_PS_4280_d591580d_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 1,
                  lowRoot / L"draw_0143_PS_4280_d591580d_rt1_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup1, 0,
                  lowRoot / L"draw_0144_PS_4280_d591580d_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup1, 1,
                  lowRoot / L"draw_0144_PS_4280_d591580d_rt1_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Upsample, 0,
                  lowRoot / L"draw_0145_PS_3516_f481ce19_rt0_1282x722_f113_7404832.bin"},
                 {NguLumaQuality720Surface::Upsample, 4,
                  lowRoot / L"draw_0146_PS_3516_f481ce19_rt0_1282x722_f113_7404832.bin"},
                 {NguLumaQuality720Surface::Convert, 0,
                  lowRoot / L"draw_0147_PS_912_86f81c64_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::Convert, 1,
                  lowRoot / L"draw_0148_PS_912_86f81c64_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup1, 0,
                  lowRoot / L"draw_0149_PS_4280_d591580d_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup1, 1,
                  lowRoot / L"draw_0149_PS_4280_d591580d_rt1_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::Dense, 0,
                  lowRoot / L"draw_0150_PS_1528_f0e4bb92_rt0_2562x1442_f113_29555232.bin"},
                 {NguLumaQuality720Surface::Add, 0,
                  lowRoot / L"draw_0151_PS_564_500335c8_rt0_5120x2880_f81_29491200.bin"},
                 {NguLumaQuality720Surface::TailX, 0,
                  lowRoot / L"draw_0152_PS_1416_6ae3afa3_rt0_3840x2880_f36_88473600.bin"},
                 {NguLumaQuality720Surface::TailY, 0,
                  lowRoot / L"draw_0153_PS_1404_7b76a575_rt0_3840x2160_f36_66355200.bin"},
             }},
            {NguQuality::Medium,
             "med",
             {
                 {NguLumaQuality720Surface::Initial, 0,
                  medRoot / L"draw_0132_PS_1412_aad01c2f_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Initial, 1,
                  medRoot / L"draw_0132_PS_1412_aad01c2f_rt1_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Initial, 2,
                  medRoot / L"draw_0132_PS_1412_aad01c2f_rt2_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup1, 0,
                  medRoot / L"draw_0142_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup1, 1,
                  medRoot / L"draw_0143_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup1, 2,
                  medRoot / L"draw_0144_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 0,
                  medRoot / L"draw_0145_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 1,
                  medRoot / L"draw_0146_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 2,
                  medRoot / L"draw_0147_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Upsample, 0,
                  medRoot / L"draw_0148_PS_4920_9257dfe6_rt0_1282x722_f113_7404832.bin"},
                 {NguLumaQuality720Surface::Upsample, 4,
                  medRoot / L"draw_0149_PS_4920_9257dfe6_rt0_1282x722_f113_7404832.bin"},
                 {NguLumaQuality720Surface::Convert, 0,
                  medRoot / L"draw_0150_PS_912_86f81c64_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::Convert, 1,
                  medRoot / L"draw_0151_PS_912_86f81c64_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup1, 0,
                  medRoot / L"draw_0152_PS_4280_d591580d_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup1, 1,
                  medRoot / L"draw_0152_PS_4280_d591580d_rt1_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup0, 0,
                  medRoot / L"draw_0153_PS_4280_d591580d_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup0, 1,
                  medRoot / L"draw_0153_PS_4280_d591580d_rt1_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::Dense, 0,
                  medTailRoot / L"draw_0154_PS_1528_f0e4bb92_rt0_2562x1442_f113_29555232.bin"},
                 {NguLumaQuality720Surface::Add, 0,
                  medTailRoot / L"draw_0155_PS_564_500335c8_rt0_5120x2880_f81_29491200.bin"},
                 {NguLumaQuality720Surface::TailX, 0,
                  medTailRoot / L"draw_0156_PS_1416_6ae3afa3_rt0_3840x2880_f36_88473600.bin"},
                 {NguLumaQuality720Surface::TailY, 0,
                  medTailRoot / L"draw_0157_PS_1404_7b76a575_rt0_3840x2160_f36_66355200.bin"},
             }},
            {NguQuality::High,
             "high",
             {
                 {NguLumaQuality720Surface::Initial, 0,
                  highRoot / L"draw_0136_PS_1412_aad01c2f_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Initial, 1,
                  highRoot / L"draw_0136_PS_1412_aad01c2f_rt1_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Initial, 2,
                  highRoot / L"draw_0136_PS_1412_aad01c2f_rt2_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 0,
                  highRoot / L"draw_0149_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 1,
                  highRoot / L"draw_0150_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::FirstGroup0, 2,
                  highRoot / L"draw_0151_PS_3588_80fb311e_rt0_1280x720_f113_7372800.bin"},
                 {NguLumaQuality720Surface::Upsample, 0,
                  highRoot / L"draw_0152_PS_4920_9257dfe6_rt0_1282x722_f113_7404832.bin"},
                 {NguLumaQuality720Surface::Upsample, 4,
                  highRoot / L"draw_0153_PS_4920_9257dfe6_rt0_1282x722_f113_7404832.bin"},
                 {NguLumaQuality720Surface::Upsample, 8,
                  highRoot / L"draw_0154_PS_4920_9257dfe6_rt0_1282x722_f113_7404832.bin"},
                 {NguLumaQuality720Surface::Convert, 0,
                  highRoot / L"draw_0155_PS_912_86f81c64_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::Convert, 1,
                  highRoot / L"draw_0156_PS_912_86f81c64_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::Convert, 2,
                  highRoot / L"draw_0157_PS_912_86f81c64_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup0, 0,
                  highRoot / L"draw_0158_PS_3588_80fb311e_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup0, 1,
                  highRoot / L"draw_0159_PS_3588_80fb311e_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup0, 2,
                  highRoot / L"draw_0160_PS_3588_80fb311e_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup1, 0,
                  highRoot / L"draw_0161_PS_3588_80fb311e_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup1, 1,
                  highRoot / L"draw_0162_PS_3588_80fb311e_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::SecondGroup1, 2,
                  highRoot / L"draw_0163_PS_3588_80fb311e_rt0_2560x1440_f113_29491200.bin"},
                 {NguLumaQuality720Surface::Dense, 0,
                  highRoot / L"draw_0164_PS_2100_ba9671a7_rt0_2562x1442_f113_29555232.bin"},
                 {NguLumaQuality720Surface::Add, 0,
                  highRoot / L"draw_0165_PS_564_500335c8_rt0_5120x2880_f81_29491200.bin"},
                 {NguLumaQuality720Surface::TailX, 0,
                  highTailRoot / L"draw_0166_PS_1612_a3d450ae_rt0_3840x2880_f36_88473600.bin"},
                 {NguLumaQuality720Surface::TailY, 0,
                  highTailRoot / L"draw_0167_PS_1576_5e089fd4_rt0_3840x2160_f36_66355200.bin"},
             }},
        };

        bool passed = true;
        for (const auto& item : cases)
        {
            if (!selected.empty() && selected != item.name)
                continue;
            std::printf("prepare %s\n", item.name);
            std::fflush(stdout);
            Require(ngu.PrepareNativeLumaQuality720(d3d->gpu, item.quality), ngu.LastError());
            std::printf("render %s\n", item.name);
            std::fflush(stdout);
            auto output = CreateTexture(d3d->device, 3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM, nullptr);
            ComPtr<ID3D11RenderTargetView> outputView;
            Require(SUCCEEDED(d3d->device->CreateRenderTargetView(output.Get(), nullptr, &outputView)), "output RTV");
            Require(ngu.RunNativeLumaQuality720LumaForTest(inputView.Get(), item.quality, outputView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            std::printf("compare %s\n", item.name);
            std::fflush(stdout);
            const auto prefixRoot =
                item.quality == NguQuality::Low ? lowRoot : item.quality == NguQuality::Medium ? medRoot : highRoot;
            const auto compareExternal = [&](const char* label,
                                             const std::filesystem::path& expectedPath,
                                             UINT width,
                                             UINT height,
                                             DXGI_FORMAT format,
                                             ID3D11RenderTargetView* renderTarget)
            {
                const auto expected = ReadBytes(expectedPath, std::filesystem::file_size(expectedPath));
                auto texture = CreateTexture(d3d->device, width, height, format, nullptr);
                ComPtr<ID3D11Resource> source;
                renderTarget->GetResource(&source);
                Require(bool(source), "surface source resource");
                context->CopyResource(texture.Get(), source.Get());
                pl_gpu_finish(d3d->gpu);
                const auto difference =
                    Measure(Download(d3d->device, context.Get(), texture.Get()), expected);
                std::printf("%s %s different=%zu maximum=%u\n",
                            item.name,
                            label,
                            difference.different,
                            unsigned(difference.maximum));
            };
            auto scaleX = CreateTexture(d3d->device, 5120, 720, DXGI_FORMAT_R16_UNORM, nullptr);
            auto scaleY = CreateTexture(d3d->device, 5120, 2880, DXGI_FORMAT_R16_UNORM, nullptr);
            ComPtr<ID3D11RenderTargetView> scaleXView, scaleYView;
            Require(SUCCEEDED(d3d->device->CreateRenderTargetView(scaleX.Get(), nullptr, &scaleXView)) &&
                        SUCCEEDED(d3d->device->CreateRenderTargetView(scaleY.Get(), nullptr, &scaleYView)) &&
                        ngu.CopyNativeLuma4x720ScaleX(scaleXView.Get()) &&
                        ngu.CopyNativeLuma4x720ScaleY(scaleYView.Get()),
                    ngu.LastError());
            pl_gpu_finish(d3d->gpu);
            const auto scaleXExpected =
                item.quality == NguQuality::Low
                    ? lowRoot / L"draw_0138_PS_520_4a9505a4_rt0_5120x720_f81_7372800.bin"
                    : item.quality == NguQuality::Medium
                          ? medRoot / L"draw_0130_PS_520_4a9505a4_rt0_5120x720_f81_7372800.bin"
                          : highRoot / L"draw_0134_PS_520_4a9505a4_rt0_5120x720_f81_7372800.bin";
            const auto scaleYExpected =
                item.quality == NguQuality::Low
                    ? lowRoot / L"draw_0139_PS_520_4ee2414c_rt0_5120x2880_f81_29491200.bin"
                    : item.quality == NguQuality::Medium
                          ? medRoot / L"draw_0131_PS_520_4ee2414c_rt0_5120x2880_f81_29491200.bin"
                          : highRoot / L"draw_0135_PS_520_4ee2414c_rt0_5120x2880_f81_29491200.bin";
            compareExternal("scaleX",
                            scaleXExpected,
                            5120,
                            720,
                            DXGI_FORMAT_R16_UNORM,
                            scaleXView.Get());
            compareExternal("scaleY",
                            scaleYExpected,
                            5120,
                            2880,
                            DXGI_FORMAT_R16_UNORM,
                            scaleYView.Get());
            if (item.quality == NguQuality::Low)
            {
                const auto targetAdd =
                    ReadBytes(lowRoot / L"draw_0151_PS_564_500335c8_rt0_5120x2880_f81_29491200.bin",
                              size_t(5120) * 2880 * 2);
                auto targetAddTexture =
                    CreateTexture(d3d->device, 5120, 2880, DXGI_FORMAT_R16_UNORM, targetAdd.data());
                ComPtr<ID3D11ShaderResourceView> targetAddView;
                Require(SUCCEEDED(
                            d3d->device->CreateShaderResourceView(targetAddTexture.Get(), nullptr, &targetAddView)),
                        "target add SRV");
                auto tailX =
                    CreateTexture(d3d->device, 3840, 2880, DXGI_FORMAT_R16G16B16A16_UNORM, nullptr);
                ComPtr<ID3D11RenderTargetView> tailXView;
                Require(SUCCEEDED(d3d->device->CreateRenderTargetView(tailX.Get(), nullptr, &tailXView)),
                        "tail X test RTV");
                Require(ngu.RunNativeLumaQuality720TailForTest(
                            item.quality, false, targetAddView.Get(), tailXView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto expectedTailX =
                    ReadBytes(lowRoot / L"draw_0152_PS_1416_6ae3afa3_rt0_3840x2880_f36_88473600.bin",
                              size_t(3840) * 2880 * 8);
                const auto tailXDifference =
                    Measure(Download(d3d->device, context.Get(), tailX.Get()), expectedTailX);
                std::printf("low tail-X captured-input different=%zu maximum=%u\n",
                            tailXDifference.different,
                            unsigned(tailXDifference.maximum));
                Require(tailXDifference.different == 0 && tailXDifference.maximum == 0,
                        "captured-input tail X mismatch");

                auto targetTailX =
                    CreateTexture(d3d->device, 3840, 2880, DXGI_FORMAT_R16G16B16A16_UNORM, expectedTailX.data());
                ComPtr<ID3D11ShaderResourceView> targetTailXView;
                Require(SUCCEEDED(
                            d3d->device->CreateShaderResourceView(targetTailX.Get(), nullptr, &targetTailXView)),
                        "target tail X SRV");
                auto tailY =
                    CreateTexture(d3d->device, 3840, 2160, DXGI_FORMAT_R16G16B16A16_UNORM, nullptr);
                ComPtr<ID3D11RenderTargetView> tailYView;
                Require(SUCCEEDED(d3d->device->CreateRenderTargetView(tailY.Get(), nullptr, &tailYView)),
                        "tail Y test RTV");
                Require(ngu.RunNativeLumaQuality720TailForTest(
                            item.quality, true, targetTailXView.Get(), tailYView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto expectedTailY =
                    ReadBytes(lowRoot / L"draw_0153_PS_1404_7b76a575_rt0_3840x2160_f36_66355200.bin",
                              size_t(3840) * 2160 * 8);
                const auto tailYDifference =
                    Measure(Download(d3d->device, context.Get(), tailY.Get()), expectedTailY);
                std::printf("low tail-Y captured-input different=%zu maximum=%u\n",
                            tailYDifference.different,
                            unsigned(tailYDifference.maximum));
                Require(tailYDifference.different == 0 && tailYDifference.maximum == 0,
                        "captured-input tail Y mismatch");

                const auto expected0 =
                    ReadBytes(lowRoot / L"draw_0141_PS_6140_e8f1d399_rt0_1280x720_f113_7372800.bin",
                              size_t(1280) * 720 * 8);
                const auto expected1 =
                    ReadBytes(lowRoot / L"draw_0141_PS_6140_e8f1d399_rt1_1280x720_f113_7372800.bin",
                              size_t(1280) * 720 * 8);
                static constexpr int order[3] = {0, 1, 2};
                auto mixer0 =
                    CreateTexture(d3d->device, 1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr);
                auto mixer1 =
                    CreateTexture(d3d->device, 1280, 720, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr);
                ComPtr<ID3D11RenderTargetView> mixerView0, mixerView1;
                Require(SUCCEEDED(
                            d3d->device->CreateRenderTargetView(mixer0.Get(), nullptr, &mixerView0)) &&
                            SUCCEEDED(
                                d3d->device->CreateRenderTargetView(mixer1.Get(), nullptr, &mixerView1)),
                        "mixer output RTV");
                Require(ngu.RunNativeLumaQuality720MixerForTest(
                            item.quality, order, mixerView0.Get(), mixerView1.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto difference0 =
                    Measure(Download(d3d->device, context.Get(), mixer0.Get()), expected0);
                const auto difference1 =
                    Measure(Download(d3d->device, context.Get(), mixer1.Get()), expected1);
                std::printf("low mixer rt0=%zu/%u rt1=%zu/%u\n",
                            difference0.different,
                            unsigned(difference0.maximum),
                            difference1.different,
                            unsigned(difference1.maximum));
                Require(difference0.different == 0 && difference0.maximum == 0 &&
                            difference1.different == 0 && difference1.maximum == 0,
                        "mixer mismatch");
            }
            for (const auto& surface : item.surfaces)
            {
                const auto expected = ReadBytes(surface.expected, std::filesystem::file_size(surface.expected));
                const auto geometry = Geometry(surface.surface);
                auto surfaceOutput = CreateTexture(
                    d3d->device, geometry.width, geometry.height, geometry.format, nullptr);
                ComPtr<ID3D11RenderTargetView> surfaceOutputView;
                Require(SUCCEEDED(d3d->device->CreateRenderTargetView(surfaceOutput.Get(), nullptr, &surfaceOutputView)),
                        "surface output RTV");
                Require(ngu.CopyNativeLumaQuality720Surface(
                            item.quality, surface.surface, surface.index, surfaceOutputView.Get()),
                        ngu.LastError());
                pl_gpu_finish(d3d->gpu);
                const auto actual = Download(d3d->device, context.Get(), surfaceOutput.Get());
                if (const char* dumpRoot = std::getenv("NGU_QUALITY_DUMP_DIR"))
                {
                    const std::filesystem::path dumpDirectory(dumpRoot);
                    std::filesystem::create_directories(dumpDirectory);
                    char name[128] = {};
                    std::snprintf(name,
                                  sizeof(name),
                                  "%s_surface%d_index%d.bin",
                                  item.name,
                                  int(surface.surface),
                                  surface.index);
                    std::ofstream file(dumpDirectory / name, std::ios::binary);
                    Require(bool(file), "surface dump open failed");
                    file.write(reinterpret_cast<const char*>(actual.data()), std::streamsize(actual.size()));
                }
                const auto difference = Measure(actual, expected);
                if (difference.different != 0)
                {
                    size_t first = 0;
                    while (first < actual.size() && actual[first] == expected[first])
                        ++first;
                    std::printf("%s surface=%d index=%d first-byte=%zu actual=%u expected=%u memcmp=%d\n",
                                item.name,
                                int(surface.surface),
                                surface.index,
                                first,
                                first < actual.size() ? unsigned(actual[first]) : 0,
                                first < expected.size() ? unsigned(expected[first]) : 0,
                                std::memcmp(actual.data(), expected.data(), actual.size()));
                }
                std::printf("%s surface=%d index=%d different=%zu maximum=%u\n",
                            item.name,
                            int(surface.surface),
                            surface.index,
                            difference.different,
                            unsigned(difference.maximum));
                passed = passed && difference.different == 0 && difference.maximum == 0;
            }
        }
        Require(passed, "luma quality target mismatch");
        puts("PASS_PRODUCT NGU Sharp luma Low/Medium/High 720p target-tail surfaces");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL_PRODUCT %s\n", error.what());
        return 1;
    }
}
