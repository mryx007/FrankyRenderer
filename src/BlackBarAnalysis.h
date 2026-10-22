#pragma once
#include "BlackBarDetector.h"
#include <libplacebo/renderer.h>
#include <libplacebo/d3d11.h>
#include <wrl/client.h>

// Small asynchronous readback, shared by CPU-uploaded and native GPU input.
// Never map the decoder texture and never wait for an analysis result.
class BlackBarAnalysis
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target, readback;
    pl_tex wrapped = nullptr;
    pl_renderer renderer = nullptr;
    pl_gpu gpu = nullptr;
    bool pending = false;
    ULONGLONG last = 0;
    int width = 0, height = 0, sw = 0, sh = 0;
    pl_color_transfer transfer = PL_COLOR_TRC_UNKNOWN;
    bool dovi = false;

  public:
    BlackBarDetector detector;
    bool failed = false;
    uint64_t generation = 0;

    ~BlackBarAnalysis()
    {
        Reset();
    }

    void Reset()
    {
        if (gpu)
            pl_tex_destroy(gpu, &wrapped);
        pl_renderer_destroy(&renderer);
        readback.Reset();
        target.Reset();
        gpu = nullptr;
        pending = false;
        last = 0;
        sw = sh = 0;
        detector.Reset();
        failed = false;
        ++generation;
    }

    void Discontinuity()
    {
        pending = false;
        last = 0;
        detector.Discontinuity();
        ++generation;
    }

    void Analyze(pl_d3d11 d3d,
                 ID3D11DeviceContext* context,
                 const pl_frame& source,
                 int sourceW,
                 int sourceH,
                 const BlackBarSettings& settings)
    {
        if (!settings.enabled)
        {
            if (gpu)
                Reset();
            return;
        }
        if (sw != sourceW || sh != sourceH || transfer != source.color.transfer || dovi != bool(source.repr.dovi))
        {
            Reset();
            sw = sourceW;
            sh = sourceH;
            transfer = source.color.transfer;
            dovi = bool(source.repr.dovi);
        }
        if (failed || sourceW < 16 || sourceH < 16)
            return;
        const auto now = GetTickCount64();
        if (pending)
        {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            const HRESULT hr = context->Map(readback.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
                return;
            pending = false;
            if (FAILED(hr))
            {
                failed = true;
                return;
            }
            if (detector.Observe(
                    static_cast<const uint8_t*>(mapped.pData), width, height, mapped.RowPitch, sw, sh, settings, now))
                ++generation;
            context->Unmap(readback.Get(), 0);
        }
        // Only the first three observations form a fast startup burst.
        // Ambiguous dark/title frames must not cause a full scan every frame.
        const ULONGLONG interval = !detector.measured && detector.samples < 3
                                       ? 0
                                       : (settings.keepSubtitles || !detector.measured ? 200 : 1000);
        if (last && now - last < interval)
            return;
        last = now;
        if (!target)
        {
            gpu = d3d->gpu;
            width = std::min(sw, 1024);
            height = std::min(sh, 576);
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = d3d->device->CreateTexture2D(&desc, nullptr, &target);
            desc.BindFlags = 0;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (SUCCEEDED(hr))
                hr = d3d->device->CreateTexture2D(&desc, nullptr, &readback);
            if (FAILED(hr))
            {
                failed = true;
                return;
            }
            pl_d3d11_wrap_params wrap = {};
            wrap.tex = target.Get();
            wrapped = pl_d3d11_wrap(gpu, &wrap);
            renderer = pl_renderer_create(nullptr, gpu);
            if (!wrapped || !renderer)
            {
                failed = true;
                return;
            }
        }
        pl_frame input = source;
        input.crop = {0, 0, float(sw), float(sh)};
        input.field = PL_FIELD_NONE;
        input.prev = input.next = nullptr;
        input.overlays = nullptr;
        input.num_overlays = 0;
        pl_frame output = {};
        output.num_planes = 1;
        output.planes[0].texture = wrapped;
        output.planes[0].components = 4;
        for (int i = 0; i < 4; i++)
            output.planes[0].component_mapping[i] = i;
        output.crop = {0, 0, float(width), float(height)};
        output.color = pl_color_space_srgb;
        output.color.hdr.max_luma = 203;
        output.repr.sys = PL_COLOR_SYSTEM_RGB;
        output.repr.levels = PL_COLOR_LEVELS_FULL;
        pl_render_params params = pl_render_fast_params;
        params.upscaler = params.downscaler = &pl_filter_bilinear;
        params.peak_detect_params = nullptr;
        params.sigmoid_params = nullptr;
        params.dither_params = nullptr;
        if (!pl_render_image(renderer, &input, &output, &params))
        {
            failed = true;
            return;
        }
        pl_gpu_flush(gpu);
        context->CopyResource(readback.Get(), target.Get());
        pending = true;
    }
};
