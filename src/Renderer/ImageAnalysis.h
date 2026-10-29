#pragma once
#include "Renderer/ImageAnalysisPlot.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/shaders/colorspace.h>
#include <libplacebo/shaders/sampling.h>
#include <wrl/client.h>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

// Two small floating-point readbacks. Never map decoder/output textures, wait
// for the GPU, or analyse text/subtitle pixels. Plotting runs on a bounded worker.
class ImageAnalysis
{
    struct Stage
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture, staging;
        pl_tex wrapped = nullptr;
        bool pending = false;
        uint64_t captured = 0, key = 0;
        bool hdr = false, mixed = false;
        float reference = 203;
        float aspect = 16.f / 9;
        ImageAnalysisPlot::Sample latest;
    } stages[2];

    struct Job
    {
        ImageAnalysisPlot::Sample before, after;
        uint64_t generation;
        HWND window;
    };

    pl_d3d11 d3d = nullptr;
    ID3D11DeviceContext* context = nullptr;
    pl_hook hook = {};
    const pl_hook* ramp = nullptr;
    pl_color_space rampColor = {};
    pl_color_repr outputRepr = pl_color_repr_rgb, rampRepr = {};
    pl_tex bitmap = nullptr;
    uint64_t last = 0, key = 0;
    bool capture = false, failed = false, mixed = false;
    HWND window = nullptr;
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Job> job;
    std::optional<ImageAnalysisPlot::Image> ready;
    std::thread worker;
    uint64_t generation = 0;
    bool stopped = false;

    void Work()
    {
        for (;;)
        {
            std::optional<Job> next;
            {
                std::unique_lock guard(mutex);
                wake.wait(guard, [&] { return stopped || job.has_value(); });
                if (stopped)
                    return;
                next.swap(job);
            }
            try
            {
                auto image = ImageAnalysisPlot::Draw(next->before, next->after);
                std::lock_guard guard(mutex);
                if (!stopped && generation == next->generation && !image.rgba.empty())
                {
                    ready = std::move(image);
                    if (next->window)
                        PostMessageW(next->window, ReadyMessage, 0, 0);
                }
            }
            catch (...)
            {
            } // Diagnostics must not terminate playback.
        }
    }

    bool Ensure(Stage& s)
    {
        if (s.wrapped)
            return true;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = ImageAnalysisPlot::SampleWidth;
        desc.Height = ImageAnalysisPlot::SampleHeight;
        desc.MipLevels = desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = d3d->device->CreateTexture2D(&desc, nullptr, &s.texture);
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (SUCCEEDED(hr))
            hr = d3d->device->CreateTexture2D(&desc, nullptr, &s.staging);
        if (FAILED(hr))
            return false;
        pl_d3d11_wrap_params wrap = {};
        wrap.tex = s.texture.Get();
        s.wrapped = pl_d3d11_wrap(d3d->gpu, &wrap);
        return s.wrapped != nullptr;
    }

    void Capture(const pl_hook_params* p)
    {
        if (!capture || failed || !p->tex)
            return;
        auto& s = stages[p->stage == PL_HOOK_OUTPUT ? 1 : 0];
        if (s.pending || s.captured == key)
            return;
        if (!Ensure(s))
        {
            failed = true;
            return;
        }
        auto sh = pl_dispatch_begin(p->dispatch);
        pl_sample_src src = {};
        src.tex = p->tex;
        src.rect = p->rect;
        src.components = p->components;
        src.new_w = ImageAnalysisPlot::SampleWidth;
        src.new_h = ImageAnalysisPlot::SampleHeight;
        if (!pl_shader_sample_nearest(sh, &src))
        {
            pl_dispatch_abort(p->dispatch, &sh);
            return;
        }
        if (p->stage == PL_HOOK_OUTPUT)
        {
            auto repr = outputRepr;
            pl_color_repr_normalize(&repr);
            repr.alpha = PL_ALPHA_NONE;
            pl_shader_decode_color(sh, &repr, nullptr);
        }
        auto color = p->color;
        pl_color_space_infer(&color);
        pl_shader_linearize(sh, &color);
        const auto xyz = pl_get_rgb2xyz_matrix(pl_raw_primaries_get(color.primaries));
        s.hdr = p->stage == PL_HOOK_OUTPUT ? pl_color_space_is_hdr(&color)
                                           : p->orig_color && pl_color_space_is_hdr(p->orig_color);
        s.reference = !s.hdr && color.hdr.max_luma > 0 ? color.hdr.max_luma : PL_COLOR_SDR_WHITE;
        char body[512];
        sprintf_s(body,
                  "color.rgb *= %.9f; color.a = dot(color.rgb, vec3(%.9f, %.9f, %.9f));",
                  double(s.reference),
                  double(xyz.m[1][0]),
                  double(xyz.m[1][1]),
                  double(xyz.m[1][2]));
        pl_custom_shader custom = {};
        custom.description = "Image analysis: linear RGB and luminance";
        custom.input = custom.output = PL_SHADER_SIG_COLOR;
        custom.body = body;
        if (!pl_shader_custom(sh, &custom))
        {
            pl_dispatch_abort(p->dispatch, &sh);
            return;
        }
        pl_dispatch_params dispatch = {};
        dispatch.shader = &sh;
        dispatch.target = s.wrapped;
        if (!pl_dispatch_finish(p->dispatch, &dispatch))
        {
            failed = true;
            return;
        }
        pl_gpu_flush(d3d->gpu);
        context->CopyResource(s.staging.Get(), s.texture.Get());
        s.mixed = mixed;
        if (p->dst_rect.y1 != p->dst_rect.y0)
            s.aspect = std::abs(float(p->dst_rect.x1 - p->dst_rect.x0) / (p->dst_rect.y1 - p->dst_rect.y0));
        s.pending = true;
        s.key = key;
        s.captured = key;
        // One deferred repaint is enough to collect a paused image. If the
        // nonblocking map is not ready yet, Begin schedules another attempt.
        if (window)
            SetTimer(window, Timer, 100, nullptr);
    }

  public:
    static constexpr UINT ReadyMessage = WM_APP + 0x393;
    static constexpr UINT_PTR Timer = 0x4b2;

    ImageAnalysis()
    {
        hook.stages = static_cast<pl_hook_stage>(PL_HOOK_SCALED | PL_HOOK_OUTPUT);
        hook.input = PL_HOOK_SIG_TEX;
        hook.priv = this;
        hook.signature = 0x494d4753434f5045ULL;
        hook.hook = [](void* priv, const pl_hook_params* p)
        {
            static_cast<ImageAnalysis*>(priv)->Capture(p);
            return pl_hook_res{}; // Observation only, never replaces video.
        };
    }

    ~ImageAnalysis()
    {
        {
            std::lock_guard guard(mutex);
            stopped = true;
            wake.notify_all();
        }
        if (worker.joinable())
            worker.join();
        Reset();
    }

    void Reset()
    {
        if (window)
            KillTimer(window, Timer);
        {
            std::lock_guard guard(mutex);
            ++generation;
            job.reset();
            ready.reset();
        }
        for (auto& s : stages)
        {
            if (d3d)
                pl_tex_destroy(d3d->gpu, &s.wrapped);
            s = {};
        }
        if (d3d)
            pl_tex_destroy(d3d->gpu, &bitmap);
        pl_mpv_user_shader_destroy(&ramp);
        rampColor = {};
        d3d = nullptr;
        context = nullptr;
        window = nullptr;
        last = key = 0;
        capture = failed = false;
    }

    void Begin(pl_d3d11 device, ID3D11DeviceContext* ctx, HWND hwnd, uint64_t frameKey, bool mixing = false)
    {
        if (d3d && d3d != device)
            Reset();
        d3d = device;
        context = ctx;
        window = hwnd;
        key = frameKey ? frameKey : 1;
        mixed = mixing;
        bool fresh = false, pending = false;
        for (auto& s : stages)
        {
            if (!s.pending)
                continue;
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            const auto hr = context->Map(s.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
            {
                pending = true;
                continue;
            }
            s.pending = false;
            if (FAILED(hr))
            {
                failed = true;
                continue;
            }
            s.latest.pixels.resize(ImageAnalysisPlot::SampleWidth * ImageAnalysisPlot::SampleHeight);
            for (int y = 0; y < ImageAnalysisPlot::SampleHeight; ++y)
                memcpy(s.latest.pixels.data() + y * ImageAnalysisPlot::SampleWidth,
                       static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch,
                       ImageAnalysisPlot::SampleWidth * sizeof(ImageAnalysisPlot::Pixel));
            context->Unmap(s.staging.Get(), 0);
            s.latest.key = s.key;
            s.latest.reference = s.reference;
            s.latest.hdr = s.hdr;
            s.latest.mixed = s.mixed;
            s.latest.aspect = s.aspect;
            fresh = true;
        }
        if (pending && window)
            SetTimer(window, Timer, 100, nullptr);
        if (fresh)
        {
            std::lock_guard guard(mutex);
            if (!worker.joinable())
                worker = std::thread([this] { Work(); });
            job = Job{stages[0].latest, stages[1].latest, generation, window};
            wake.notify_one();
        }
        std::optional<ImageAnalysisPlot::Image> image;
        {
            std::lock_guard guard(mutex);
            image.swap(ready);
        }
        if (image)
        {
            pl_tex_params p = {};
            p.w = ImageAnalysisPlot::Width;
            p.h = ImageAnalysisPlot::Height;
            p.format = pl_find_named_fmt(d3d->gpu, "rgba8");
            p.sampleable = p.host_writable = true;
            if (p.format && pl_tex_recreate(d3d->gpu, &bitmap, &p))
            {
                pl_tex_transfer_params t = {};
                t.tex = bitmap;
                t.ptr = image->rgba.data();
                if (!pl_tex_upload(d3d->gpu, &t))
                    pl_tex_destroy(d3d->gpu, &bitmap);
            }
        }
        const auto now = GetTickCount64();
        capture = !last || now - last >= 200;
        if (capture)
            last = now;
    }

    const pl_hook* Observer() const
    {
        return &hook;
    }
#ifdef PLACEBO_TEST_IMAGE_ANALYSIS
    ImageAnalysisPlot::Sample SampleForTest(int stage) const
    {
        return stages[stage].latest;
    }
#endif

    const pl_hook* Ramp(const pl_color_space& color, const pl_color_repr& repr = pl_color_repr_rgb)
    {
        outputRepr = repr;
        if (!d3d)
            return nullptr;
        if (ramp && pl_color_space_equal(&rampColor, &color) && pl_color_repr_equal(&rampRepr, &repr))
            return ramp;
        pl_mpv_user_shader_destroy(&ramp);
        rampColor = color;
        rampRepr = repr;
        // Uniform output-signal steps, capped at diffuse white (203 nits)
        // in HDR. The generated strip then passes through actual dithering.
        float white[3] = {1, 1, 1};
        pl_color_delinearize(&color, white);
        const float peak = std::clamp(white[0], 0.f, 1.f);
        auto normalized = repr;
        pl_color_repr_normalize(&normalized);
        auto encode = pl_color_repr_decode(&normalized, nullptr);
        pl_transform3x3_invert(&encode);
        float black[3] = {0, 0, 0}, top[3] = {peak, peak, peak};
        pl_transform3x3_apply(&encode, black);
        pl_transform3x3_apply(&encode, top);
        char source[1024];
        sprintf_s(source,
                  "//!HOOK OUTPUT\n//!BIND HOOKED\nvec4 hook(){vec4 c=HOOKED_tex(HOOKED_pos);"
                  "if(HOOKED_pos.y>0.92){float t=clamp((HOOKED_pos.x-0.02)/0.96,0.0,1.0);"
                  "c=vec4(vec3(%.9f + t*%.9f),1.0);}return c;}\n",
                  double(black[0]),
                  double(top[0] - black[0]));
        ramp = pl_mpv_user_shader_parse(d3d->gpu, source, strlen(source));
        return ramp;
    }

    bool Describe(pl_overlay& overlay, pl_overlay_part& part, int width, int height)
    {
        if (!bitmap)
            return false;
        const float scale = std::max(0.f,
                                     std::min({std::clamp(float(height) / 1080.f, 1.f, 2.f),
                                               float(width - 32) / ImageAnalysisPlot::Width,
                                               float(height - 100) / ImageAnalysisPlot::Height}));
        if (scale <= 0)
            return false;
        const float w = ImageAnalysisPlot::Width * scale, h = ImageAnalysisPlot::Height * scale;
        part.src = {0, 0, float(ImageAnalysisPlot::Width), float(ImageAnalysisPlot::Height)};
        part.dst = {width - w - 16, 16, width - 16.f, h + 16};
        overlay = {};
        overlay.tex = bitmap;
        overlay.mode = PL_OVERLAY_NORMAL;
        overlay.coords = PL_OVERLAY_COORDS_DST_FRAME;
        overlay.repr.sys = PL_COLOR_SYSTEM_RGB;
        overlay.repr.levels = PL_COLOR_LEVELS_FULL;
        overlay.repr.alpha = PL_ALPHA_INDEPENDENT;
        overlay.color = pl_color_space_srgb;
        overlay.parts = &part;
        overlay.num_parts = 1;
        return true;
    }
};
