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

std::filesystem::path FindAnyCapture(const std::filesystem::path& root,
                                     const char* shader,
                                     const char* geometry,
                                     size_t expectedSize)
{
    int matches = 0;
    std::filesystem::path result;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.find(shader) == std::string::npos ||
            name.find(geometry) == std::string::npos ||
            name.find("_rt0_") == std::string::npos ||
            size_t(entry.file_size()) != expectedSize)
            continue;
        ++matches;
        result = entry.path();
    }
    Require(matches > 0, "any stage capture");
    return result;
}

std::filesystem::path FindAnyStageInput(const std::filesystem::path& root,
                                        const char* shader,
                                        const char* suffix,
                                        size_t expectedSize)
{
    int matches = 0;
    std::filesystem::path result;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.find(shader) == std::string::npos ||
            name.find(suffix) == std::string::npos ||
            size_t(entry.file_size()) != expectedSize)
            continue;
        ++matches;
        result = entry.path();
    }
    Require(matches > 0, "any stage input");
    return result;
}

std::filesystem::path FindFirstCapture(const std::filesystem::path& root,
                                       const char* shader,
                                       const char* geometry,
                                       size_t expectedSize)
{
    std::vector<std::filesystem::path> matches;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string name = entry.path().filename().string();
        if (name.find(shader) == std::string::npos ||
            name.find(geometry) == std::string::npos ||
            name.find("_rt0_") == std::string::npos ||
            size_t(entry.file_size()) != expectedSize)
            continue;
        matches.push_back(entry.path());
    }
    Require(!matches.empty(), "first stage capture");
    std::sort(matches.begin(), matches.end());
    return matches.front();
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

ComPtr<ID3D11Texture2D> CreateRawTexture(ID3D11Device* device,
                                         UINT width,
                                         UINT height,
                                         DXGI_FORMAT format,
                                         UINT bind,
                                         const void* initial,
                                         UINT rowPitch)
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
    data.SysMemPitch = rowPitch;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, initial ? &data : nullptr, &texture)),
            "create raw texture");
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
    UINT pixelBytes = 8;
    switch (desc.Format)
    {
    case DXGI_FORMAT_R8_UNORM:
        pixelBytes = 1;
        break;
    case DXGI_FORMAT_R16_UNORM:
        pixelBytes = 2;
        break;
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        pixelBytes = 4;
        break;
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        pixelBytes = 8;
        break;
    default:
        Require(false, "unsupported download format");
    }
    const size_t rowBytes = size_t(desc.Width) * pixelBytes;
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

int CompareR16ToExpandedCapture(const std::vector<uint8_t>& actual,
                                const std::vector<uint8_t>& expected,
                                int& maximum,
                                int& overOne,
                                size_t& maximumOffset)
{
    Require(expected.size() == actual.size() * 4,
            "expanded capture size");
    int different = 0;
    maximum = 0;
    overOne = 0;
    maximumOffset = 0;
    for (size_t pixel = 0; pixel < actual.size() / 2; ++pixel)
    {
        const uint16_t left =
            uint16_t(actual[pixel * 2]) |
            (uint16_t(actual[pixel * 2 + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[pixel * 8]) |
            (uint16_t(expected[pixel * 8 + 1]) << 8);
        const int delta = std::abs(int(left) - int(right));
        different += delta != 0;
        overOne += delta > 1;
        if (delta > maximum)
        {
            maximum = delta;
            maximumOffset = pixel;
        }
    }
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc >= 2 && argc <= 5,
                "usage: test_ngu_class3 <capture-root> [label] [p210-raw] "
                "[core-target-root]");
        const std::filesystem::path root(argv[1]);
        const char* label = argc >= 3 ? argv[2] : "class3";
        const std::filesystem::path p210Raw = argc >= 4 ? argv[3] : "";
        const std::filesystem::path coreTargetRoot =
            argc == 5 ? argv[4] : "";

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

        if (!p210Raw.empty())
        {
            constexpr UINT rawWidth = 1280;
            constexpr UINT rawHeight = 720;
            const size_t yBytes = size_t(rawWidth) * rawHeight * 2;
            const auto raw = ReadBytes(
                p210Raw, yBytes + size_t(640) * rawHeight * 4);
            auto coreY = CreateRawTexture(device,
                                          rawWidth,
                                          rawHeight,
                                          DXGI_FORMAT_R16_UNORM,
                                          D3D11_BIND_SHADER_RESOURCE,
                                          raw.data(),
                                          rawWidth * 2);
            auto coreUv = CreateRawTexture(device,
                                           640,
                                           rawHeight,
                                           DXGI_FORMAT_R16G16_UNORM,
                                           D3D11_BIND_SHADER_RESOURCE,
                                           raw.data() + yBytes,
                                           640 * 4);
            auto coreOutput = CreateTexture(device,
                                            1280,
                                            720,
                                            D3D11_BIND_RENDER_TARGET |
                                                D3D11_BIND_SHADER_RESOURCE,
                                            nullptr);
            ComPtr<ID3D11ShaderResourceView> coreYView;
            ComPtr<ID3D11ShaderResourceView> coreUvView;
            ComPtr<ID3D11RenderTargetView> coreOutputView;
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        coreY.Get(), nullptr, &coreYView)),
                    "core Y SRV");
            Require(SUCCEEDED(device->CreateShaderResourceView(
                        coreUv.Get(), nullptr, &coreUvView)),
                    "core UV SRV");
            Require(SUCCEEDED(device->CreateRenderTargetView(
                        coreOutput.Get(), nullptr, &coreOutputView)),
                    "core output RTV");
            NguScaler core;
            if (!core.PrepareNativeChromaCoreClass3_720(d3d->gpu))
            {
                const std::string message =
                    std::string("prepare class-3 core: ") +
                    core.LastError();
                throw std::runtime_error(message);
            }
            if (!core.RunNativeChromaCoreClass3_720(
                    coreUvView.Get(),
                    coreYView.Get(),
                    coreOutputView.Get()))
            {
                const std::string message =
                    std::string("run class-3 core: ") + core.LastError();
                throw std::runtime_error(message);
            }
            const struct
            {
                int stage;
                const char* shader;
                UINT width;
                UINT height;
            } coreStages[] = {
                {6, "PS_1516_11f763dd", 1280, 1440},
                {7, "PS_396_e3199f71", 1280, 720},
                {11, "PS_4576_b7830945", 1280, 720},
            };
            std::vector<uint8_t> coreRgbActual;
            for (const auto& coreStage : coreStages)
            {
                const std::string geometry =
                    std::to_string(coreStage.width) + "x" +
                    std::to_string(coreStage.height);
                const auto stageExpected = ReadBytes(
                    FindCapture(root,
                                coreStage.shader,
                                geometry.c_str(),
                                size_t(coreStage.width) *
                                    coreStage.height * 8),
                    size_t(coreStage.width) * coreStage.height * 8);
                auto stageOutput = CreateTexture(
                    device,
                    coreStage.width,
                    coreStage.height,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11RenderTargetView> stageView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            stageOutput.Get(), nullptr, &stageView)),
                        "core stage RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        coreStage.stage, stageView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 core stage: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto stageActual =
                    Download(device, context.Get(), stageOutput.Get());
                if (coreStage.stage == 7)
                    coreRgbActual = stageActual;
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
                    "class3-core stage=%d different=%d maximum=%d "
                    "diff_gt_1_channels=%d\n",
                    coreStage.stage,
                    stageDifferent,
                    stageMaximum,
                    stageOverOne);
                if (coreStage.stage == 7)
                {
                    int yMaximum = 0;
                    for (size_t pixel = 0; pixel < size_t(1280) * 720;
                         ++pixel)
                    {
                        const uint16_t actualY =
                            uint16_t(stageActual[pixel * 8]) |
                            (uint16_t(stageActual[pixel * 8 + 1]) << 8);
                        const uint16_t expectedY =
                            uint16_t(stageExpected[pixel * 8]) |
                            (uint16_t(stageExpected[pixel * 8 + 1]) << 8);
                        yMaximum = std::max(
                            yMaximum,
                            std::abs(int(actualY) - int(expectedY)));
                    }
                    std::printf("class3-core PS_396 Y maximum=%d\n",
                                yMaximum);
                }
            }
            {
                const auto combine2Expected = ReadBytes(
                    FindCapture(root,
                                "PS_644_512f7798",
                                "1280x1440",
                                size_t(1280) * 1440 * 4),
                    size_t(1280) * 1440 * 4);
                auto combine2Output = CreateTexture(
                    device,
                    1280,
                    1440,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr,
                    DXGI_FORMAT_R16G16_UNORM);
                ComPtr<ID3D11RenderTargetView> combine2View;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            combine2Output.Get(), nullptr, &combine2View)),
                        "combine2 RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        5, combine2View.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 combine2: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto combine2Actual =
                    Download(device, context.Get(), combine2Output.Get());
                int combine2Maximum = 0;
                int combine2OverOne = 0;
                size_t combine2MaximumOffset = 0;
                const int combine2Different = CompareChannels(
                    combine2Actual,
                    combine2Expected,
                    combine2Maximum,
                    combine2OverOne,
                    combine2MaximumOffset);
                std::printf(
                    "class3-core PS_644 different=%d maximum=%d "
                    "diff_gt_1_channels=%d\n",
                    combine2Different,
                    combine2Maximum,
                    combine2OverOne);

                const auto realignInputBytes = ReadBytes(
                    FindCapture(root,
                                "PS_644_512f7798",
                                "1280x1440",
                                size_t(1280) * 1440 * 4),
                    size_t(1280) * 1440 * 4);
                auto realignInput = CreateRawTexture(
                    device,
                    1280,
                    1440,
                    DXGI_FORMAT_R16G16_UNORM,
                    D3D11_BIND_SHADER_RESOURCE,
                    realignInputBytes.data(),
                    1280 * 4);
                auto realignOutput = CreateTexture(
                    device,
                    1280,
                    1440,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11ShaderResourceView> realignInputView;
                ComPtr<ID3D11RenderTargetView> realignOutputView;
                Require(SUCCEEDED(device->CreateShaderResourceView(
                            realignInput.Get(),
                            nullptr,
                            &realignInputView)) &&
                            SUCCEEDED(device->CreateRenderTargetView(
                                realignOutput.Get(),
                                nullptr,
                                &realignOutputView)),
                        "realign captured views");
                if (!core.RunNativeChromaRealign(
                        realignInputView.Get(), realignOutputView.Get()))
                {
                    const std::string message =
                        std::string("run captured realign: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto realignExpected = ReadBytes(
                    FindCapture(root,
                                "PS_1516_11f763dd",
                                "1280x1440",
                                size_t(1280) * 1440 * 8),
                    size_t(1280) * 1440 * 8);
                const auto realignActual =
                    Download(device, context.Get(), realignOutput.Get());
                int realignMaximum = 0;
                int realignOverOne = 0;
                size_t realignMaximumOffset = 0;
                const int realignDifferent = CompareChannels(
                    realignActual,
                    realignExpected,
                    realignMaximum,
                    realignOverOne,
                    realignMaximumOffset);
                std::printf(
                    "captured-input PS_1516 different=%d maximum=%d "
                    "diff_gt_1_channels=%d\n",
                    realignDifferent,
                    realignMaximum,
                    realignOverOne);
            }
            const auto coreExpected = ReadBytes(
                FindCapture(root,
                            "PS_4576_b7830945",
                            "1280x720",
                            size_t(1280) * 720 * 8),
                size_t(1280) * 720 * 8);
            if (!coreTargetRoot.empty())
            {
                const auto yExpandedExpected = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_4412_c39b49cf",
                                      "_s0.bin",
                                      size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                auto yExpandedOutput = CreateTexture(
                    device,
                    1280,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11RenderTargetView> yExpandedView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            yExpandedOutput.Get(), nullptr, &yExpandedView)),
                        "core yExpanded RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        13, yExpandedView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 yExpanded: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto yExpandedActual =
                    Download(device, context.Get(), yExpandedOutput.Get());
                int yExpandedMaximum = 0;
                int yExpandedOverOne = 0;
                size_t yExpandedMaximumOffset = 0;
                const int yExpandedDifferent = CompareChannels(
                    yExpandedActual,
                    yExpandedExpected,
                    yExpandedMaximum,
                    yExpandedOverOne,
                    yExpandedMaximumOffset);
                std::printf(
                    "class3-core yExpanded different=%d maximum=%d "
                    "diff_gt_1_channels=%d max_xy_channel=%zu,%zu,%zu\n",
                    yExpandedDifferent,
                    yExpandedMaximum,
                    yExpandedOverOne,
                    (yExpandedMaximumOffset / 8) % 1280,
                    (yExpandedMaximumOffset / 8) / 1280,
                    (yExpandedMaximumOffset / 2) % 4);
                if (yExpandedMaximum > 1)
                {
                    std::printf("yExpanded actual-first=");
                    for (int channel = 0; channel < 8; ++channel)
                    {
                        const uint16_t value =
                            uint16_t(yExpandedActual[channel * 2]) |
                            (uint16_t(yExpandedActual[channel * 2 + 1])
                             << 8);
                        std::printf("%u,", unsigned(value));
                    }
                    std::printf(" expected-first=");
                    for (int channel = 0; channel < 8; ++channel)
                    {
                        const uint16_t value =
                            uint16_t(yExpandedExpected[channel * 2]) |
                            (uint16_t(yExpandedExpected[channel * 2 + 1])
                             << 8);
                        std::printf("%u,", unsigned(value));
                    }
                    std::printf("\n");
                }
                Require(yExpandedMaximum <= 1 && yExpandedOverOne == 0,
                        "class-3 yExpanded source exceeds one code");

                const auto cbScaleXExpected = ReadBytes(
                    FindFirstCapture(coreTargetRoot,
                                     "PS_748_721dd04e",
                                     "1280x720",
                                     size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                auto cbScaleXOutput = CreateTexture(
                    device,
                    1280,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr,
                    DXGI_FORMAT_R16_UNORM);
                ComPtr<ID3D11RenderTargetView> cbScaleXView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            cbScaleXOutput.Get(), nullptr, &cbScaleXView)),
                        "core cbScaleX RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        12, cbScaleXView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 cbScaleX: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto cbScaleXActual =
                    Download(device, context.Get(), cbScaleXOutput.Get());
                int cbScaleXMaximum = 0;
                int cbScaleXOverOne = 0;
                size_t cbScaleXMaximumOffset = 0;
                const int cbScaleXDifferent =
                    CompareR16ToExpandedCapture(
                        cbScaleXActual,
                        cbScaleXExpected,
                        cbScaleXMaximum,
                        cbScaleXOverOne,
                        cbScaleXMaximumOffset);
                std::printf(
                    "class3-core PS_748 X cbScaleX "
                    "different=%d maximum=%d diff_gt_1_channels=%d "
                    "max_xy=%zu,%zu\n",
                    cbScaleXDifferent,
                    cbScaleXMaximum,
                    cbScaleXOverOne,
                    cbScaleXMaximumOffset % 1280,
                    cbScaleXMaximumOffset / 1280);
                Require(cbScaleXMaximum <= 1 && cbScaleXOverOne == 0,
                        "class-3 cbScaleX source exceeds one code");

                const auto cbScaleYExpected = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_564_500335c8",
                                      "_s0.bin",
                                      size_t(1280) * 1440 * 8),
                    size_t(1280) * 1440 * 8);
                auto cbScaleYOutput = CreateTexture(
                    device,
                    1280,
                    1440,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr,
                    DXGI_FORMAT_R16_UNORM);
                ComPtr<ID3D11RenderTargetView> cbScaleYView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            cbScaleYOutput.Get(), nullptr, &cbScaleYView)),
                        "core cbScaleY RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        2, cbScaleYView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 cbScaleY: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto cbScaleYActual =
                    Download(device, context.Get(), cbScaleYOutput.Get());
                int cbScaleYMaximum = 0;
                int cbScaleYOverOne = 0;
                size_t cbScaleYMaximumOffset = 0;
                const int cbScaleYDifferent =
                    CompareR16ToExpandedCapture(
                        cbScaleYActual,
                        cbScaleYExpected,
                        cbScaleYMaximum,
                        cbScaleYOverOne,
                        cbScaleYMaximumOffset);
                std::printf(
                    "class3-core PS_564 input s0 cbScaleY "
                    "different=%d maximum=%d diff_gt_1_channels=%d "
                    "max_xy=%zu,%zu\n",
                    cbScaleYDifferent,
                    cbScaleYMaximum,
                    cbScaleYOverOne,
                    cbScaleYMaximumOffset % 1280,
                    cbScaleYMaximumOffset / 1280);
                if (cbScaleYMaximum != 0)
                {
                    std::printf(
                        "PS_748 Y mismatch actual=%u expected=%u\n",
                        unsigned(uint16_t(
                                     cbScaleYActual[
                                         cbScaleYMaximumOffset * 2]) |
                                 (uint16_t(
                                      cbScaleYActual[
                                          cbScaleYMaximumOffset * 2 + 1])
                                  << 8)),
                        unsigned(uint16_t(
                                     cbScaleYExpected[
                                         cbScaleYMaximumOffset * 8]) |
                                 (uint16_t(
                                      cbScaleYExpected[
                                          cbScaleYMaximumOffset * 8 + 1])
                                  << 8)));
                }
                Require(cbScaleYMaximum <= 1 && cbScaleYOverOne == 0,
                        "class-3 cbScaleY source exceeds one code");

                const auto denseCbExpected = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_564_500335c8",
                                      "_s2.bin",
                                      size_t(642) * 722 * 8),
                    size_t(642) * 722 * 8);
                auto denseCbOutput = CreateTexture(
                    device,
                    642,
                    722,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr,
                    DXGI_FORMAT_R16G16B16A16_FLOAT);
                ComPtr<ID3D11RenderTargetView> denseCbView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            denseCbOutput.Get(), nullptr, &denseCbView)),
                        "core denseCb RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        1, denseCbView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 denseCb: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto denseCbActual =
                    Download(device, context.Get(), denseCbOutput.Get());
                int denseCbMaximum = 0;
                int denseCbOverOne = 0;
                size_t denseCbMaximumOffset = 0;
                const int denseCbDifferent = CompareChannels(
                    denseCbActual,
                    denseCbExpected,
                    denseCbMaximum,
                    denseCbOverOne,
                    denseCbMaximumOffset);
                std::printf(
                    "class3-core PS_564 input s2 denseCb "
                    "different=%d maximum=%d diff_gt_1_channels=%d "
                    "max_xy=%zu,%zu\n",
                    denseCbDifferent,
                    denseCbMaximum,
                    denseCbOverOne,
                    (denseCbMaximumOffset / 8) % 642,
                    (denseCbMaximumOffset / 8) / 642);
                Require(denseCbMaximum <= 1 && denseCbOverOne == 0,
                        "class-3 denseCb source exceeds one code");

                const auto combine1Expected = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_644_512f7798",
                                      "_s2.bin",
                                      size_t(1280) * 1440 * 8),
                    size_t(1280) * 1440 * 8);
                auto combine1Output = CreateTexture(
                    device,
                    1280,
                    1440,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr,
                    DXGI_FORMAT_R16_UNORM);
                ComPtr<ID3D11RenderTargetView> combine1View;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            combine1Output.Get(), nullptr, &combine1View)),
                        "core combine1 RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        3, combine1View.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 combine1: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto combine1Actual =
                    Download(device, context.Get(), combine1Output.Get());
                int combine1Maximum = 0;
                int combine1OverOne = 0;
                size_t combine1MaximumOffset = 0;
                const int combine1Different =
                    CompareR16ToExpandedCapture(
                        combine1Actual,
                        combine1Expected,
                        combine1Maximum,
                        combine1OverOne,
                        combine1MaximumOffset);
                std::printf(
                    "class3-core PS_644 input s2 combine1 "
                    "different=%d maximum=%d diff_gt_1_channels=%d "
                    "max_xy=%zu,%zu\n",
                    combine1Different,
                    combine1Maximum,
                    combine1OverOne,
                    combine1MaximumOffset % 1280,
                    combine1MaximumOffset / 1280);
                Require(combine1Maximum <= 1 && combine1OverOne == 0,
                        "class-3 combine1 source exceeds one code");

                const auto denseCrExpected = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_644_512f7798",
                                      "_s3.bin",
                                      size_t(642) * 722 * 8),
                    size_t(642) * 722 * 8);
                auto denseCrOutput = CreateTexture(
                    device,
                    642,
                    722,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr,
                    DXGI_FORMAT_R16G16B16A16_FLOAT);
                ComPtr<ID3D11RenderTargetView> denseCrView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            denseCrOutput.Get(), nullptr, &denseCrView)),
                        "core denseCr RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        4, denseCrView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 denseCr: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto denseCrActual =
                    Download(device, context.Get(), denseCrOutput.Get());
                int denseCrMaximum = 0;
                int denseCrOverOne = 0;
                size_t denseCrMaximumOffset = 0;
                const int denseCrDifferent = CompareChannels(
                    denseCrActual,
                    denseCrExpected,
                    denseCrMaximum,
                    denseCrOverOne,
                    denseCrMaximumOffset);
                std::printf(
                    "class3-core PS_644 input s3 denseCr "
                    "different=%d maximum=%d diff_gt_1_channels=%d "
                    "max_xy=%zu,%zu\n",
                    denseCrDifferent,
                    denseCrMaximum,
                    denseCrOverOne,
                    (denseCrMaximumOffset / 8) % 642,
                    (denseCrMaximumOffset / 8) / 642);
                Require(denseCrMaximum <= 1 && denseCrOverOne == 0,
                        "class-3 denseCr source exceeds one code");

                const auto downExpected = ReadBytes(
                    FindFirstCapture(coreTargetRoot,
                                     "PS_520_4a9505a4",
                                     "640x720",
                                     size_t(640) * 720 * 8),
                    size_t(640) * 720 * 8);
                auto downOutput = CreateTexture(
                    device,
                    640,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11RenderTargetView> downView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            downOutput.Get(), nullptr, &downView)),
                        "core downX RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        8, downView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 downX: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto downActual =
                    Download(device, context.Get(), downOutput.Get());
                int downMaximum = 0;
                int downOverOne = 0;
                size_t downMaximumOffset = 0;
                const int downDifferent = CompareChannels(
                    downActual,
                    downExpected,
                    downMaximum,
                    downOverOne,
                    downMaximumOffset);
                std::printf(
                    "class3-core PS_520 different=%d maximum=%d "
                    "diff_gt_1_channels=%d max_xy_channel=%zu,%zu,%zu\n",
                    downDifferent,
                    downMaximum,
                    downOverOne,
                    (downMaximumOffset / 8) % 640,
                    (downMaximumOffset / 8) / 640,
                    (downMaximumOffset / 2) % 4);
                if (downMaximum > 1)
                {
                    std::printf("PS_520 actual-first=");
                    for (int channel = 0; channel < 8; ++channel)
                    {
                        const uint16_t value =
                            uint16_t(downActual[channel * 2]) |
                            (uint16_t(downActual[channel * 2 + 1]) << 8);
                        std::printf("%u,", unsigned(value));
                    }
                    std::printf(" expected-first=");
                    for (int channel = 0; channel < 8; ++channel)
                    {
                        const uint16_t value =
                            uint16_t(downExpected[channel * 2]) |
                            (uint16_t(downExpected[channel * 2 + 1]) << 8);
                        std::printf("%u,", unsigned(value));
                    }
                    std::printf("\n");
                }

                const auto diffExpected = ReadBytes(
                    FindFirstCapture(coreTargetRoot,
                                     "PS_284_e8e2a8eb",
                                     "640x720",
                                     size_t(640) * 720 * 4),
                    size_t(640) * 720 * 4);
                auto diffOutput = CreateTexture(
                    device,
                    640,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr,
                    DXGI_FORMAT_R16G16_FLOAT);
                ComPtr<ID3D11RenderTargetView> diffView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            diffOutput.Get(), nullptr, &diffView)),
                        "core diff RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        9, diffView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 diff: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto diffActual =
                    Download(device, context.Get(), diffOutput.Get());
                int diffMaximum = 0;
                int diffOverOne = 0;
                size_t diffMaximumOffset = 0;
                const int diffDifferent = CompareChannels(
                    diffActual,
                    diffExpected,
                    diffMaximum,
                    diffOverOne,
                    diffMaximumOffset);
                std::printf(
                    "class3-core PS_4412 difference different=%d maximum=%d "
                    "diff_gt_1_channels=%d max_xy_channel=%zu,%zu,%zu\n",
                    diffDifferent,
                    diffMaximum,
                    diffOverOne,
                    (diffMaximumOffset / 4) % 640,
                    (diffMaximumOffset / 4) / 640,
                    (diffMaximumOffset / 2) % 2);

                const auto stageExpected = ReadBytes(
                    FindFirstCapture(coreTargetRoot,
                                     "PS_4412_c39b49cf",
                                     "1280x720",
                                     size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                auto stageOutput = CreateTexture(
                    device,
                    1280,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11RenderTargetView> stageView;
                Require(SUCCEEDED(device->CreateRenderTargetView(
                            stageOutput.Get(), nullptr, &stageView)),
                        "core PS4412 RTV");
                if (!core.CopyNativeChromaCoreClass3Stage720(
                        10, stageView.Get()))
                {
                    const std::string message =
                        std::string("copy class-3 PS4412: ") +
                        core.LastError();
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
                    "class3-core stage=10 different=%d maximum=%d "
                    "diff_gt_1_channels=%d\n",
                    stageDifferent,
                    stageMaximum,
                    stageOverOne);
                if (stageMaximum > 1)
                {
                    for (size_t offset = 0;
                         offset < stageActual.size();
                         offset += 2)
                    {
                        const uint16_t actualValue =
                            uint16_t(stageActual[offset]) |
                            (uint16_t(stageActual[offset + 1]) << 8);
                        const uint16_t expectedValue =
                            uint16_t(stageExpected[offset]) |
                            (uint16_t(stageExpected[offset + 1]) << 8);
                        if (actualValue == expectedValue)
                            continue;
                        const size_t pixel = offset / 8;
                        std::printf(
                            "PS_4412 first-mismatch xy_channel=%zu,%zu,%zu "
                            "actual=%u expected=%u\n",
                            pixel % 1280,
                            pixel / 1280,
                            (offset / 2) % 4,
                            unsigned(actualValue),
                            unsigned(expectedValue));
                        break;
                    }
                }
                if (stageMaximum > 1)
                {
                    std::printf("PS_4412 actual-first=");
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

                const auto targetMainS0 = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_4412_c39b49cf",
                                      "_s0.bin",
                                      size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                const auto targetMainS2 = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_4412_c39b49cf",
                                      "_s2.bin",
                                      size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                const auto targetMainS3 = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_4412_c39b49cf",
                                      "_s3.bin",
                                      size_t(640) * 720 * 4),
                    size_t(640) * 720 * 4);
                auto targetMainS0Texture = CreateRawTexture(
                    device,
                    1280,
                    720,
                    DXGI_FORMAT_R16G16B16A16_UNORM,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    targetMainS0.data(),
                    1280 * 8);
                auto targetMainS2Texture = CreateRawTexture(
                    device,
                    1280,
                    720,
                    DXGI_FORMAT_R16G16B16A16_UNORM,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    targetMainS2.data(),
                    1280 * 8);
                auto targetMainS3Texture = CreateRawTexture(
                    device,
                    640,
                    720,
                    DXGI_FORMAT_R16G16_FLOAT,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    targetMainS3.data(),
                    640 * 4);
                auto targetMainOutput = CreateTexture(
                    device,
                    1280,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11ShaderResourceView> targetMainS0View;
                ComPtr<ID3D11ShaderResourceView> targetMainS2View;
                ComPtr<ID3D11ShaderResourceView> targetMainS3View;
                ComPtr<ID3D11RenderTargetView> targetMainOutputView;
                Require(SUCCEEDED(device->CreateShaderResourceView(
                            targetMainS0Texture.Get(),
                            nullptr,
                            &targetMainS0View)) &&
                            SUCCEEDED(device->CreateShaderResourceView(
                                targetMainS2Texture.Get(),
                                nullptr,
                                &targetMainS2View)) &&
                            SUCCEEDED(device->CreateShaderResourceView(
                                targetMainS3Texture.Get(),
                                nullptr,
                                &targetMainS3View)) &&
                            SUCCEEDED(device->CreateRenderTargetView(
                                targetMainOutput.Get(),
                                nullptr,
                                &targetMainOutputView)),
                        "target PS4412 views");
                if (!core.RunNativeSuperResToRgbMainClass3(
                        targetMainS0View.Get(),
                        targetMainS2View.Get(),
                        targetMainS3View.Get(),
                        targetMainOutputView.Get()))
                {
                    const std::string message =
                        std::string("run captured PS4412: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto targetMainExpected = ReadBytes(
                    FindFirstCapture(coreTargetRoot,
                                     "PS_4412_c39b49cf",
                                     "1280x720",
                                     size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                const auto targetMainActual =
                    Download(device, context.Get(), targetMainOutput.Get());
                int targetMainMaximum = 0;
                int targetMainOverOne = 0;
                size_t targetMainMaximumOffset = 0;
                const int targetMainDifferent = CompareChannels(
                    targetMainActual,
                    targetMainExpected,
                    targetMainMaximum,
                    targetMainOverOne,
                    targetMainMaximumOffset);
                std::printf(
                    "captured-input PS_4412 different=%d maximum=%d "
                    "diff_gt_1_channels=%d\n",
                    targetMainDifferent,
                    targetMainMaximum,
                    targetMainOverOne);
                Require(coreRgbActual.size() ==
                            size_t(1280) * 720 * 8,
                        "local RGB replay bytes");
                auto localByteS0Texture = CreateRawTexture(
                    device,
                    1280,
                    720,
                    DXGI_FORMAT_R16G16B16A16_UNORM,
                    D3D11_BIND_SHADER_RESOURCE,
                    yExpandedActual.data(),
                    1280 * 8);
                auto localByteS2Texture = CreateRawTexture(
                    device,
                    1280,
                    720,
                    DXGI_FORMAT_R16G16B16A16_UNORM,
                    D3D11_BIND_SHADER_RESOURCE,
                    coreRgbActual.data(),
                    1280 * 8);
                auto localByteS3Texture = CreateRawTexture(
                    device,
                    640,
                    720,
                    DXGI_FORMAT_R16G16_FLOAT,
                    D3D11_BIND_SHADER_RESOURCE,
                    diffActual.data(),
                    640 * 4);
                auto localByteOutput = CreateTexture(
                    device,
                    1280,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11ShaderResourceView> localByteS0View;
                ComPtr<ID3D11ShaderResourceView> localByteS2View;
                ComPtr<ID3D11ShaderResourceView> localByteS3View;
                ComPtr<ID3D11RenderTargetView> localByteOutputView;
                Require(SUCCEEDED(device->CreateShaderResourceView(
                            localByteS0Texture.Get(),
                            nullptr,
                            &localByteS0View)) &&
                            SUCCEEDED(device->CreateShaderResourceView(
                                localByteS2Texture.Get(),
                                nullptr,
                                &localByteS2View)) &&
                            SUCCEEDED(device->CreateShaderResourceView(
                                localByteS3Texture.Get(),
                                nullptr,
                                &localByteS3View)) &&
                            SUCCEEDED(device->CreateRenderTargetView(
                                localByteOutput.Get(),
                                nullptr,
                                &localByteOutputView)),
                        "local-byte PS4412 views");
                if (!core.RunNativeSuperResToRgbMainClass3(
                        localByteS0View.Get(),
                        localByteS2View.Get(),
                        localByteS3View.Get(),
                        localByteOutputView.Get()))
                {
                    const std::string message =
                        std::string("run local-byte PS4412: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto localByteActual =
                    Download(device, context.Get(), localByteOutput.Get());
                int localByteMaximum = 0;
                int localByteOverOne = 0;
                size_t localByteMaximumOffset = 0;
                const int localByteDifferent = CompareChannels(
                    localByteActual,
                    targetMainExpected,
                    localByteMaximum,
                    localByteOverOne,
                    localByteMaximumOffset);
                std::printf(
                    "local-byte PS_4412 different=%d maximum=%d "
                    "diff_gt_1_channels=%d\n",
                    localByteDifferent,
                    localByteMaximum,
                    localByteOverOne);
                Require(localByteMaximum <= 1 &&
                            localByteOverOne == 0,
                        "local-byte PS4412 exceeds one code");

                const auto targetBaseS0 = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_4576_b7830945",
                                      "_s0.bin",
                                      size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                const auto targetBaseS2 = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_4576_b7830945",
                                      "_s2.bin",
                                      size_t(1280) * 720 * 8),
                    size_t(1280) * 720 * 8);
                const auto targetBaseS3 = ReadBytes(
                    FindAnyStageInput(coreTargetRoot,
                                      "PS_4576_b7830945",
                                      "_s3.bin",
                                      size_t(640) * 720 * 4),
                    size_t(640) * 720 * 4);
                auto targetBaseS0Texture = CreateRawTexture(
                    device,
                    1280,
                    720,
                    DXGI_FORMAT_R16G16B16A16_UNORM,
                    D3D11_BIND_SHADER_RESOURCE,
                    targetBaseS0.data(),
                    1280 * 8);
                auto targetBaseS2Texture = CreateRawTexture(
                    device,
                    1280,
                    720,
                    DXGI_FORMAT_R16G16B16A16_UNORM,
                    D3D11_BIND_SHADER_RESOURCE,
                    targetBaseS2.data(),
                    1280 * 8);
                auto targetBaseS3Texture = CreateRawTexture(
                    device,
                    640,
                    720,
                    DXGI_FORMAT_R16G16_FLOAT,
                    D3D11_BIND_SHADER_RESOURCE,
                    targetBaseS3.data(),
                    640 * 4);
                auto targetBaseOutput = CreateTexture(
                    device,
                    1280,
                    720,
                    D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_SHADER_RESOURCE,
                    nullptr);
                ComPtr<ID3D11ShaderResourceView> targetBaseS0View;
                ComPtr<ID3D11ShaderResourceView> targetBaseS2View;
                ComPtr<ID3D11ShaderResourceView> targetBaseS3View;
                ComPtr<ID3D11RenderTargetView> targetBaseOutputView;
                Require(SUCCEEDED(device->CreateShaderResourceView(
                            targetBaseS0Texture.Get(),
                            nullptr,
                            &targetBaseS0View)) &&
                            SUCCEEDED(device->CreateShaderResourceView(
                                targetBaseS2Texture.Get(),
                                nullptr,
                                &targetBaseS2View)) &&
                            SUCCEEDED(device->CreateShaderResourceView(
                                targetBaseS3Texture.Get(),
                                nullptr,
                                &targetBaseS3View)) &&
                            SUCCEEDED(device->CreateRenderTargetView(
                                targetBaseOutput.Get(),
                                nullptr,
                                &targetBaseOutputView)),
                        "target PS4576 views");
                if (!core.RunNativeSuperResToRgbBaseClass3(
                        targetBaseS0View.Get(),
                        targetBaseS2View.Get(),
                        targetBaseS3View.Get(),
                        targetBaseOutputView.Get()))
                {
                    const std::string message =
                        std::string("run captured PS4576: ") +
                        core.LastError();
                    throw std::runtime_error(message);
                }
                const auto targetBaseActual =
                    Download(device, context.Get(), targetBaseOutput.Get());
                int targetBaseMaximum = 0;
                int targetBaseOverOne = 0;
                size_t targetBaseMaximumOffset = 0;
                const int targetBaseDifferent = CompareChannels(
                    targetBaseActual,
                    coreExpected,
                    targetBaseMaximum,
                    targetBaseOverOne,
                    targetBaseMaximumOffset);
                std::printf(
                    "captured-input PS_4576 different=%d maximum=%d "
                    "diff_gt_1_channels=%d\n",
                    targetBaseDifferent,
                    targetBaseMaximum,
                    targetBaseOverOne);
            }
            const auto coreActual =
                Download(device, context.Get(), coreOutput.Get());
            int coreMaximum = 0;
            int coreOverOne = 0;
            size_t coreMaximumOffset = 0;
            const int coreDifferent = CompareChannels(
                coreActual,
                coreExpected,
                coreMaximum,
                coreOverOne,
                coreMaximumOffset);
            std::printf(
                "class3-core PS_4576 different=%d maximum=%d "
                "diff_gt_1_channels=%d max_xy_channel=%zu,%zu,%zu\n",
                coreDifferent,
                coreMaximum,
                coreOverOne,
                (coreMaximumOffset / 8) % 1280,
                (coreMaximumOffset / 8) / 1280,
                (coreMaximumOffset / 2) % 4);
            Require(coreMaximum <= 1 && coreOverOne == 0,
                    "class-3 PS_4576 exceeds one code");
            core.Reset();
        }

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
