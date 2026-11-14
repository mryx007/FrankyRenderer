#pragma once
#include <windows.h>
#include <libplacebo/cache.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
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
struct ShaderWarmupJob
{
    std::atomic<int> state{0}; // 0 running, 1 done, 2 failed
    std::string source;
    // In: the playing device's cache, so known shaders load at once.
    // Out, once done: the same cache with the new shader.
    std::vector<uint8_t> cache;
    LUID adapter = {};
    // The playing device's level: compiled shaders depend on it, and a
    // player's own device (PotPlayer) may run below the highest level.
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL(0);
    int width = 0, height = 0;
    bool deep = false, rgb = false;
    HWND notify = nullptr;
    HMODULE module = nullptr;
};

inline bool WarmUpShader(ShaderWarmupJob& job)
{
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
        const pl_hook* hook = pl_mpv_user_shader_parse(gpu, job.source.data(), job.source.size());
        const int w = std::max(job.width, 16), h = std::max(job.height, 16);
        const auto plane = [&](const char* format, int pw, int ph)
        {
            pl_tex_params p = {};
            p.w = pw;
            p.h = ph;
            p.format = pl_find_named_fmt(gpu, format);
            p.sampleable = true;
            return p.format ? pl_tex_create(gpu, &p) : nullptr;
        };
        pl_tex planes[2] = {};
        pl_frame input = {};
        if (job.rgb)
        {
            planes[0] = plane(job.deep ? "rgba16" : "rgba8", w, h);
            input.num_planes = 1;
            input.planes[0].texture = planes[0];
            input.planes[0].components = 4;
            for (int i = 0; i < 4; i++)
                input.planes[0].component_mapping[i] = i;
            input.repr = pl_color_repr_rgb;
        }
        else
        {
            planes[0] = plane(job.deep ? "r16" : "r8", w, h);
            planes[1] = plane(job.deep ? "rg16" : "rg8", (w + 1) / 2, (h + 1) / 2);
            input.num_planes = 2;
            input.planes[0].texture = planes[0];
            input.planes[0].components = 1;
            input.planes[0].component_mapping[0] = PL_CHANNEL_Y;
            input.planes[1].texture = planes[1];
            input.planes[1].components = 2;
            input.planes[1].component_mapping[0] = PL_CHANNEL_CB;
            input.planes[1].component_mapping[1] = PL_CHANNEL_CR;
            input.repr = pl_color_repr_sdtv;
            input.repr.sys = PL_COLOR_SYSTEM_BT_709;
        }
        input.color = pl_color_space_bt709;
        pl_renderer renderer = pl_renderer_create(nullptr, gpu);
        if (hook && renderer && planes[0] && (job.rgb || planes[1]))
        {
            ok = true;
            auto render = pl_render_default_params;
            render.hooks = &hook;
            render.num_hooks = 1;
            // Upscaling shaders only run when the picture is enlarged enough
            // (their //!WHEN lines), so render at twice and three times the size.
            for (const double scale : {2.0, 3.2})
            {
                pl_tex_params tp = {};
                tp.w = std::min(8192, int(std::lround(w * scale)));
                tp.h = std::min(8192, int(std::lround(h * scale)));
                tp.format = pl_find_named_fmt(gpu, "rgba8");
                tp.renderable = true;
                pl_tex target = pl_tex_create(gpu, &tp);
                pl_frame output = {};
                output.num_planes = 1;
                output.planes[0].texture = target;
                output.planes[0].components = 4;
                for (int i = 0; i < 4; i++)
                    output.planes[0].component_mapping[i] = i;
                output.repr = pl_color_repr_rgb;
                output.color = pl_color_space_srgb;
                const bool drawn = target && pl_render_image(renderer, &input, &output, &render);
                pl_gpu_finish(gpu);
                if (!drawn)
                    Trace("Shader warm-up render %dx%d failed", tp.w, tp.h);
                ok = drawn && ok;
                pl_tex_destroy(gpu, &target);
            }
        }
        else
            Trace("Shader warm-up: setup failed (hook %d, renderer %d, planes %d/%d)",
                  hook != nullptr,
                  renderer != nullptr,
                  planes[0] != nullptr,
                  planes[1] != nullptr);
        pl_renderer_destroy(&renderer);
        pl_mpv_user_shader_destroy(&hook);
        for (auto& tex : planes)
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
