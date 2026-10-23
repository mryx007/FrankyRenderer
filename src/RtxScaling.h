#pragma once
#include "RtxVideoProcessor.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/filters.h>
#include <map>
#include <string>
#include <memory>
#include <cmath>
#include <cstring>

// Convert/deinterlace each decoded image once, upscale it, then let the normal
// renderer mix, color-map and composite subtitles at the presentation rate.
class RtxScaling
{
  public:
    struct Frame
    {
        pl_gpu gpu = nullptr;
        pl_tex texture = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pixels;
        pl_frame image = {};
        pl_rect2df sourceCrop = {};
        const pl_frame* previous = nullptr;
        const pl_frame* next = nullptr;
        pl_field firstField = PL_FIELD_NONE;

        ~Frame()
        {
            if (texture)
                pl_tex_destroy(gpu, &texture);
        }
    };

  private:
    RtxVideoProcessor processor;
    pl_gpu gpu = nullptr;
    pl_renderer converter = nullptr;
    pl_tex input[2] = {};
    pl_color_space sourceColor = {};
    std::map<uint64_t, std::shared_ptr<Frame>> frames;
    int width = 0, height = 0, outputWidth = 0, outputHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool failed = false;
    uint64_t evaluations = 0;
    std::wstring status = L"off";

    void Fail(HRESULT result)
    {
        frames.clear();
        for (auto& plane : input)
            if (plane)
                pl_tex_destroy(gpu, &plane);
        processor.Reset();
        wchar_t text[128];
        swprintf_s(text, L"unavailable (0x%08X); normal scaling", unsigned(result));
        status = text;
        failed = true;
    }

    static pl_frame Describe(pl_tex texture, const pl_color_space& color, int bits)
    {
        pl_frame image = {};
        image.num_planes = 1;
        image.planes[0].texture = texture;
        image.planes[0].components = 3;
        for (int i = 0; i < 4; ++i)
            image.planes[0].component_mapping[i] = i < 3 ? i : -1;
        image.crop = {0, 0, float(texture->params.w), float(texture->params.h)};
        image.color = color;
        image.repr = pl_color_repr_rgb;
        image.repr.bits.sample_depth = image.repr.bits.color_depth = bits;
        return image;
    }

  public:
    ~RtxScaling()
    {
        Reset();
    }

    const std::wstring& Status() const
    {
        return status;
    }

    uint64_t Evaluations() const
    {
        return evaluations;
    }

    void Invalidate()
    {
        frames.clear();
        failed = false;
        if (converter)
            pl_renderer_flush_cache(converter);
    }

    void Reset()
    {
        Invalidate();
        for (auto& plane : input)
            if (plane)
                pl_tex_destroy(gpu, &plane);
        pl_renderer_destroy(&converter);
        processor.Reset();
        gpu = nullptr;
        width = height = outputWidth = outputHeight = 0;
        format = DXGI_FORMAT_UNKNOWN;
        status = L"off";
    }

    std::shared_ptr<Frame> Get(pl_d3d11 d3d,
                               ID3D11DeviceContext* context,
                               const pl_frame& source,
                               uint64_t key,
                               int outW,
                               int outH,
                               const pl_render_params& options)
    {
        const int w = int(std::ceil(std::abs(source.crop.x1 - source.crop.x0)));
        const int h = int(std::ceil(std::abs(source.crop.y1 - source.crop.y0)));
        if (w < 1 || h < 1 || outW < w || outH < h || (outW == w && outH == h))
        {
            status = L"bypassed (no upscaling)";
            return {};
        }
        if (source.rotation || source.repr.dovi || source.repr.alpha != PL_ALPHA_UNKNOWN ||
            source.color.transfer == PL_COLOR_TRC_HLG || source.color.transfer == PL_COLOR_TRC_LINEAR ||
            (source.color.transfer == PL_COLOR_TRC_PQ && source.color.primaries != PL_COLOR_PRIM_BT_2020))
        {
            status = L"bypassed (unsupported source format)";
            return {};
        }
        if (failed)
            return {};
        const int bits = pl_color_space_is_hdr(&source.color) || source.repr.bits.color_depth > 8 ? 10 : 8;
        const auto type = bits == 10 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (width != w || height != h || outputWidth != outW || outputHeight != outH || format != type ||
            !pl_color_space_equal(&sourceColor, &source.color))
        {
            frames.clear();
            for (auto& plane : input)
                if (plane)
                    pl_tex_destroy(gpu, &plane);
            width = w;
            height = h;
            outputWidth = outW;
            outputHeight = outH;
            sourceColor = source.color;
            format = type;
        }
        if (const auto found = frames.find(key); found != frames.end())
        {
            const auto& saved = found->second;
            if (!memcmp(&saved->sourceCrop, &source.crop, sizeof(source.crop)) && saved->previous == source.prev &&
                saved->next == source.next && saved->firstField == source.first_field)
            {
                status = L"driver requested (cached)";
                return saved;
            }
        }
        gpu = d3d->gpu;
        if (!converter)
            converter = pl_renderer_create(nullptr, gpu);
        if (!converter)
        {
            Fail(E_OUTOFMEMORY);
            return {};
        }
        const bool pq = source.color.transfer == PL_COLOR_TRC_PQ;
        const bool bt2020 = source.color.primaries == PL_COLOR_PRIM_BT_2020;
        HRESULT hr = processor.Configure(d3d->device, w, h, outW, outH, bits, pq, bt2020);
        if (FAILED(hr))
        {
            Fail(hr);
            return {};
        }
        D3D11_TEXTURE2D_DESC inputDesc;
        processor.Input()->GetDesc(&inputDesc);
        for (int i = 0; i < (bits > 8 ? 2 : 1); ++i)
        {
            if (input[i])
                continue;
            pl_d3d11_wrap_params wrap = {};
            wrap.tex = processor.Input();
            if (bits > 8)
            {
                wrap.fmt = i ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R16_UNORM;
                wrap.w = inputDesc.Width / (i ? 2 : 1);
                wrap.h = inputDesc.Height / (i ? 2 : 1);
            }
            input[i] = pl_d3d11_wrap(gpu, &wrap);
            if (!input[i] || !input[i]->params.renderable)
            {
                Fail(E_FAIL);
                return {};
            }
        }
        auto prepared = Describe(input[0], source.color, bits);
        prepared.crop = {0, 0, float(w), float(h)};
        if (bits > 8)
        {
            prepared.num_planes = 2;
            prepared.planes[0].components = 1;
            prepared.planes[0].component_mapping[1] = prepared.planes[0].component_mapping[2] = -1;
            prepared.planes[1].texture = input[1];
            prepared.planes[1].components = 2;
            prepared.planes[1].component_mapping[0] = 1;
            prepared.planes[1].component_mapping[1] = 2;
            prepared.planes[1].component_mapping[2] = prepared.planes[1].component_mapping[3] = -1;
            prepared.repr.sys = bt2020 ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
            prepared.repr.levels = PL_COLOR_LEVELS_LIMITED;
            prepared.repr.bits = {16, 10, 6};
            pl_frame_set_chroma_location(&prepared, PL_CHROMA_LEFT);
        }
        pl_render_params conversion = pl_render_fast_params;
        conversion.upscaler = conversion.downscaler = &pl_filter_nearest;
        conversion.plane_upscaler = options.plane_upscaler ? options.plane_upscaler
                                    : options.upscaler     ? options.upscaler
                                                           : &pl_filter_bilinear;
        conversion.plane_downscaler = options.plane_downscaler ? options.plane_downscaler
                                      : options.downscaler     ? options.downscaler
                                                               : &pl_filter_bilinear;
        conversion.antiringing_strength = options.antiringing_strength;
        conversion.deinterlace_params = options.deinterlace_params;
        pl_color_map_params mapping = pl_color_map_default_params;
        mapping.tone_mapping_function = &pl_tone_map_clip;
        mapping.gamut_mapping = &pl_gamut_map_clip;
        mapping.contrast_recovery = 0;
        conversion.color_map_params = &mapping;
        if (!pl_render_image(converter, &source, &prepared, &conversion))
        {
            Fail(E_FAIL);
            return {};
        }
        pl_gpu_flush(gpu);
        hr = processor.Evaluate();
        if (FAILED(hr))
        {
            Fail(hr);
            return {};
        }
        auto saved = std::make_shared<Frame>();
        saved->gpu = gpu;
        saved->sourceCrop = source.crop;
        saved->previous = source.prev;
        saved->next = source.next;
        saved->firstField = source.first_field;
        D3D11_TEXTURE2D_DESC desc;
        processor.Output()->GetDesc(&desc);
        desc.MiscFlags = 0;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        hr = d3d->device->CreateTexture2D(&desc, nullptr, &saved->pixels);
        if (FAILED(hr))
        {
            Fail(hr);
            return {};
        }
        context->CopyResource(saved->pixels.Get(), processor.Output());
        pl_d3d11_wrap_params wrap = {};
        wrap.tex = saved->pixels.Get();
        saved->texture = pl_d3d11_wrap(gpu, &wrap);
        if (!saved->texture)
        {
            Fail(E_FAIL);
            return {};
        }
        saved->image = Describe(saved->texture, source.color, bits);
        if (frames.size() >= 16 && !frames.count(key))
            frames.erase(frames.begin());
        frames[key] = saved;
        ++evaluations;
        status = L"driver requested";
        return saved;
    }
};
