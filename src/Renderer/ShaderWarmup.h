#pragma once
#include <windows.h>
#include <libplacebo/cache.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/options.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include "Renderer/RtxScaling.h"
#include "Renderer/ShaderHooks.h"
#include "Renderer/Trace.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Large shaders take minutes to compile, and libplacebo compiles inside the
// render that first uses them. A worker compiles a shader on its own D3D11
// device instead, so playback goes on without it; the result reaches the
// playing device through the shader cache.
// The shader text depends on everything around it (render parameters such as
// debanding and frame mixing, the plane formats, the shader's own parameter
// values), so the worker renders the playing setup: otherwise the playing
// device compiles the shader again once it becomes active (ArtCNN: 15 s).
struct ShaderWarmupPlane
{
    std::string format;
    int width = 0, height = 0, components = 0;
    int mapping[4] = {};
};

struct ShaderWarmupJob
{
    std::atomic<int> state{0}; // 0 running, 1 done, 2 failed
    int id = 0;
    ShaderConfig config = {};
    float antiring = -1;
    // The playing setup: render parameters (pl_options_save, hooks left out;
    // empty: libplacebo defaults), source planes and the output format.
    std::string options;
    std::vector<ShaderWarmupPlane> planes;
    pl_color_repr sourceRepr = pl_color_repr_sdtv, targetRepr = pl_color_repr_rgb;
    pl_color_space sourceColor = pl_color_space_bt709, targetColor = pl_color_space_srgb;
    std::string targetFormat = "rgba8";
    int targetComponents = 4, targetMapping[4] = {0, 1, 2, 3};
    int screenWidth = 0, screenHeight = 0; // full screen on this monitor
    bool mix = false;                      // Smooth Motion blends frames

    // With RTX Video, its conversion pass before the driver, where shaders
    // that work before scaling run (issue #51): the source picture, the
    // prepared picture; the pass's parameters follow from `options`
    // (RtxScaling::ConversionParams). No target planes: none.
    struct Conversion
    {
        std::vector<ShaderWarmupPlane> sources, targets;
        pl_color_repr sourceRepr = pl_color_repr_sdtv, targetRepr = pl_color_repr_rgb;
        pl_color_space sourceColor = pl_color_space_bt709, targetColor = pl_color_space_bt709;
        pl_rect2df sourceCrop = {}, targetCrop = {};
        pl_field field = PL_FIELD_NONE;
    } conversion;

    // In: the playing device's cache, so known shaders load at once.
    // Out, once done: the same cache with the new shader.
    std::vector<uint8_t> cache;
    LUID adapter = {};
    // The playing device's level: compiled shaders depend on it, and a
    // player's own device (PotPlayer) may run below the highest level.
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL(0);
    HWND notify = nullptr;
    HMODULE module = nullptr;
};

// A plain 8-bit 4:2:0 BT.709 video of the given size, for a job without a
// playing setup.
inline void DescribeWarmupVideo(ShaderWarmupJob& job, int width, int height)
{
    job.planes = {{"r8", width, height, 1, {PL_CHANNEL_Y}},
                  {"rg8", (width + 1) / 2, (height + 1) / 2, 2, {PL_CHANNEL_CB, PL_CHANNEL_CR}}};
    job.sourceRepr = pl_color_repr_sdtv;
    job.sourceRepr.sys = PL_COLOR_SYSTEM_BT_709;
    job.sourceColor = pl_color_space_bt709;
}

// The warm-up's description of RTX's conversion pass (RtxScaling::DescribeConversion);
// empty when a source plane is unknown.
inline ShaderWarmupJob::Conversion DescribeWarmupConversion(const RtxScaling::Conversion& conversion)
{
    ShaderWarmupJob::Conversion described;
    bool complete = true;
    const auto& source = conversion.picture;
    for (int i = 0; i < source.num_planes && i < 4; i++)
    {
        const auto& plane = source.planes[i];
        if (!plane.texture)
        {
            complete = false;
            break;
        }
        ShaderWarmupPlane p;
        p.format = plane.texture->params.format->name;
        p.width = plane.texture->params.w;
        p.height = plane.texture->params.h;
        p.components = plane.components;
        for (int c = 0; c < 4; c++)
            p.mapping[c] = plane.component_mapping[c];
        described.sources.push_back(p);
    }
    const auto& prepared = conversion.prepared;
    for (int i = 0; complete && i < prepared.num_planes && i < 2; i++)
    {
        ShaderWarmupPlane p;
        p.format = conversion.formats[i] ? conversion.formats[i] : "";
        p.width = conversion.widths[i];
        p.height = conversion.heights[i];
        p.components = prepared.planes[i].components;
        for (int c = 0; c < 4; c++)
            p.mapping[c] = prepared.planes[i].component_mapping[c];
        described.targets.push_back(p);
    }
    described.sourceRepr = source.repr;
    described.sourceColor = source.color;
    described.sourceCrop = source.crop;
    described.field = source.field;
    described.targetRepr = prepared.repr;
    described.targetColor = prepared.color;
    described.targetCrop = prepared.crop;
    if (!complete)
        described = {};
    return described;
}

// Renders RTX's conversion pass with the shader, as playback does once the
// shader runs before the driver.
inline bool WarmUpConversion(pl_gpu gpu, pl_renderer renderer, const pl_hook* hook, const ShaderWarmupJob& job)
{
    const auto& c = job.conversion;
    if (c.sources.empty() || c.sources.size() > 4 || c.targets.empty() || c.targets.size() > 4)
        return false;
    // The playing options, from which RtxScaling derives the pass's own.
    pl_options options = pl_options_alloc(nullptr);
    bool ok = options && (job.options.empty() || pl_options_load(options, job.options.c_str()));
    std::vector<pl_tex> textures;
    pl_frame source = {}, target = {};
    const auto planes = [&](const std::vector<ShaderWarmupPlane>& from, pl_frame& frame, bool render)
    {
        for (size_t i = 0; ok && i < from.size(); i++)
        {
            const auto& p = from[i];
            pl_tex_params tp = {};
            tp.w = std::max(p.width, 1);
            tp.h = std::max(p.height, 1);
            tp.format = pl_find_named_fmt(gpu, p.format.c_str());
            tp.sampleable = true;
            tp.renderable = render;
            tp.blit_dst = render;
            pl_tex tex = tp.format ? pl_tex_create(gpu, &tp) : nullptr;
            ok = tex != nullptr;
            textures.push_back(tex);
            frame.planes[i].texture = tex;
            frame.planes[i].components = p.components;
            for (int n = 0; n < 4; n++)
                frame.planes[i].component_mapping[n] = p.mapping[n];
        }
        frame.num_planes = int(from.size());
    };
    planes(c.sources, source, false);
    planes(c.targets, target, true);
    source.repr = c.sourceRepr;
    source.color = c.sourceColor;
    source.crop = c.sourceCrop;
    source.field = c.field;
    target.repr = c.targetRepr;
    target.color = c.targetColor;
    target.crop = c.targetCrop;
    if (ok)
    {
        // As in playback: no dynamic constants in this pass.
        pl_color_map_params mapping;
        pl_render_params render =
            RtxScaling::ConversionParams(job.options.empty() ? pl_render_default_params : options->params, mapping);
        render.hooks = &hook;
        render.num_hooks = 1;
        ok = pl_render_image(renderer, &source, &target, &render);
        pl_gpu_finish(gpu);
    }
    if (!ok)
        Trace("Shader warm-up: RTX conversion pass failed");
    for (auto& tex : textures)
        pl_tex_destroy(gpu, &tex);
    pl_options_free(&options);
    return ok;
}

inline bool WarmUpShader(ShaderWarmupJob& job)
{
    if (job.planes.empty() || job.planes.size() > 4)
        return false;
    auto cacheParams = pl_cache_default_params;
    cacheParams.max_object_size = 16 * 1024 * 1024;
    cacheParams.max_total_size = 64 * 1024 * 1024;
    pl_cache cache = pl_cache_create(&cacheParams);
    if (!cache)
        return false;
    if (!job.cache.empty())
        pl_cache_load(cache, job.cache.data(), job.cache.size());
    auto params = pl_d3d11_default_params;
    params.adapter_luid = job.adapter;
    if (job.featureLevel)
        params.min_feature_level = params.max_feature_level = job.featureLevel;
    pl_d3d11 d3d = pl_d3d11_create(nullptr, &params);
    bool ok = false;
    if (!d3d)
        Trace("Shader warm-up: no D3D11 device");
    if (d3d)
    {
        const pl_gpu gpu = d3d->gpu;
        pl_gpu_set_cache(gpu, cache);
        const pl_hook* hook = ParseUserShader(gpu, job.id, job.config, job.antiring);
        pl_options options = pl_options_alloc(nullptr);
        pl_renderer renderer = pl_renderer_create(nullptr, gpu);
        std::vector<pl_tex> textures;
        pl_frame input = {};
        bool ready =
            hook && options && renderer && (job.options.empty() || pl_options_load(options, job.options.c_str()));
        for (size_t i = 0; ready && i < job.planes.size(); i++)
        {
            const auto& p = job.planes[i];
            pl_tex_params tp = {};
            tp.w = std::max(p.width, 1);
            tp.h = std::max(p.height, 1);
            tp.format = pl_find_named_fmt(gpu, p.format.c_str());
            tp.sampleable = true;
            pl_tex tex = tp.format ? pl_tex_create(gpu, &tp) : nullptr;
            ready = tex != nullptr;
            textures.push_back(tex);
            input.planes[i].texture = tex;
            input.planes[i].components = p.components;
            for (int c = 0; c < 4; c++)
                input.planes[i].component_mapping[c] = p.mapping[c];
        }
        input.num_planes = int(job.planes.size());
        input.repr = job.sourceRepr;
        input.color = job.sourceColor;
        ok = ready;
        if (ready)
        {
            pl_render_params render = job.options.empty() ? pl_render_default_params : options->params;
            // As in playback: sizes are runtime values (dynamic constants),
            // which changes the shader text and so the cache key.
            render.dynamic_constants = true;
            render.hooks = &hook;
            render.num_hooks = 1;
            const float w = float(job.planes[0].width), h = float(job.planes[0].height);
            // Full screen on this monitor first (where enlarging shaders switch
            // on, //!WHEN; black bars where the shapes differ), then twice and
            // three times the size; each with the whole picture and with black
            // bars cropped, which arranges the passes otherwise.
            const bool screen = job.screenWidth > 0 && job.screenHeight > 0;
            uint64_t signature = 1;
            for (const double scale : {0.0, 2.0, 3.2})
                for (const bool cropped : {false, true})
                {
                    input.crop = {0, cropped ? std::round(h * .02f) : 0, w, cropped ? h - std::round(h * .015f) : h};
                    const float cw = pl_rect_w(input.crop), ch = pl_rect_h(input.crop);
                    const double enlarge = scale > 0 || !screen ? std::max(scale, 2.0)
                                                                : std::min(job.screenWidth / cw, job.screenHeight / ch);
                    const int pictureW = std::clamp(int(std::lround(cw * enlarge)), 16, 8192),
                              pictureH = std::clamp(int(std::lround(ch * enlarge)), 16, 8192);
                    pl_tex_params tp = {};
                    tp.w = scale > 0 || !screen ? pictureW : std::max(job.screenWidth, pictureW);
                    tp.h = scale > 0 || !screen ? pictureH : std::max(job.screenHeight, pictureH);
                    tp.format = pl_find_named_fmt(gpu, job.targetFormat.c_str());
                    tp.renderable = true;
                    tp.blit_dst = true;
                    pl_tex target = tp.format ? pl_tex_create(gpu, &tp) : nullptr;
                    pl_frame output = {};
                    output.num_planes = 1;
                    output.planes[0].texture = target;
                    output.planes[0].components = job.targetComponents;
                    for (int c = 0; c < 4; c++)
                        output.planes[0].component_mapping[c] = job.targetMapping[c];
                    output.repr = job.targetRepr;
                    output.color = job.targetColor;
                    const float left = float(tp.w - pictureW) / 2, top = float(tp.h - pictureH) / 2;
                    output.crop = {left, top, left + float(pictureW), top + float(pictureH)};
                    bool drawn = false;
                    if (target && job.mix)
                    {
                        // Two frames blended, as Smooth Motion between frames.
                        const pl_frame* frames[2] = {&input, &input};
                        const uint64_t signatures[2] = {signature, signature + 1};
                        const double timestamps[2] = {-.8, .2};
                        pl_frame_mix mix = {};
                        mix.num_frames = 2;
                        mix.frames = frames;
                        mix.signatures = signatures;
                        mix.timestamps = timestamps;
                        mix.vsync_duration = .4;
                        drawn = pl_render_image_mix(renderer, &mix, &output, &render);
                    }
                    else if (target)
                        drawn = pl_render_image(renderer, &input, &output, &render);
                    signature += 2;
                    pl_gpu_finish(gpu);
                    if (!drawn)
                        Trace("Shader warm-up render %dx%d failed", tp.w, tp.h);
                    ok = drawn && ok;
                    pl_tex_destroy(gpu, &target);
                }
            // With RTX Video a shader that works before scaling runs in its
            // conversion pass instead. A failure there leaves the rest.
            constexpr unsigned beforeScaling = PL_HOOK_SIGMOID - 1;
            if (ok && hook->stages && !(hook->stages & ~beforeScaling) && !job.conversion.targets.empty())
                WarmUpConversion(gpu, renderer, hook, job);
        }
        else
            Trace("Shader warm-up: setup failed (hook %d, renderer %d, options %d)",
                  hook != nullptr,
                  renderer != nullptr,
                  options != nullptr);
        pl_renderer_destroy(&renderer);
        pl_options_free(&options);
        pl_mpv_user_shader_destroy(&hook);
        for (auto& tex : textures)
            pl_tex_destroy(gpu, &tex);
        pl_gpu_set_cache(gpu, nullptr);
        pl_d3d11_destroy(&d3d);
    }
    if (ok)
    {
        const size_t size = pl_cache_save(cache, nullptr, 0);
        job.cache.resize(size);
        ok = size && pl_cache_save(cache, job.cache.data(), size) == size;
    }
    pl_cache_destroy(&cache);
    return ok;
}

inline DWORD WINAPI ShaderWarmupThread(void* parameter)
{
    auto* handed = static_cast<std::shared_ptr<ShaderWarmupJob>*>(parameter);
    std::shared_ptr<ShaderWarmupJob> job = std::move(*handed);
    delete handed;
    bool ok = false;
    try
    {
        ok = WarmUpShader(*job);
    }
    catch (...)
    {
    }
    job->state = ok ? 1 : 2;
    if (job->notify)
        PostMessageW(job->notify, WM_PAINT, 0, 0); // a paused video shows the shader now
    const HMODULE module = job->module;
    job.reset();
    // The renderer may already be gone; this thread keeps its DLL loaded.
    FreeLibraryAndExitThread(module, 0);
}

// Starts the worker; on failure the job is marked failed and the shader then
// compiles during playback as before.
inline void StartShaderWarmup(const std::shared_ptr<ShaderWarmupJob>& job)
{
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&ShaderWarmupThread), &job->module))
    {
        job->state = 2;
        return;
    }
    auto* handed = new std::shared_ptr<ShaderWarmupJob>(job);
    if (HANDLE thread = CreateThread(nullptr, 0, ShaderWarmupThread, handed, 0, nullptr))
    {
        CloseHandle(thread);
        return;
    }
    delete handed;
    FreeLibrary(job->module);
    job->state = 2;
}
