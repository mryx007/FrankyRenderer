#pragma once
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <utility>

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
    bool superResolution = false, trueHdr = false;
    // A newly created processor: its first output is not trusted.
    bool fresh = false;

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
        superResolution = trueHdr = false;
        fresh = false;
    }

    // upscale: RTX Video Super Resolution; hdr: RTX Video HDR (SDR in, HDR10
    // out). Either or both.
    HRESULT Configure(
        ID3D11Device* device, int w, int h, int ow, int oh, int bits, bool pq, bool bt2020, bool upscale, bool hdr)
    {
        const auto type = bits > 8 ? DXGI_FORMAT_P010 : DXGI_FORMAT_R8G8B8A8_UNORM;
        const auto source =
            bits > 8 ? (pq       ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020
                        : bt2020 ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020
                                 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709)
                     : (bt2020 ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        const auto target = pq || hdr ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
                            : bt2020  ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020
                                      : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        if (processor && owner == device && width == w && height == h && outputWidth == ow && outputHeight == oh &&
            inputFormat == type && sourceSpace == source && superResolution == upscale && trueHdr == hdr)
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
        const auto outType = bits > 8 || hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (SUCCEEDED(hr))
            hr = formats->CheckVideoProcessorFormatConversion(type, source, outType, target, &supported);
        if (FAILED(hr))
            return hr;
        // SDR to PQ is only produced by the TrueHDR extension; drivers do not
        // list it as a plain format conversion.
        if (!supported && !hdr)
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
        if (upscale)
        {
            constexpr GUID extension = {0xd43ce1b3, 0x1f4b, 0x48ac, {0xba, 0xee, 0xc3, 0xc2, 0x53, 0x75, 0xe6, 0xf7}};

            struct
            {
                UINT version, method, enable;
            } request = {1, 2, 1};

            hr = context->VideoProcessorSetStreamExtension(processor.Get(), 0, &extension, sizeof(request), &request);
            if (FAILED(hr))
                return hr;
        }
        if (hdr)
        {
            // TrueHDR request: interface version 4, method 3, as MPC Video
            // Renderer and Chromium send it.
            constexpr GUID extension = {0xfdd62bb4, 0x620b, 0x4fd7, {0x9a, 0xb3, 0x1e, 0x59, 0xd0, 0xd5, 0x44, 0xb3}};

            struct
            {
                UINT version, method, enable;
            } request = {4, 3, 1};

            hr = context->VideoProcessorSetStreamExtension(processor.Get(), 0, &extension, sizeof(request), &request);
            if (FAILED(hr))
                return hr;
        }
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
        if (SUCCEEDED(hr) && hdr)
            hr = CheckHdr(device, immediate.Get());
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
        superResolution = upscale;
        trueHdr = hdr;
        fresh = true;
        return S_OK;
    }

    // Without an active TrueHDR model the driver writes SDR codes straight
    // into PQ: SDR white turns into almost 10000 nits. Send one white picture
    // and keep RTX Video HDR only when it comes back at a plausible level.
    HRESULT CheckHdr(ID3D11Device* device, ID3D11DeviceContext* immediate)
    {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> luma, chroma;
        D3D11_RENDER_TARGET_VIEW_DESC view = {};
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        view.Format = DXGI_FORMAT_R16_UNORM;
        HRESULT hr = device->CreateRenderTargetView(input.Get(), &view, &luma);
        view.Format = DXGI_FORMAT_R16G16_UNORM;
        if (SUCCEEDED(hr))
            hr = device->CreateRenderTargetView(input.Get(), &view, &chroma);
        if (FAILED(hr))
            return hr;
        // Limited range 10-bit white (940) and neutral chroma (512) in P010.
        const float white[4] = {940 * 64 / 65535.f, 0, 0, 0},
                    neutral[4] = {512 * 64 / 65535.f, 512 * 64 / 65535.f, 0, 0};
        immediate->ClearRenderTargetView(luma.Get(), white);
        immediate->ClearRenderTargetView(chroma.Get(), neutral);
        hr = Evaluate(0);
        if (FAILED(hr))
            return hr;
        D3D11_TEXTURE2D_DESC desc;
        output->GetDesc(&desc);
        const UINT x = desc.Width / 2, y = desc.Height / 2;
        desc.Width = desc.Height = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
        hr = device->CreateTexture2D(&desc, nullptr, &readback);
        if (FAILED(hr))
            return hr;
        const D3D11_BOX box = {x, y, 0, x + 1, y + 1, 1};
        immediate->CopySubresourceRegion(readback.Get(), 0, 0, 0, 0, output.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = immediate->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
            return hr;
        const UINT green = (*static_cast<const UINT*>(mapped.pData) >> 10) & 1023;
        immediate->Unmap(readback.Get(), 0);
        // PQ code 0.75 is about 1000 nits; TrueHDR places SDR white far below.
        return green < 1023 * 3 / 4 ? S_OK : DXGI_ERROR_UNSUPPORTED;
    }

    // index counts video frames, as MPC Video Renderer passes them: the same
    // frame again keeps its number.
    bool TakeFresh()
    {
        return std::exchange(fresh, false);
    }

    // An output view on a texture of the output's size and format, so the
    // driver writes each frame straight into its own texture.
    HRESULT OutputView(ID3D11Texture2D* texture, Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView>& view)
    {
        if (!video || !enumerator)
            return E_UNEXPECTED;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov = {};
        ov.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        return video->CreateVideoProcessorOutputView(texture, enumerator.Get(), &ov, &view);
    }

    // source: the part of the input to process; destination: where it goes
    // in the target. Both stay set for later calls without them.
    HRESULT Evaluate(UINT index,
                     ID3D11VideoProcessorOutputView* target = nullptr,
                     const RECT* source = nullptr,
                     const RECT* destination = nullptr)
    {
        if (!processor || !inputView || !outputView)
            return E_UNEXPECTED;
        if (source)
            context->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, source);
        if (destination)
        {
            context->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, destination);
            context->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, destination);
        }
        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.InputFrameOrField = index;
        stream.pInputSurface = inputView.Get();
        return context->VideoProcessorBlt(processor.Get(), target ? target : outputView.Get(), index, 1, &stream);
    }
};
