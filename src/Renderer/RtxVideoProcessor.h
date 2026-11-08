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

    // The format the driver reads. NV12 and P010 are decoder pictures handed
    // over as they are, like MPC Video Renderer does. A picture that libplacebo prepares is
    // written into a single-plane texture instead: writing the two planes of a
    // P010 texture one after the other can leave the luma of an older picture
    // for the driver on NVIDIA. RGB is not taken for BT.2020 or PQ, Y410 is.
    enum class InputKind
    {
        Rgba8,
        Nv12,
        P010,
        Rgb10,
        Y410
    };

    // The single-plane format for a picture that libplacebo prepares.
    static InputKind PreparedInput(int bits, bool pq, bool bt2020, bool hdr)
    {
        if (bits <= 8 && !hdr)
            return InputKind::Rgba8;
        return pq || bt2020 ? InputKind::Y410 : InputKind::Rgb10;
    }

    // upscale: RTX Video Super Resolution; hdr: RTX Video HDR (SDR in, HDR10
    // out). Either or both.
    HRESULT Configure(ID3D11Device* device,
                      int w,
                      int h,
                      int ow,
                      int oh,
                      InputKind kind,
                      bool pq,
                      bool bt2020,
                      bool upscale,
                      bool hdr)
    {
        const bool yuv = kind == InputKind::Nv12 || kind == InputKind::P010 || kind == InputKind::Y410;
        const auto type = kind == InputKind::Nv12    ? DXGI_FORMAT_NV12
                          : kind == InputKind::P010  ? DXGI_FORMAT_P010
                          : kind == InputKind::Y410  ? DXGI_FORMAT_Y410
                          : kind == InputKind::Rgb10 ? DXGI_FORMAT_R10G10B10A2_UNORM
                                                     : DXGI_FORMAT_R8G8B8A8_UNORM;
        const auto source = yuv      ? (pq       ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020
                                        : bt2020 ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020
                                                 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709)
                            : bt2020 ? DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020
                                     : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
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
        const bool eightBit = kind == InputKind::Rgba8 || kind == InputKind::Nv12;
        const auto outType = !eightBit || hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
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
        const bool planar = kind == InputKind::Nv12 || kind == InputKind::P010;
        desc.Width = planar ? (w + 1) & ~1 : w;
        desc.Height = planar ? (h + 1) & ~1 : h;
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
    // The model can take a moment to become active, so one bright result
    // is not a refusal yet.
    HRESULT CheckHdr(ID3D11Device* device, ID3D11DeviceContext* immediate)
    {
        D3D11_TEXTURE2D_DESC inputDesc;
        input->GetDesc(&inputDesc);
        D3D11_RENDER_TARGET_VIEW_DESC view = {};
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        HRESULT hr = S_OK;
        if (inputDesc.Format == DXGI_FORMAT_P010 || inputDesc.Format == DXGI_FORMAT_NV12)
        {
            const bool tenBit = inputDesc.Format == DXGI_FORMAT_P010;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> luma, chroma;
            view.Format = tenBit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;
            hr = device->CreateRenderTargetView(input.Get(), &view, &luma);
            view.Format = tenBit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;
            if (SUCCEEDED(hr))
                hr = device->CreateRenderTargetView(input.Get(), &view, &chroma);
            if (FAILED(hr))
                return hr;
            // Limited range white and neutral chroma: 940 and 512 in P010,
            // 235 and 128 in NV12. Only this check writes the planes.
            const float white[4] = {tenBit ? 940 * 64 / 65535.f : 235 / 255.f, 0, 0, 0},
                        neutral[4] = {
                            tenBit ? 512 * 64 / 65535.f : 128 / 255.f, tenBit ? 512 * 64 / 65535.f : 128 / 255.f, 0, 0};
            immediate->ClearRenderTargetView(luma.Get(), white);
            immediate->ClearRenderTargetView(chroma.Get(), neutral);
        }
        else
        {
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> picture;
            view.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            hr = device->CreateRenderTargetView(input.Get(), &view, &picture);
            if (FAILED(hr))
                return hr;
            // Y410: limited range white (940) and neutral chroma (512), stored as
            // Cb, Y, Cr in the red, green and blue bits. RGB: full range white.
            const float yuvWhite[4] = {512 / 1023.f, 940 / 1023.f, 512 / 1023.f, 1}, rgbWhite[4] = {1, 1, 1, 1};
            immediate->ClearRenderTargetView(picture.Get(), inputDesc.Format == DXGI_FORMAT_Y410 ? yuvWhite : rgbWhite);
        }
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
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            if (attempt)
                Sleep(4);
            hr = Evaluate(0);
            if (FAILED(hr))
                return hr;
            immediate->CopySubresourceRegion(readback.Get(), 0, 0, 0, 0, output.Get(), 0, &box);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            hr = immediate->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr))
                return hr;
            const UINT green = (*static_cast<const UINT*>(mapped.pData) >> 10) & 1023;
            immediate->Unmap(readback.Get(), 0);
            // PQ code 0.75 is about 1000 nits; TrueHDR places SDR white far below.
            if (green < 1023 * 3 / 4)
                return S_OK;
        }
        return DXGI_ERROR_UNSUPPORTED;
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

    // An input view on a decoder picture of the input's format, read by the
    // driver directly.
    HRESULT InputView(ID3D11Texture2D* texture, Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>& view)
    {
        if (!video || !enumerator)
            return E_UNEXPECTED;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv = {};
        iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        return video->CreateVideoProcessorInputView(texture, enumerator.Get(), &iv, &view);
    }

    // source: the part of the input to process; destination: where it goes
    // in the target. Both stay set for later calls without them. picture:
    // a decoder picture instead of the own input texture.
    HRESULT Evaluate(UINT index,
                     ID3D11VideoProcessorOutputView* target = nullptr,
                     const RECT* source = nullptr,
                     const RECT* destination = nullptr,
                     ID3D11VideoProcessorInputView* picture = nullptr)
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
        stream.pInputSurface = picture ? picture : inputView.Get();
        return context->VideoProcessorBlt(processor.Get(), target ? target : outputView.Get(), index, 1, &stream);
    }
};
