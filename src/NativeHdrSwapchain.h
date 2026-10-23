#pragma once
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <libplacebo/d3d11.h>

// Vendor passthrough consumes already PQ/BT.2020-coded RGB. Keep these values
// in a 10-bit UNORM buffer; do not route them through a linear scRGB surface.
// The driver session supplies signaling instead of changing Windows HDR.
class NativeHdrSwapchain
{
    template <class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<IDXGISwapChain1> swapchain;
    pl_gpu gpu = nullptr;
    pl_tex target = nullptr;
    HWND owner = nullptr;
    int width = 0, height = 0;
    UINT originalLatency = 0;

  public:
    ~NativeHdrSwapchain()
    {
        Destroy();
    }

    bool Active() const
    {
        return swapchain != nullptr;
    }

    IDXGISwapChain1* Get() const
    {
        return swapchain.Get();
    }

    pl_tex Target() const
    {
        return target;
    }

    void Destroy()
    {
        if (gpu)
            pl_tex_destroy(gpu, &target);
        swapchain.Reset();
        gpu = nullptr;
        owner = nullptr;
        width = height = 0;
        originalLatency = 0;
    }

    HRESULT Ensure(pl_d3d11 d3d, HWND window, int w, int h, int maximumLatency)
    {
        if (w <= 0 || h <= 0 || maximumLatency < 0 || maximumLatency > 16)
            return E_INVALIDARG;
        if (owner && owner != window)
            Destroy();
        gpu = d3d->gpu;
        if (!swapchain)
        {
            Ptr<IDXGIDevice> device;
            Ptr<IDXGIAdapter> adapter;
            Ptr<IDXGIFactory2> factory;
            HRESULT hr = d3d->device->QueryInterface(IID_PPV_ARGS(&device));
            if (SUCCEEDED(hr))
                hr = device->GetAdapter(&adapter);
            if (SUCCEEDED(hr))
                hr = adapter->GetParent(IID_PPV_ARGS(&factory));
            if (FAILED(hr))
                return hr;
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            desc.Width = w;
            desc.Height = h;
            desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 3;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            hr = factory->CreateSwapChainForHwnd(d3d->device, window, &desc, nullptr, nullptr, &swapchain);
            if (FAILED(hr))
                return hr;
            factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
            owner = window;
            width = w;
            height = h;
        }
        if (width != w || height != h)
        {
            pl_tex_destroy(gpu, &target);
            pl_gpu_flush(gpu);
            const HRESULT hr = swapchain->ResizeBuffers(
                0, w, h, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
            if (FAILED(hr))
                return hr;
            width = w;
            height = h;
        }
        // The waitable swapchain has its own limit, separate from the device's
        // frame latency. Preserve its initial value for the Default setting.
        {
            Ptr<IDXGISwapChain2> latency;
            HRESULT hr = swapchain.As(&latency);
            if (FAILED(hr))
                return hr;
            UINT actual = 0;
            hr = latency->GetMaximumFrameLatency(&actual);
            if (FAILED(hr))
                return hr;
            if (!originalLatency)
                originalLatency = actual;
            const UINT wanted = maximumLatency ? UINT(maximumLatency) : originalLatency;
            if (actual != wanted)
            {
                hr = latency->SetMaximumFrameLatency(wanted);
                if (FAILED(hr))
                    return hr;
            }
        }
        if (!target)
        {
            Ptr<ID3D11Texture2D> back;
            const HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
            if (FAILED(hr))
                return hr;
            pl_d3d11_wrap_params wrap = {};
            wrap.tex = back.Get();
            target = pl_d3d11_wrap(gpu, &wrap);
            if (!target)
                return E_FAIL;
        }
        return S_OK;
    }

    HRESULT Submit()
    {
        if (!swapchain || !target)
            return E_UNEXPECTED;
        pl_gpu_flush(gpu);
        return swapchain->Present(1, 0);
    }
};
