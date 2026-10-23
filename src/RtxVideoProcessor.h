#pragma once
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

// The driver owns the model and quality setting. This is the D3D11 request
// used by MPC Video Renderer and Chromium, not an NVIDIA SDK integration.
class RtxVideoProcessor
{
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> video;
    Microsoft::WRL::ComPtr<ID3D11VideoContext1> context;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> processor;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> input, output;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> inputView;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outputView;
    ID3D11Device* owner = nullptr;
    int width = 0, height = 0, outputWidth = 0, outputHeight = 0;
    DXGI_FORMAT inputFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_COLOR_SPACE_TYPE sourceSpace = DXGI_COLOR_SPACE_CUSTOM;
    UINT frame = 0;

  public:
    ID3D11Texture2D* Input() const
    {
        return input.Get();
    }

    ID3D11Texture2D* Output() const
    {
        return output.Get();
    }

    void Reset()
    {
        outputView.Reset();
        inputView.Reset();
        output.Reset();
        input.Reset();
        processor.Reset();
        enumerator.Reset();
        context.Reset();
        video.Reset();
        owner = nullptr;
        width = height = outputWidth = outputHeight = 0;
        inputFormat = DXGI_FORMAT_UNKNOWN;
        sourceSpace = DXGI_COLOR_SPACE_CUSTOM;
        frame = 0;
    }

    HRESULT Configure(ID3D11Device* device, int w, int h, int ow, int oh, int bits, bool pq, bool bt2020)
    {
        const auto type = bits > 8 ? DXGI_FORMAT_P010 : DXGI_FORMAT_R8G8B8A8_UNORM;
        const auto source =
            bits > 8 ? (pq       ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020
                        : bt2020 ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020
                                 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709)
                     : (bt2020 ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        const auto target = pq       ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                            : bt2020 ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020
                                     : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        if (processor && owner == device && width == w && height == h && outputWidth == ow && outputHeight == oh &&
            inputFormat == type && sourceSpace == source)
            return S_OK;
        Reset();
        if (w < 1 || h < 1 || ow < 1 || oh < 1 || w > 16384 || h > 16384 || ow > 16384 || oh > 16384)
            return E_INVALIDARG;
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC info = {};
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgi));
        if (SUCCEEDED(hr))
            hr = dxgi->GetAdapter(&adapter);
        if (SUCCEEDED(hr))
            hr = adapter->GetDesc(&info);
        if (FAILED(hr))
            return hr;
        if (info.VendorId != 0x10de)
            return DXGI_ERROR_UNSUPPORTED;
        hr = device->QueryInterface(IID_PPV_ARGS(&video));
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
        device->GetImmediateContext(&immediate);
        if (SUCCEEDED(hr))
            hr = immediate.As(&context);
        if (FAILED(hr))
            return hr;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content = {};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = content.OutputFrameRate = {24, 1};
        content.InputWidth = w;
        content.InputHeight = h;
        content.OutputWidth = ow;
        content.OutputHeight = oh;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        hr = video->CreateVideoProcessorEnumerator(&content, &enumerator);
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator1> formats;
        if (SUCCEEDED(hr))
            hr = enumerator.As(&formats);
        BOOL supported = FALSE;
        const auto outType = bits > 8 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (SUCCEEDED(hr))
            hr = formats->CheckVideoProcessorFormatConversion(type, source, outType, target, &supported);
        if (FAILED(hr))
            return hr;
        if (!supported)
            return DXGI_ERROR_UNSUPPORTED;
        hr = video->CreateVideoProcessor(enumerator.Get(), 0, &processor);
        if (FAILED(hr))
            return hr;
        context->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);
        context->VideoProcessorSetStreamFrameFormat(processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        context->VideoProcessorSetStreamColorSpace1(processor.Get(), 0, source);
        context->VideoProcessorSetOutputColorSpace1(processor.Get(), target);
        RECT src = {0, 0, w, h}, dst = {0, 0, ow, oh};
        context->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &src);
        context->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &dst);
        context->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &dst);
        constexpr GUID extension = {0xd43ce1b3, 0x1f4b, 0x48ac, {0xba, 0xee, 0xc3, 0xc2, 0x53, 0x75, 0xe6, 0xf7}};

        struct
        {
            UINT version, method, enable;
        } request = {1, 2, 1};

        hr = context->VideoProcessorSetStreamExtension(processor.Get(), 0, &extension, sizeof(request), &request);
        if (FAILED(hr))
            return hr;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = bits > 8 ? (w + 1) & ~1 : w;
        desc.Height = bits > 8 ? (h + 1) & ~1 : h;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = type;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        hr = device->CreateTexture2D(&desc, nullptr, &input);
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv = {};
        iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        if (SUCCEEDED(hr))
            hr = video->CreateVideoProcessorInputView(input.Get(), enumerator.Get(), &iv, &inputView);
        desc.Width = ow;
        desc.Height = oh;
        desc.Format = outType;
        if (SUCCEEDED(hr))
            hr = device->CreateTexture2D(&desc, nullptr, &output);
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov = {};
        ov.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        if (SUCCEEDED(hr))
            hr = video->CreateVideoProcessorOutputView(output.Get(), enumerator.Get(), &ov, &outputView);
        if (FAILED(hr))
        {
            Reset();
            return hr;
        }
        owner = device;
        width = w;
        height = h;
        outputWidth = ow;
        outputHeight = oh;
        inputFormat = type;
        sourceSpace = source;
        return S_OK;
    }

    HRESULT Evaluate()
    {
        if (!processor || !inputView || !outputView)
            return E_UNEXPECTED;
        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.InputFrameOrField = frame;
        stream.pInputSurface = inputView.Get();
        return context->VideoProcessorBlt(processor.Get(), outputView.Get(), frame++, 1, &stream);
    }
};
