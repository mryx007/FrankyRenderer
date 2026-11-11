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
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr int Width = 1920, Height = 1080, Components = 4;
constexpr size_t PixelCount = size_t(Width) * Height;
constexpr size_t Half4Bytes = PixelCount * Components * sizeof(uint16_t);
constexpr size_t L16CaptureBytes = PixelCount * 8;

void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<uint8_t> ReadBytes(const std::wstring& path, size_t expected)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open binary");
    Require(size_t(file.tellg()) == expected, "unexpected binary size");
    file.seekg(0);
    std::vector<uint8_t> bytes(expected);
    Require(bool(file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()))), "read binary");
    return bytes;
}

void WriteBytes(const std::wstring& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary);
    Require(bool(file), "write binary");
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    Require(bool(file), "finish binary");
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device, DXGI_FORMAT format, UINT bind, const void* initial = nullptr)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = Width;
    desc.Height = Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = initial;
    data.SysMemPitch = Width * (format == DXGI_FORMAT_R16_UNORM ? 2 : 8);
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, initial ? &data : nullptr, &texture)), "create texture");
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
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "create staging texture");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map staging texture");
    std::vector<uint8_t> bytes(Half4Bytes);
    const size_t rowBytes = size_t(Width) * Components * sizeof(uint16_t);
    for (int y = 0; y < Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

int CountDifferences(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected, int& maximum)
{
    Require(actual.size() == expected.size(), "comparison size");
    int different = 0, largest = 0;
    for (size_t offset = 0; offset < actual.size(); offset += 2)
    {
        const uint16_t left =
            uint16_t(actual[offset]) | (uint16_t(actual[offset + 1]) << 8);
        const uint16_t right =
            uint16_t(expected[offset]) | (uint16_t(expected[offset + 1]) << 8);
        if (left != right)
        {
            ++different;
            const int delta = std::abs(int(left) - int(right));
            largest = std::max(largest, delta);
        }
    }
    maximum = largest;
    return different;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "usage: test_ngu_native <capture-root> <actual-prefix> <summary>");
        const std::filesystem::path root = std::filesystem::path(argv[1]);
        const std::wstring actualPrefix = std::filesystem::path(argv[2]).wstring();
        const std::filesystem::path summaryPath = std::filesystem::path(argv[3]);

        const auto sourceCapture = ReadBytes(
            (root / "input_31706_PS_1412_aad01c2f_s0.bin").wstring(),
            L16CaptureBytes);
        std::vector<uint16_t> source(PixelCount);
        for (size_t i = 0; i < PixelCount; ++i)
            std::memcpy(&source[i], sourceCapture.data() + i * 8, sizeof(uint16_t));

        const std::array<std::filesystem::path, 3> expectedPaths = {
            root / "draw_31706_PS_1412_aad01c2f_rt0_1920x1080_f113.bin",
            root / "input_31707_PS_3588_80fb311e_s2.bin",
            root / "input_31707_PS_3588_80fb311e_s3.bin",
        };
        std::array<std::vector<uint8_t>, 3> expected;
        for (size_t i = 0; i < expected.size(); ++i)
            expected[i] = ReadBytes(expectedPaths[i].wstring(), Half4Bytes);

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ID3D11Device* device = d3d->device;

        auto input = CreateTexture(
            device,
            DXGI_FORMAT_R16_UNORM,
            D3D11_BIND_SHADER_RESOURCE,
            source.data());
        ComPtr<ID3D11ShaderResourceView> inputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(input.Get(), nullptr, &inputView)),
                "create input SRV");

        std::array<ComPtr<ID3D11Texture2D>, 3> outputs;
        std::array<ComPtr<ID3D11RenderTargetView>, 3> outputViews;
        std::array<ID3D11RenderTargetView*, 3> rawViews = {};
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            outputs[i] = CreateTexture(
                device,
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            Require(SUCCEEDED(device->CreateRenderTargetView(outputs[i].Get(), nullptr, &outputViews[i])),
                    "create output RTV");
            rawViews[i] = outputViews[i].Get();
        }

        NguScalerRequest request;
        request.sourceWidth = Width;
        request.sourceHeight = Height;
        request.stage = NguScalerStage::VeryHighLumaInitial1080;
        Require(ProbeNguScaler(request) == NguScalerSupport::Ready, "native Cubici1 capability");

        NguScaler ngu;
        Require(ngu.PrepareNativeLumaInitial(d3d->gpu), ngu.LastError());
        Require(ngu.RunNativeLumaInitial(inputView.Get(), Width, Height, rawViews.data()),
                ngu.LastError());
        pl_gpu_finish(d3d->gpu);

        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        std::array<int, 3> differences = {};
        std::array<int, 3> maximum = {};
        std::array<std::vector<uint8_t>, 3> actual;
        for (size_t i = 0; i < actual.size(); ++i)
        {
            actual[i] = Download(device, context.Get(), outputs[i].Get());
            differences[i] = CountDifferences(actual[i], expected[i], maximum[i]);
            WriteBytes(actualPrefix + std::to_wstring(i) + L".bin", actual[i]);
        }

        constexpr int BenchmarkFrames = 20;
        const auto benchmarkStart = std::chrono::steady_clock::now();
        for (int frame = 0; frame < BenchmarkFrames; ++frame)
            Require(ngu.RunNativeLumaInitial(inputView.Get(), Width, Height, rawViews.data()),
                    ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - benchmarkStart).count() /
            BenchmarkFrames;

        std::ofstream summary(summaryPath);
        Require(bool(summary), "write summary");
        const bool pass =
            std::all_of(differences.begin(), differences.end(), [](int value) { return value == 0; });
        summary << "{\n"
                << "  \"status\": \"" << (pass ? "PASS_PRODUCT" : "FAIL_PRODUCT") << "\",\n"
                << "  \"geometry\": [1920, 1080],\n"
                << "  \"different_half_components\": ["
                << differences[0] << ", " << differences[1] << ", " << differences[2] << "],\n"
                << "  \"maximum_half_code_delta\": [" << maximum[0] << ", " << maximum[1] << ", "
                << maximum[2] << "],\n"
                << "  \"native_ms_per_frame\": " << milliseconds << "\n"
                << "}\n";
        for (size_t i = 0; i < differences.size(); ++i)
            std::printf("target%zu different_half_components=%d maximum_half_code_delta=%d\n",
                        i,
                        differences[i],
                        maximum[i]);
        std::printf("%s native NGU Cubici1 1920x1080: %.3f ms/frame\n",
                    pass ? "PASS_PRODUCT" : "FAIL_PRODUCT",
                    milliseconds);
        ngu.Reset();
        rawViews.fill(nullptr);
        for (auto& view : outputViews)
            view.Reset();
        for (auto& output : outputs)
            output.Reset();
        inputView.Reset();
        input.Reset();
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
