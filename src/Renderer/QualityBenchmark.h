#pragma once
// Measures which quality upgrades this display's GPU can afford. Test frames
// are rendered off screen with the playback option code (RenderOptions.h) at
// the display's resolution, together with the profile's active shaders,
// Adaptive Sharpen and Smooth Motion frame mixing. An upgrade is kept while the
// slower of two typical sources (1080p SDR, 4K HDR10) stays within three
// quarters of a refresh interval; the rest is left for subtitles, presentation
// and load spikes.
#include "Renderer/RenderOptions.h"
#include "Renderer/ShaderHooks.h"
#include "Settings/QualityPresets.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/utils/upload.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

struct BenchmarkTarget
{
    LUID adapter = {};
    int width = 1920, height = 1080;
    double refresh = 60;
};

struct BenchmarkResult
{
    bool ok = false;
    uint32_t upgrades = 0;
    double milliseconds = 0, budget = 0;
    int hooks = 0; // shaders measured with the upgrades
};

// The display mode and GPU of a monitor, in physical pixels.
inline bool FindBenchmarkTarget(HMONITOR monitor, BenchmarkTarget& out)
{
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfoW(monitor, &info))
        return false;
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || !mode.dmPelsWidth || !mode.dmPelsHeight)
        return false;
    out.width = int(mode.dmPelsWidth);
    out.height = int(mode.dmPelsHeight);
    out.refresh = mode.dmDisplayFrequency > 1 ? double(mode.dmDisplayFrequency) : 60.;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a)
    {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o)
        {
            DXGI_OUTPUT_DESC desc = {};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor)
            {
                DXGI_ADAPTER_DESC1 adapterDesc = {};
                adapter->GetDesc1(&adapterDesc);
                out.adapter = adapterDesc.AdapterLuid;
                return true;
            }
        }
    }
    return false;
}

class QualityBenchmark
{
    struct Source
    {
        pl_tex planes[2] = {};
        pl_frame frame = {};
        pl_tex target = nullptr;
        pl_frame output = {};
    };

    pl_d3d11 d3d = nullptr;
    pl_renderer renderer = nullptr;
    pl_options options = nullptr;
    Source sources[2];
    std::vector<const pl_hook*> hooks;
    bool mixing = false;
    uint64_t signature = 0;

    // One presented picture. With Smooth Motion every call brings one new
    // video frame into the mix, the most work a refresh can need.
    bool RenderOnce(Source& s)
    {
        pl_render_params params = options->params;
        params.hooks = hooks.data();
        params.num_hooks = int(hooks.size());
        if (!mixing)
            return pl_render_image(renderer, &s.frame, &s.output, &params);
        const pl_frame* frames[] = {&s.frame, &s.frame};
        const uint64_t signatures[] = {signature, signature + 1};
        const double timestamps[] = {-.5, .5};
        ++signature;
        pl_frame_mix mix = {};
        mix.num_frames = 2;
        mix.frames = frames;
        mix.signatures = signatures;
        mix.timestamps = timestamps;
        mix.vsync_duration = 1;
        return pl_render_image_mix(renderer, &mix, &s.output, &params);
    }

    // NV12 / P010-like 4:2:0 frame with picture detail, so scalers and
    // debanding do representative work.
    bool MakeSource(Source& s, int w, int h, bool hdr, const BenchmarkTarget& display)
    {
        const pl_gpu gpu = d3d->gpu;
        const int depth = hdr ? 16 : 8;
        std::vector<uint16_t> wide;
        std::vector<uint8_t> narrow;
        for (int p = 0; p < 2; ++p)
        {
            const int pw = p ? w / 2 : w, ph = p ? h / 2 : h, components = p ? 2 : 1;
            const size_t count = size_t(pw) * ph * components;
            hdr ? wide.assign(count, 0) : narrow.assign(count, 0);
            for (int y = 0; y < ph; ++y)
                for (int x = 0; x < pw; ++x)
                    for (int c = 0; c < components; ++c)
                    {
                        const double v =
                            p ? .5 + .1 * std::sin(x * .02 + c) : .45 + .35 * std::sin(x * .11) * std::cos(y * .07);
                        const size_t i = (size_t(y) * pw + x) * components + c;
                        hdr ? void(wide[i] = uint16_t(v * 65535)) : void(narrow[i] = uint8_t(v * 255));
                    }
            pl_plane_data data = {};
            data.type = PL_FMT_UNORM;
            data.width = pw;
            data.height = ph;
            data.pixel_stride = size_t(components) * depth / 8;
            for (int c = 0; c < components; ++c)
            {
                data.component_size[c] = depth;
                data.component_map[c] = p ? c + 1 : 0;
            }
            data.pixels = hdr ? static_cast<const void*>(wide.data()) : narrow.data();
            if (!pl_upload_plane(gpu, &s.frame.planes[p], &s.planes[p], &data))
                return false;
        }
        s.frame.num_planes = 2;
        s.frame.repr = pl_color_repr_sdtv;
        s.frame.repr.sys = hdr ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
        s.frame.color = hdr ? pl_color_space_hdr10 : pl_color_space_bt709;
        if (hdr)
        {
            s.frame.color.hdr.max_luma = 1000;
            s.frame.color.hdr.max_cll = 1000;
        }
        s.frame.crop = {0, 0, float(w), float(h)};
        pl_frame_set_chroma_location(&s.frame, PL_CHROMA_LEFT);

        // The picture is fitted into the display like playback in fullscreen.
        const bool hdrOutput = hdr && displayHdr;
        // HDR output uses rgb10a2 like the swapchain (its 2-bit alpha rules out
        // a plain 4 x 10-bit search); half float where that is missing.
        pl_fmt format = hdrOutput ? pl_find_named_fmt(gpu, "rgb10a2")
                                  : pl_find_fmt(gpu, PL_FMT_UNORM, 4, 8, 8, PL_FMT_CAP_RENDERABLE);
        if (hdrOutput && (!format || !(format->caps & PL_FMT_CAP_RENDERABLE)))
            format = pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 16, 16, PL_FMT_CAP_RENDERABLE);
        if (!format)
            return false;
        pl_tex_params params = {};
        params.w = display.width;
        params.h = display.height;
        params.format = format;
        params.renderable = true;
        params.blit_dst = true;
        s.target = pl_tex_create(gpu, &params);
        if (!s.target)
            return false;
        s.output.num_planes = 1;
        s.output.planes[0].texture = s.target;
        s.output.planes[0].components = 4;
        for (int c = 0; c < 4; ++c)
            s.output.planes[0].component_mapping[c] = c;
        s.output.repr = pl_color_repr_rgb;
        s.output.color = hdrOutput ? pl_color_space_hdr10 : pl_color_space_srgb;
        const double scale = std::min(double(display.width) / w, double(display.height) / h);
        const float fw = float(w * scale), fh = float(h * scale);
        s.output.crop = {
            (display.width - fw) / 2, (display.height - fh) / 2, (display.width + fw) / 2, (display.height + fh) / 2};
        return true;
    }

    // Average milliseconds per frame; the fastest of five batches filters out
    // other GPU work such as a playing video.
    double Measure(Source& s, double limit, const std::atomic<bool>* cancel)
    {
        const pl_gpu gpu = d3d->gpu;
        for (int i = 0; i < 2; ++i)
            if (!RenderOnce(s))
                return -1;
        pl_gpu_finish(gpu);
        double best = 1e9;
        for (int batch = 0; batch < 5 && !(cancel && *cancel); ++batch)
        {
            constexpr int frames = 8;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < frames; ++i)
                if (!RenderOnce(s))
                    return -1;
            pl_gpu_finish(gpu);
            best = std::min(
                best,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames);
            // Clearly too slow or clearly fast enough: more batches cannot
            // change the decision. Close calls get all five.
            if (best > limit * 2 || (batch >= 1 && best < limit / 2))
                break;
        }
        return best;
    }

  public:
    bool displayHdr = false;

    ~QualityBenchmark()
    {
        for (auto& s : sources)
            if (d3d)
            {
                for (auto& tex : s.planes)
                    pl_tex_destroy(d3d->gpu, &tex);
                pl_tex_destroy(d3d->gpu, &s.target);
            }
        for (auto& hook : hooks)
            pl_mpv_user_shader_destroy(&hook);
        pl_options_free(&options);
        pl_renderer_destroy(&renderer);
        pl_d3d11_destroy(&d3d);
    }

    // settings: the profile being measured; its other options (tone mapping,
    // gamut mapping, ...) stay part of every measurement.
    BenchmarkResult Run(const PlaceboSettings& settings,
                        const BenchmarkTarget& display,
                        std::atomic<float>* progress = nullptr,
                        const std::atomic<bool>* cancel = nullptr)
    {
        BenchmarkResult result;
        result.budget = 750. / std::max(1., display.refresh);
        auto params = pl_d3d11_default_params;
        params.adapter_luid = display.adapter;
        d3d = pl_d3d11_create(nullptr, &params);
        if (!d3d)
            return result;
        renderer = pl_renderer_create(nullptr, d3d->gpu);
        options = pl_options_alloc(nullptr);
        displayHdr = settings.hdrMode != 2;
        if (!renderer || !options || !MakeSource(sources[0], 1920, 1080, false, display) ||
            !MakeSource(sources[1], 3840, 2160, true, display))
            return result;
        // The same hooks as playback, in its order; one that fails there is
        // skipped there too.
        if (settings.adaptiveSharpen)
            if (const pl_hook* sharpen = ParseSharpenShader(d3d->gpu, settings.sharpenStrength))
                hooks.push_back(sharpen);
        for (int id : ShaderChainOrder(settings.userShaders, settings.shaderAutoOrder != FALSE))
            if (settings.userShaders.shaders[id].enabled)
                if (const pl_hook* hook = ParseUserShader(d3d->gpu, id, settings.userShaders.shaders[id]))
                    hooks.push_back(hook);
        mixing = settings.smoothMotion != FALSE;
        result.hooks = int(hooks.size());
        // An idle GPU runs at low clocks at first; keep it busy briefly so the
        // first candidates are not measured slower than the later ones.
        if (FAILED(BuildRenderOptions(options, BackendSettings(settings))))
            return result;
        const auto warm = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - warm < std::chrono::milliseconds(600) && !(cancel && *cancel))
        {
            for (auto& s : sources)
                if (!RenderOnce(s))
                    return result;
            pl_gpu_finish(d3d->gpu);
        }

        auto cost = [&](uint32_t upgrades)
        {
            PlaceboSettings candidate = settings;
            ApplyMeasuredQuality(candidate, upgrades);
            if (FAILED(BuildRenderOptions(options, BackendSettings(candidate))))
                return -1.;
            double slowest = 0;
            for (auto& s : sources)
            {
                const double ms = Measure(s, result.budget, cancel);
                if (ms < 0)
                    return -1.;
                slowest = std::max(slowest, ms);
            }
            return slowest;
        };
        uint32_t accepted = 0;
        result.milliseconds = cost(accepted);
        if (result.milliseconds < 0)
            return result;
        int step = 0;
        for (const auto upgrade : MeasuredUpgrades)
        {
            if (progress)
                *progress = float(++step) / float(std::size(MeasuredUpgrades));
            if (cancel && *cancel)
                return result;
            // Each scaler step builds on the cheaper one; HDR detail needs peak detection.
            if ((upgrade == MeasureEwa && !(accepted & MeasureLanczos)) ||
                (upgrade == MeasureEwaSharp && !(accepted & MeasureEwa)) ||
                (upgrade == MeasureHdrDetail && !(accepted & MeasurePeakDetection)))
                continue;
            const double ms = cost(accepted | upgrade);
            if (ms < 0)
                return result;
            if (ms <= result.budget)
            {
                accepted |= upgrade;
                result.milliseconds = ms;
            }
        }
        result.upgrades = accepted;
        result.ok = true;
        return result;
    }
};

// Short technical summary of the chosen upgrades.
inline std::wstring DescribeMeasuredQuality(uint32_t upgrades)
{
    const auto preset = MeasuredPreset(upgrades);
    std::wstring text;
    for (const char* c = preset.upscaler; *c; ++c)
        text += wchar_t(*c);
    if (!strcmp(preset.upscaler, "none"))
        text = L"bilinear";
    if (upgrades & MeasureDeband)
        text += L", deband";
    if (upgrades & MeasureDither)
        text += L", dither";
    if (upgrades & MeasurePeakDetection)
        text += upgrades & MeasureHdrDetail ? L", HDR peak detection (detailed)" : L", HDR peak detection";
    return text;
}
