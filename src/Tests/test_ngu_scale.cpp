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
    const UINT pixelBytes = format == DXGI_FORMAT_R16_UNORM ? 2 : 8;
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
    const size_t rowBytes = size_t(desc.Width) * 2;
    std::vector<uint8_t> bytes(rowBytes * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                    rowBytes);
    context->Unmap(staging.Get(), 0);
    return bytes;
}

std::array<int, 4> CompareL16(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
{
    Require(actual.size() * 4 == expected.size(), "L16 comparison size");
    std::array<int, 4> counts = {};
    for (size_t i = 0; i < actual.size() / 2; ++i)
    {
        const uint16_t left = uint16_t(actual[i * 2]) | (uint16_t(actual[i * 2 + 1]) << 8);
        const uint16_t right = uint16_t(expected[i * 8]) | (uint16_t(expected[i * 8 + 1]) << 8);
        const int delta = int(left) - int(right);
        if (delta == 0)
            ++counts[0];
        else if (delta == 1)
            ++counts[1];
        else if (delta == -1)
            ++counts[2];
        else
            ++counts[3];
    }
    return counts;
}

std::vector<uint16_t> L16Input(const std::filesystem::path& path, int width, int height)
{
    const auto bytes = ReadBytes(path, size_t(width) * height * 8);
    std::vector<uint16_t> values(size_t(width) * height);
    for (size_t i = 0; i < values.size(); ++i)
        std::memcpy(&values[i], bytes.data() + i * 8, sizeof(uint16_t));
    return values;
}

void PrintCounts(const char* stage, const std::array<int, 4>& counts)
{
    std::printf("%s exact=%d plus1=%d minus1=%d other=%d\n", stage, counts[0], counts[1], counts[2], counts[3]);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: test_ngu_scale <capture-root> <actual-prefix>");
        const std::filesystem::path root(argv[1]);
        const std::wstring actualPrefix = std::filesystem::path(argv[2]).wstring();

        auto d3dParams = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &d3dParams);
        Require(d3d != nullptr, "D3D11 GPU");
        ID3D11Device* device = d3d->device;
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);

        NguScalerRequest request;
        request.sourceWidth = 1920;
        request.sourceHeight = 1080;
        request.stage = NguScalerStage::VeryHighLumaScaleX1080;
        Require(ProbeNguScaler(request) == NguScalerSupport::Ready, "X scale capability");

        NguScaler ngu;
        const bool prepared = ngu.PrepareNativeLumaScaleAdd(d3d->gpu);
        Require(prepared, ngu.LastError());

        const auto xInputData = L16Input(root / "input_31704_PS_748_721dd04e_s0.bin", 1920, 1080);
        auto xInput = CreateTexture(
            device, 1920, 1080, DXGI_FORMAT_R16_UNORM, D3D11_BIND_SHADER_RESOURCE, xInputData.data());
        ComPtr<ID3D11ShaderResourceView> xInputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(xInput.Get(), nullptr, &xInputView)), "X input SRV");
        auto xOutput = CreateTexture(
            device, 3840, 1080, DXGI_FORMAT_R16_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> xOutputView;
        ComPtr<ID3D11ShaderResourceView> xOutputSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(xOutput.Get(), nullptr, &xOutputView)), "X output RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(xOutput.Get(), nullptr, &xOutputSrv)), "X output SRV");
        const bool xRan = ngu.RunNativeLumaScaleX(xInputView.Get(), xOutputView.Get());
        Require(xRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto xActual = Download(device, context.Get(), xOutput.Get());
        const auto xCounts = CompareL16(
            xActual, ReadBytes(root / "draw_31704_PS_748_721dd04e_rt0_3840x1080_f81.bin", size_t(3840) * 1080 * 8));
        constexpr int BenchmarkFrames = 20;
        auto benchmark = [&](auto&& run)
        {
            const auto start = std::chrono::steady_clock::now();
            for (int frame = 0; frame < BenchmarkFrames; ++frame)
                Require(run(), "benchmark pass");
            pl_gpu_finish(d3d->gpu);
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
                   BenchmarkFrames;
        };
        const double xMs = benchmark([&] { return ngu.RunNativeLumaScaleX(xInputView.Get(), xOutputView.Get()); });
        PrintCounts("XScale2x", xCounts);
        std::printf("XScale2x %.3f ms/pass\n", xMs);

        auto yInput = CreateTexture(
            device, 3840, 1080, DXGI_FORMAT_R16_UNORM, D3D11_BIND_SHADER_RESOURCE, xActual.data());
        ComPtr<ID3D11ShaderResourceView> yInputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(yInput.Get(), nullptr, &yInputView)), "Y input SRV");
        auto yOutput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> yOutputView;
        ComPtr<ID3D11ShaderResourceView> yOutputSrv;
        Require(SUCCEEDED(device->CreateRenderTargetView(yOutput.Get(), nullptr, &yOutputView)), "Y output RTV");
        Require(SUCCEEDED(device->CreateShaderResourceView(yOutput.Get(), nullptr, &yOutputSrv)), "Y output SRV");
        const bool yRan = ngu.RunNativeLumaScaleY(yInputView.Get(), yOutputView.Get());
        Require(yRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto yActual = Download(device, context.Get(), yOutput.Get());
        const auto yCounts = CompareL16(
            yActual, ReadBytes(root / "draw_31705_PS_748_4085ea73_rt0_3840x2160_f81.bin", size_t(3840) * 2160 * 8));
        const double yMs = benchmark([&] { return ngu.RunNativeLumaScaleY(yInputView.Get(), yOutputView.Get()); });
        PrintCounts("YScale2x", yCounts);
        std::printf("YScale2x %.3f ms/pass\n", yMs);

        const auto denseInputData =
            ReadBytes(root / "input_31723_PS_564_500335c8_s2.bin", size_t(1922) * 1082 * 8);
        auto denseInput = CreateTexture(
            device,
            1922,
            1082,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE,
            denseInputData.data());
        ComPtr<ID3D11ShaderResourceView> denseInputView;
        Require(SUCCEEDED(device->CreateShaderResourceView(denseInput.Get(), nullptr, &denseInputView)),
                "dense input SRV");
        auto addOutput = CreateTexture(
            device, 3840, 2160, DXGI_FORMAT_R16_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ComPtr<ID3D11RenderTargetView> addOutputView;
        Require(SUCCEEDED(device->CreateRenderTargetView(addOutput.Get(), nullptr, &addOutputView)), "add RTV");
        const bool addRan = ngu.RunNativeLumaAdd(yOutputSrv.Get(), denseInputView.Get(), addOutputView.Get());
        Require(addRan, ngu.LastError());
        pl_gpu_finish(d3d->gpu);
        const auto addActual = Download(device, context.Get(), addOutput.Get());
        const auto addCounts = CompareL16(
            addActual, ReadBytes(root / "draw_31723_PS_564_500335c8_rt0_3840x2160_f81.bin", size_t(3840) * 2160 * 8));
        const double addMs =
            benchmark([&] { return ngu.RunNativeLumaAdd(yOutputSrv.Get(), denseInputView.Get(), addOutputView.Get()); });
        PrintCounts("Cubicd20", addCounts);
        std::printf("Cubicd20 %.3f ms/pass\n", addMs);
        {
            std::ofstream file(actualPrefix + L"x.bin", std::ios::binary);
            file.write(reinterpret_cast<const char*>(xActual.data()), std::streamsize(xActual.size()));
            file.close();
            file.open(actualPrefix + L"y.bin", std::ios::binary);
            file.write(reinterpret_cast<const char*>(yActual.data()), std::streamsize(yActual.size()));
            file.close();
            file.open(actualPrefix + L"add.bin", std::ios::binary);
            file.write(reinterpret_cast<const char*>(addActual.data()), std::streamsize(addActual.size()));
        }

        const bool pass = xCounts[3] == 0 && yCounts[3] == 0 && addCounts[3] == 0;
        std::printf("%s native NGU scale/add\n", pass ? "PASS_PRODUCT" : "FAIL_PRODUCT");
        ngu.Reset();
        addOutputView.Reset();
        addOutput.Reset();
        denseInputView.Reset();
        denseInput.Reset();
        yOutputSrv.Reset();
        yOutputView.Reset();
        yOutput.Reset();
        yInputView.Reset();
        yInput.Reset();
        xOutputSrv.Reset();
        xOutputView.Reset();
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
