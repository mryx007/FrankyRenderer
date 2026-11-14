#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
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
constexpr UINT kSourceWidth = 1280;
constexpr UINT kSourceHeight = 720;
constexpr UINT kDenseWidth = 1282;
constexpr UINT kDenseHeight = 722;
constexpr UINT kOutputWidth = 2560;
constexpr UINT kOutputHeight = 1440;

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

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    Require(bool(file), "open shader");
    const auto size = file.tellg();
    file.seekg(0);
    std::string text(size_t(size), '\0');
    Require(bool(file.read(text.data(), size)), "read shader");
    return text;
}

ComPtr<ID3DBlob> Compile(const std::filesystem::path& path,
                         const char* entry,
                         const char* target)
{
    const std::string source = ReadText(path);
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.data(),
                                  source.size(),
                                  path.string().c_str(),
                                  nullptr,
                                  nullptr,
                                  entry,
                                  target,
                                  D3DCOMPILE_ENABLE_STRICTNESS |
                                      D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                  0,
                                  &blob,
                                  &errors);
    if (FAILED(hr))
    {
        const char* message =
            errors ? static_cast<const char*>(errors->GetBufferPointer())
                   : "shader compilation failed";
        throw std::runtime_error(message);
    }
    return blob;
}

ComPtr<ID3D11Texture2D> CreateTexture(ID3D11Device* device,
                                      UINT width,
                                      UINT height,
                                      DXGI_FORMAT format,
                                      const std::vector<uint8_t>& bytes)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = bytes.empty() ? nullptr : bytes.data();
    data.SysMemPitch = width * 8;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device->CreateTexture2D(
                &desc, bytes.empty() ? nullptr : &data, &texture)),
            "create texture");
    return texture;
}

ComPtr<ID3D11Buffer> CreateConstants(ID3D11Device* device,
                                     const std::vector<uint8_t>& bytes)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = static_cast<UINT>(bytes.size());
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = bytes.data();
    ComPtr<ID3D11Buffer> buffer;
    Require(SUCCEEDED(device->CreateBuffer(&desc, &data, &buffer)),
            "create constants");
    return buffer;
}

struct Pass
{
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
};

Pass CreatePass(ID3D11Device* device,
                const std::filesystem::path& shader)
{
    auto vertexBlob = Compile(shader, "VSMain", "vs_5_0");
    auto pixelBlob = Compile(shader, "PSMain", "ps_5_0");
    Pass pass;
    Require(SUCCEEDED(device->CreateVertexShader(
                vertexBlob->GetBufferPointer(),
                vertexBlob->GetBufferSize(),
                nullptr,
                &pass.vertex)),
            "create vertex shader");
    Require(SUCCEEDED(device->CreatePixelShader(
                pixelBlob->GetBufferPointer(),
                pixelBlob->GetBufferSize(),
                nullptr,
                &pass.pixel)),
            "create pixel shader");
    return pass;
}

void Run(ID3D11DeviceContext* context,
         const Pass& pass,
         ID3D11Buffer* constants,
         const std::vector<std::pair<UINT, ID3D11ShaderResourceView*>>&
             inputs,
         const std::vector<ID3D11RenderTargetView*>& outputs,
         UINT width,
         UINT height)
{
    Require(!outputs.empty() && outputs.size() <= D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
            "invalid outputs");

    ID3D11ShaderResourceView* nullInputs[D3D11_PS_INPUT_REGISTER_COUNT] = {};
    context->PSSetShaderResources(0, D3D11_PS_INPUT_REGISTER_COUNT, nullInputs);
    for (const auto& input : inputs)
    {
        Require(input.first < D3D11_PS_INPUT_REGISTER_COUNT,
                "invalid input slot");
        ID3D11ShaderResourceView* view = input.second;
        context->PSSetShaderResources(input.first, 1, &view);
    }
    context->OMSetRenderTargets(
        static_cast<UINT>(outputs.size()), outputs.data(), nullptr);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(pass.vertex.Get(), nullptr, 0);
    context->PSSetShader(pass.pixel.Get(), nullptr, 0);
    ID3D11Buffer* constantSlot = constants;
    context->PSSetConstantBuffers(0, 1, &constantSlot);
    const D3D11_VIEWPORT viewport = {
        0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);
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
    {
        std::memcpy(bytes.data() + size_t(y) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        size_t(y) * mapped.RowPitch,
                    rowBytes);
    }
    context->Unmap(staging.Get(), 0);
    return bytes;
}

void Compare(const char* label,
             const std::vector<uint8_t>& actual,
             const std::vector<uint8_t>& expected)
{
    Require(actual.size() == expected.size(), "comparison size");
    int maximum = 0;
    size_t different = 0;
    for (size_t i = 0; i < actual.size(); i += 2)
    {
        const uint16_t a =
            uint16_t(actual[i]) | (uint16_t(actual[i + 1]) << 8);
        const uint16_t b =
            uint16_t(expected[i]) | (uint16_t(expected[i + 1]) << 8);
        if (a != b)
        {
            ++different;
            maximum = std::max(maximum, std::abs(int(a) - int(b)));
        }
    }
    std::printf("%s different=%zu maximum=%d\n",
                label, different, maximum);
}

void Dump(const char* label, const std::vector<uint8_t>& bytes)
{
    const char* directory = std::getenv("NGU_HIGHBIT_DUMP_DIR");
    if (!directory || !*directory)
        return;
    std::filesystem::create_directories(directory);
    std::ofstream file(std::filesystem::path(directory) / label,
                       std::ios::binary);
    Require(bool(file), "open dump");
    file.write(reinterpret_cast<const char*>(bytes.data()),
               std::streamsize(bytes.size()));
}

struct Surface
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> view;
};

Surface MakeSurface(ID3D11Device* device,
                    UINT width,
                    UINT height,
                    DXGI_FORMAT format,
                    const std::vector<uint8_t>& bytes = {})
{
    Surface surface;
    surface.texture =
        CreateTexture(device, width, height, format, bytes);
    Require(SUCCEEDED(device->CreateRenderTargetView(
                surface.texture.Get(), nullptr, &surface.target)),
            "create render target");
    Require(SUCCEEDED(device->CreateShaderResourceView(
                surface.texture.Get(), nullptr, &surface.view)),
            "create shader view");
    return surface;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3,
                "usage: test_ngu_highbit_720 <vh4-capture> <full-trace>");
        const std::filesystem::path capture(argv[1]);
        const std::filesystem::path fullTrace(argv[2]);
        const std::filesystem::path shaders =
            std::filesystem::path("Shaders") / "Ngu";

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL featureLevel = {};
        Require(SUCCEEDED(D3D11CreateDevice(nullptr,
                                            D3D_DRIVER_TYPE_HARDWARE,
                                            nullptr,
                                            0,
                                            nullptr,
                                            0,
                                            D3D11_SDK_VERSION,
                                            &device,
                                            &featureLevel,
                                            &context)),
                "create D3D11 device");

        const Pass layer6140 =
            CreatePass(device.Get(), shaders / "luma_720_highbit_6140.hlsl");
        const Pass layer4280 =
            CreatePass(device.Get(), shaders / "luma_720_highbit_4280.hlsl");
        const Pass dense1528 =
            CreatePass(device.Get(), shaders / "luma_720_highbit_1528.hlsl");
        const Pass toRgb =
            CreatePass(device.Get(), shaders / "luma_720_highbit_to_rgb.hlsl");

        const auto makeCapture = [&](UINT width,
                                     UINT height,
                                     DXGI_FORMAT format,
                                     const char* name) {
            return MakeSurface(
                device.Get(),
                width,
                height,
                format,
                ReadBytes(capture / name, size_t(width) * height * 8));
        };

        auto s0 = makeCapture(
            kSourceWidth, kSourceHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
            "input_0087_PS_6140_e8f1d399_s0_1280x720_f113_7372800.bin");
        auto s2 = makeCapture(
            kSourceWidth, kSourceHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
            "input_0087_PS_6140_e8f1d399_s2_1280x720_f113_7372800.bin");
        auto s3 = makeCapture(
            kSourceWidth, kSourceHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
            "input_0087_PS_6140_e8f1d399_s3_1280x720_f113_7372800.bin");
        auto o0 = MakeSurface(
            device.Get(), kSourceWidth, kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto o1 = MakeSurface(
            device.Get(), kSourceWidth, kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto c6140 = CreateConstants(
            device.Get(),
            ReadBytes(shaders / "luma_720_highbit_6140_constants.bin", 3520));
        Run(context.Get(),
            layer6140,
            c6140.Get(),
            {{0, s0.view.Get()}, {1, s2.view.Get()}, {2, s3.view.Get()}},
            {o0.target.Get(), o1.target.Get()},
            kSourceWidth,
            kSourceHeight);
        const auto ps6140rt0 =
            Download(device.Get(), context.Get(), o0.texture.Get());
        const auto ps6140rt1 =
            Download(device.Get(), context.Get(), o1.texture.Get());
        Dump("PS6140.rt0.bin", ps6140rt0);
        Dump("PS6140.rt1.bin", ps6140rt1);
        Compare("PS6140.rt0",
                ps6140rt0,
                ReadBytes(capture /
                              "draw_0087_PS_6140_e8f1d399_rt0_1280x720_f113_7372800.bin",
                          size_t(kSourceWidth) * kSourceHeight * 8));
        Compare("PS6140.rt1",
                ps6140rt1,
                ReadBytes(capture /
                              "draw_0087_PS_6140_e8f1d399_rt1_1280x720_f113_7372800.bin",
                          size_t(kSourceWidth) * kSourceHeight * 8));

        // The three PS_4280 states are replayed from their captured inputs.
        for (int passIndex = 0; passIndex < 3; ++passIndex)
        {
            const int draw = 88 + passIndex;
            char input0Name[160] = {};
            char input1Name[160] = {};
            char output0Name[160] = {};
            char output1Name[160] = {};
            std::snprintf(
                input0Name, sizeof(input0Name),
                "input_%04d_PS_4280_d591580d_s0_1280x720_f113_7372800.bin",
                draw);
            std::snprintf(
                input1Name, sizeof(input1Name),
                "input_%04d_PS_4280_d591580d_s2_1280x720_f113_7372800.bin",
                draw);
            std::snprintf(
                output0Name, sizeof(output0Name),
                "draw_%04d_PS_4280_d591580d_rt0_1280x720_f113_7372800.bin",
                draw);
            std::snprintf(
                output1Name, sizeof(output1Name),
                "draw_%04d_PS_4280_d591580d_rt1_1280x720_f113_7372800.bin",
                draw);
            auto a = makeCapture(kSourceWidth,
                                 kSourceHeight,
                                 DXGI_FORMAT_R16G16B16A16_FLOAT,
                                 input0Name);
            auto b = makeCapture(kSourceWidth,
                                 kSourceHeight,
                                 DXGI_FORMAT_R16G16B16A16_FLOAT,
                                 input1Name);
            auto out0 = MakeSurface(device.Get(),
                                    kSourceWidth,
                                    kSourceHeight,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT);
            auto out1 = MakeSurface(device.Get(),
                                    kSourceWidth,
                                    kSourceHeight,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT);
            char constantsName[160] = {};
            std::snprintf(constantsName,
                          sizeof(constantsName),
                          "luma_720_highbit_4280_%d_constants.bin",
                          passIndex);
            auto constants = CreateConstants(
                device.Get(),
                ReadBytes(shaders / constantsName, 2368));
            Run(context.Get(),
                layer4280,
                constants.Get(),
                {{0, a.view.Get()}, {1, b.view.Get()}},
                {out0.target.Get(), out1.target.Get()},
                kSourceWidth,
                kSourceHeight);
            char label[64] = {};
            std::snprintf(label, sizeof(label), "PS4280.%d.rt0", passIndex);
            Compare(label,
                    Download(device.Get(), context.Get(), out0.texture.Get()),
                    ReadBytes(capture / output0Name,
                              size_t(kSourceWidth) * kSourceHeight * 8));
            std::snprintf(label, sizeof(label), "PS4280.%d.rt1", passIndex);
            Compare(label,
                    Download(device.Get(), context.Get(), out1.texture.Get()),
                    ReadBytes(capture / output1Name,
                              size_t(kSourceWidth) * kSourceHeight * 8));
        }

        auto denseInput0 = makeCapture(
            kSourceWidth, kSourceHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
            "input_0091_PS_1528_f0e4bb92_s0_1280x720_f113_7372800.bin");
        auto denseInput1 = makeCapture(
            kSourceWidth, kSourceHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
            "input_0091_PS_1528_f0e4bb92_s2_1280x720_f113_7372800.bin");
        auto denseOutput = MakeSurface(
            device.Get(), kDenseWidth, kDenseHeight,
            DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto denseConstants = CreateConstants(
            device.Get(),
            ReadBytes(shaders / "luma_720_highbit_1528_constants.bin", 560));
        Run(context.Get(),
            dense1528,
            denseConstants.Get(),
            {{0, denseInput0.view.Get()}, {1, denseInput1.view.Get()}},
            {denseOutput.target.Get()},
            kDenseWidth,
            kDenseHeight);
        const auto denseBytes =
            Download(device.Get(), context.Get(), denseOutput.texture.Get());
        Dump("PS1528.bin", denseBytes);
        Compare("PS1528",
                denseBytes,
                ReadBytes(capture /
                              "draw_0091_PS_1528_f0e4bb92_rt0_1282x722_f113_7404832.bin",
                          size_t(kDenseWidth) * kDenseHeight * 8));

        const Pass initial1412 =
            CreatePass(device.Get(), shaders / "luma_1080_cubici1.hlsl");
        auto initialSource = MakeSurface(
            device.Get(),
            kSourceWidth,
            kSourceHeight,
            DXGI_FORMAT_R16G16B16A16_UNORM,
            ReadBytes(fullTrace / "input_00399_PS_1412_aad01c2f_s0.bin",
                      size_t(kSourceWidth) * kSourceHeight * 8));
        auto initial0 = MakeSurface(device.Get(),
                                    kSourceWidth,
                                    kSourceHeight,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto initial1 = MakeSurface(device.Get(),
                                    kSourceWidth,
                                    kSourceHeight,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto initial2 = MakeSurface(device.Get(),
                                    kSourceWidth,
                                    kSourceHeight,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto initialConstants = CreateConstants(
            device.Get(),
            ReadBytes(shaders / "luma_720_highbit_1412_0_constants.bin",
                      32 * 16));
        Run(context.Get(),
            initial1412,
            initialConstants.Get(),
            {{0, initialSource.view.Get()}},
            {initial0.target.Get(),
             initial1.target.Get(),
             initial2.target.Get()},
            kSourceWidth,
            kSourceHeight);
        const auto initial0Bytes =
            Download(device.Get(), context.Get(), initial0.texture.Get());
        Dump("PS1412.rt0.bin", initial0Bytes);
        Compare(
            "PS1412.rt0",
            initial0Bytes,
            ReadBytes(fullTrace /
                          "draw_00399_PS_1412_aad01c2f_rt0_1280x720_f113.bin",
                      size_t(kSourceWidth) * kSourceHeight * 8));

        const auto makeFullTrace = [&](UINT width,
                                       UINT height,
                                       DXGI_FORMAT format,
                                       const char* name) {
            return MakeSurface(
                device.Get(),
                width,
                height,
                format,
                ReadBytes(fullTrace / name, size_t(width) * height * 8));
        };
        auto y = makeFullTrace(kOutputWidth,
                               kOutputHeight,
                               DXGI_FORMAT_R16G16B16A16_UNORM,
                               "input_00447_PS_908_2a1d27a9_s0.bin");
        auto cb = makeFullTrace(kOutputWidth,
                                kOutputHeight,
                                DXGI_FORMAT_R16G16B16A16_UNORM,
                                "input_00447_PS_908_2a1d27a9_s2.bin");
        auto cr = makeFullTrace(kOutputWidth,
                                kOutputHeight,
                                DXGI_FORMAT_R16G16B16A16_UNORM,
                                "input_00447_PS_908_2a1d27a9_s3.bin");
        auto residual = makeFullTrace(kDenseWidth,
                                      kDenseHeight,
                                      DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      "input_00447_PS_908_2a1d27a9_s4.bin");
        auto rgb = MakeSurface(device.Get(),
                               kOutputWidth,
                               kOutputHeight,
                               DXGI_FORMAT_R16G16B16A16_UNORM);
        auto rgbConstants = CreateConstants(
            device.Get(),
            ReadBytes(shaders / "luma_720_highbit_to_rgb_constants.bin", 32));
        Run(context.Get(),
            toRgb,
            rgbConstants.Get(),
            {{0, y.view.Get()},
             {1, cb.view.Get()},
             {2, cr.view.Get()},
             {3, residual.view.Get()}},
            {rgb.target.Get()},
            kOutputWidth,
            kOutputHeight);
        const auto rgbBytes =
            Download(device.Get(), context.Get(), rgb.texture.Get());
        Dump("PS908.bin", rgbBytes);
        Compare("PS908",
                rgbBytes,
                ReadBytes(fullTrace /
                              "draw_00447_PS_908_2a1d27a9_rt0_2560x1440_f36.bin",
                          size_t(kOutputWidth) * kOutputHeight * 8));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::printf("FAIL_INFRA %s\n", error.what());
        return 2;
    }
}
