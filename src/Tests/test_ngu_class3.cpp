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

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path,
                               size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open capture");
    Require(size_t(file.tellg()) == expected, "capture size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),
                           static_cast<std::streamsize>(bytes.size()))),
            "read capture");
    return bytes;
}

std::filesystem::path FindCapture(const std::filesystem::path& root,
                                  const char* shader,
                                  const char* geometry,
                                  size_t expectedSize)
{
    size_t matches = 0;
    std::filesystem::path result;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.find(shader) == std::string::npos ||
            name.find(geometry) == std::string::npos ||
            name.find("_rt0_") == std::string::npos)
            continue;
        if (size_t(entry.file_size()) != expectedSize)
            continue;
        ++matches;
        result = entry.path();
    }
    Require(matches == 1, "unique stage capture");
    return result;
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      UINT width,
                                      UINT height,
                                      UINT bind,
                                      const void* initial,
                                      DXGI_FORMAT format =
                                          DXGI_FORMAT_R16G16B16A16_UNORM)
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
    data.SysMemPitch = width * 8;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, initial ? &data : nullptr, &texture)),
            "create RGBA16 texture");
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
    const size_t rowBytes = size_t(desc.Width) * 8;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

int CompareChannels(const std::vector<uint8_t>& actual,
                    const std::vector<uint8_t>& expected,
                    int& maximum,
                    int& overOne,
                    size_t& maximumOffset)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0;
    maximum = 0;
    overOne = 0;
    maximumOffset = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left =
            uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        const int delta = std::abs(int(left) - int(right));
        different += delta != 0;
        overOne += delta > 1;
        if (delta > maximum)
        {
            maximum = delta;
            maximumOffset = offset;
        }
    }
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || argc == 3,
                "usage: test_ngu_class3 <capture-root> [label]");
        const std::filesystem::path root(argv[1]);
        const char* label = argc == 3 ? argv[2] : "class3";

        NguScalerRequest request;
        request.sourceWidth = 1280;
        request.sourceHeight = 720;
        request.quality = NguQuality::VeryHigh;
        request.stage = NguScalerStage::VeryHighClass3ChromaTail720;
        Require(ProbeNguScaler(request) == NguScalerSupport::Ready,
                "class-3 tail capability");
        request.sourceHeight = 721;
        Require(ProbeNguScaler(request) ==
                    NguScalerSupport::UnsupportedGeometry,
                "class-3 tail geometry gate");

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        constexpr UINT sourceWidth = 1280;
        constexpr UINT sourceHeight = 720;
        constexpr UINT outputWidth = 3840;
        constexpr UINT outputHeight = 2160;
        const auto sourceBytes = ReadBytes(
            FindCapture(root,
                        "PS_4576_b7830945",
                        "1280x720",
                        size_t(sourceWidth) * sourceHeight * 8),
            size_t(sourceWidth) * sourceHeight * 8);
        const auto expected = ReadBytes(
            FindCapture(root,
                        "PS_5868_0cb2f4f9",
                        "3840x2160",
                        size_t(outputWidth) * outputHeight * 8),
            size_t(outputWidth) * outputHeight * 8);
        auto source = CreateTexture(device,
                                    sourceWidth,
                                    sourceHeight,
                                    D3D11_BIND_SHADER_RESOURCE,
                                    sourceBytes.data());
        auto output = CreateTexture(device,
                                    outputWidth,
                                    outputHeight,
                                    D3D11_BIND_RENDER_TARGET |
                                        D3D11_BIND_SHADER_RESOURCE,
                                    nullptr);
        ComPtr<ID3D11ShaderResourceView> sourceView;
        ComPtr<ID3D11RenderTargetView> outputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(
                    source.Get(), nullptr, &sourceView)),
                "source SRV");
        Require(SUCCEEDED(device->CreateRenderTargetView(
                    output.Get(), nullptr, &outputView)),
                "output RTV");

        NguScaler ngu;
        if (!ngu.PrepareNativeClass3ChromaTail720(d3d->gpu))
        {
            const std::string message =
                std::string("prepare class-3 tail: ") + ngu.LastError();
            throw std::runtime_error(message);
        }
        if (!ngu.RunNativeClass3ChromaTail720(sourceView.Get(),
                                             outputView.Get()))
        {
            const std::string message =
                std::string("run class-3 tail: ") + ngu.LastError();
            throw std::runtime_error(message);
        }

        const struct
        {
            const char* shader;
            const char* geometry;
            UINT width;
            UINT height;
            DXGI_FORMAT format;
        } stages[] = {
            {"PS_6392_8ead432d",
             "1280x720",
             1280,
             720,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_5140_05c7e417",
             "2560x1440",
             2560,
             1440,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_4804_6ca2ba60",
             "2560x1440",
             2560,
             1440,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_6392_8ead432d",
             "2560x1440",
             2560,
             1440,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_5140_05c7e417",
             "5120x2880",
             5120,
             2880,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_4804_6ca2ba60",
             "5120x2880",
             5120,
             2880,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_536_057fb11f",
             "5120x2880",
             5120,
             2880,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_1200_ea811ad5",
             "3840x2880",
             3840,
             2880,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_1176_487f5b4d",
             "3840x2160",
             3840,
             2160,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_1208_b99fedc2",
             "3840x2160",
             3840,
             2160,
             DXGI_FORMAT_R16G16B16A16_FLOAT},
            {"PS_1052_f4319416",
             "3840x2160",
             3840,
             2160,
             DXGI_FORMAT_R16G16B16A16_UNORM},
            {"PS_5868_0cb2f4f9",
             "3840x2160",
             3840,
             2160,
             DXGI_FORMAT_R16G16B16A16_UNORM},
        };
        for (int stage = 0; stage < 12; ++stage)
        {
            const auto stageExpected =
                ReadBytes(FindCapture(root,
                                      stages[stage].shader,
                                      stages[stage].geometry,
                                      size_t(stages[stage].width) *
                                          stages[stage].height * 8),
                          size_t(stages[stage].width) *
                              stages[stage].height * 8);
            auto stageOutput = CreateTexture(device,
                                             stages[stage].width,
                                             stages[stage].height,
                                             D3D11_BIND_RENDER_TARGET |
                                                 D3D11_BIND_SHADER_RESOURCE,
                                             nullptr,
                                             stages[stage].format);
            ComPtr<ID3D11RenderTargetView> stageView;
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        stageOutput.Get(), nullptr, &stageView)),
                    "stage RTV");
            if (!ngu.CopyNativeClass3PostStage720(stage, stageView.Get()))
            {
                const std::string message =
                    std::string("copy class-3 stage: ") + ngu.LastError();
                throw std::runtime_error(message);
            }
            const auto stageActual =
                Download(device, context.Get(), stageOutput.Get());
            int stageMaximum = 0;
            int stageOverOne = 0;
            size_t stageMaximumOffset = 0;
            const int stageDifferent = CompareChannels(
                stageActual,
                stageExpected,
                stageMaximum,
                stageOverOne,
                stageMaximumOffset);
            std::printf(
                "class3 stage=%d different=%d maximum=%d "
                "diff_gt_1_channels=%d max_index=%zu "
                "max_xy_channel=%zu,%zu,%zu\n",
                stage,
                stageDifferent,
                stageMaximum,
                stageOverOne,
                stageMaximumOffset / 2,
                (stageMaximumOffset / 8) % stages[stage].width,
                (stageMaximumOffset / 8) / stages[stage].width,
                (stageMaximumOffset / 2) % 4);
            if ((stage == 1 || stage == 6) && stageMaximum > 1)
            {
                std::printf("stage%d actual-first=", stage);
                for (int channel = 0; channel < 8; ++channel)
                {
                    const uint16_t value =
                        uint16_t(stageActual[channel * 2]) |
                        (uint16_t(stageActual[channel * 2 + 1]) << 8);
                    std::printf("%u,", unsigned(value));
                }
                std::printf(" expected-first=");
                for (int channel = 0; channel < 8; ++channel)
                {
                    const uint16_t value =
                        uint16_t(stageExpected[channel * 2]) |
                        (uint16_t(stageExpected[channel * 2 + 1]) << 8);
                    std::printf("%u,", unsigned(value));
                }
                std::printf("\n");
            }
            Require(stageMaximum <= 1 && stageOverOne == 0,
                    "class-3 post stage exceeds one code");
        }

        const auto actual = Download(device, context.Get(), output.Get());
        int maximum = 0;
        int overOne = 0;
        size_t maximumOffset = 0;
        const int different =
            CompareChannels(
                actual, expected, maximum, overOne, maximumOffset);
        std::printf(
            "%s PS_5868 different=%d maximum=%d "
            "diff_gt_1_channels=%d max_xy_channel=%zu,%zu,%zu\n",
            label,
            different,
            maximum,
            overOne,
            (maximumOffset / 8) % outputWidth,
            (maximumOffset / 8) / outputWidth,
            (maximumOffset / 2) % 4);
        Require(maximum <= 1 && overOne == 0,
                "class-3 PS_5868 exceeds one code");
        ngu.Reset();
        pl_d3d11_destroy(&d3d);
        std::printf("PASS_PRODUCT %s 720p PS_5868\n", label);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
