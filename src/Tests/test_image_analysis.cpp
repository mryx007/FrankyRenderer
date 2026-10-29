#define NOMINMAX
#define PLACEBO_TEST_IMAGE_ANALYSIS
#include "Renderer/ImageAnalysis.h"
#include <libplacebo/renderer.h>
#include <cstdio>
#include <numeric>

static void Check(bool ok, const char* message)
{
    if (!ok)
    {
        printf("FAIL %s\n", message);
        exit(1);
    }
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    using namespace ImageAnalysisPlot;
    Sample source, mapped;
    source.hdr = mapped.hdr = true;
    source.pixels.resize(SampleWidth * SampleHeight);
    mapped.pixels.resize(source.pixels.size());
    for (int y = 0; y < SampleHeight; ++y)
        for (int x = 0; x < SampleWidth; ++x)
        {
            const float v = std::expm1(float(x) / (SampleWidth - 1) * std::log1p(4000.f));
            const size_t i = y * SampleWidth + x;
            source.pixels[i] = {v, v * .7f, v * .3f, v};
            mapped.pixels[i] = {v / 20, v / 25, v / 30, v / 20};
        }
    auto pre = Measure(source, 10000, 286, 142), post = Measure(mapped, 10000, 286, 142);
    Check(std::accumulate(pre.histogram.begin(), pre.histogram.end(), uint64_t(0)) == source.pixels.size(),
          "histogram counts every valid sample");
    Check(std::abs(pre.peak - 4000) < 1 && std::abs(post.peak - 200) < 1, "before/after peak measurements");
    Check(Axis(0, 10000) == 0 && Axis(10000, 10000) == 1 && Axis(100, 10000) < Axis(1000, 10000),
          "shared monotonic nits axis");
    Check(FalseColor(0) != FalseColor(203) && FalseColor(203) != FalseColor(4000), "distinct false-color ranges");
    Check(FalseColor(250) != FalseColor(800), "false colors retain detail inside former coarse brightness bands");
    const DWORD handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    auto plot = Draw(source, mapped);
    Check(plot.rgba.size() == Width * Height * 4, "all four scopes rendered");
    Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles, "plot has no GDI resource leak");
    FILE* file = nullptr;
    if (!fopen_s(&file, "src/build/image-analysis-preview.rgba", "wb") && file)
    {
        fwrite(plot.rgba.data(), 1, plot.rgba.size(), file);
        fclose(file);
    }
    puts("PASS four plot models, nits axis, histogram conservation, false colors, before/after peaks and GDI cleanup");

    auto d3d = pl_d3d11_create(nullptr, &pl_d3d11_default_params);
    Check(d3d != nullptr, "GPU device");
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    d3d->device->GetImmediateContext(&context);
    auto renderer = pl_renderer_create(nullptr, d3d->gpu);
    Check(renderer != nullptr, "GPU renderer");
    pl_tex_params texture = {};
    texture.w = SampleWidth;
    texture.h = SampleHeight;
    texture.format = pl_find_named_fmt(d3d->gpu, "rgba32f");
    texture.sampleable = texture.renderable = texture.host_writable = texture.host_readable = true;
    auto input = pl_tex_create(d3d->gpu, &texture), output = pl_tex_create(d3d->gpu, &texture);
    Check(input && output, "float GPU textures");
    for (bool hdr : {false, true})
    {
        std::vector<Pixel> pixels(SampleWidth * SampleHeight);
        pl_color_space color = hdr ? pl_color_space_hdr10 : pl_color_space_srgb;
        color.hdr.max_luma = hdr ? 4000.f : 203.f;
        for (int y = 0; y < SampleHeight; ++y)
            for (int x = 0; x < SampleWidth; ++x)
            {
                const float nits = (hdr ? 4000.f : 203.f) * x / (SampleWidth - 1);
                float rgb[3] = {nits / 203, nits / 203, nits / 203};
                pl_color_delinearize(&color, rgb);
                pixels[y * SampleWidth + x] = {rgb[0], rgb[1], rgb[2], 1};
            }
        pl_tex_transfer_params upload = {};
        upload.tex = input;
        upload.ptr = pixels.data();
        Check(pl_tex_upload(d3d->gpu, &upload), "upload known luminance ramp");
        pl_frame src = {}, dst = {};
        src.num_planes = dst.num_planes = 1;
        src.planes[0].texture = input;
        dst.planes[0].texture = output;
        src.planes[0].components = dst.planes[0].components = 4;
        for (int i = 0; i < 4; ++i)
            src.planes[0].component_mapping[i] = dst.planes[0].component_mapping[i] = i;
        src.color = dst.color = color;
        src.repr.sys = dst.repr.sys = PL_COLOR_SYSTEM_RGB;
        src.repr.levels = dst.repr.levels = PL_COLOR_LEVELS_FULL;
        src.crop = dst.crop = {0, 0, float(SampleWidth), float(SampleHeight)};
        ImageAnalysis analysis;
        pl_render_params params = pl_render_fast_params;
        const pl_hook* hooks[] = {analysis.Observer()};
        params.hooks = hooks;
        params.num_hooks = 1;
        for (int i = 0; i < 80; ++i)
        {
            analysis.Begin(d3d, context.Get(), nullptr, 1);
            Check(pl_render_image(renderer, &src, &dst, &params), "observer preserves GPU rendering");
            pl_gpu_flush(d3d->gpu);
            if (!analysis.SampleForTest(0).pixels.empty() && !analysis.SampleForTest(1).pixels.empty())
                break;
            Sleep(5);
        }
        const auto a = analysis.SampleForTest(0), b = analysis.SampleForTest(1);
        Check(!a.pixels.empty() && !b.pixels.empty(), "nonblocking readback receives both stages");
        const auto am = Measure(a, hdr ? 10000 : 203, 286, 142), bm = Measure(b, hdr ? 10000 : 203, 286, 142);
        printf("GPU %s: before %.2f / after %.2f nominal nits\n", hdr ? "PQ" : "SDR", am.peak, bm.peak);
        Check(std::abs(am.peak - (hdr ? 4000 : 203)) < (hdr ? 15 : 2), "GPU EOTF gives known source nits");
        Check(std::abs(am.peak - bm.peak) < (hdr ? 15 : 2), "neutral pipeline preserves measured luminance");
        const auto* ramp = analysis.Ramp(color);
        Check(ramp != nullptr, "gray ramp shader parses");
        hooks[0] = ramp;
        Check(pl_render_image(renderer, &src, &dst, &params), "gray ramp passes through output rendering");
        std::vector<Pixel> rendered(pixels.size());
        pl_tex_transfer_params download = {};
        download.tex = output;
        download.ptr = rendered.data();
        Check(pl_tex_download(d3d->gpu, &download), "read ramp pixels");
        const auto left = rendered[(SampleHeight - 2) * SampleWidth + 12][0];
        const auto right = rendered[(SampleHeight - 2) * SampleWidth + SampleWidth - 12][0];
        Check(right > left + .1f, "gray test strip contains a visible gradient");
        const auto bright = rendered[(SampleHeight - 2) * SampleWidth + SampleWidth - 2];
        float rgb[3] = {bright[0], bright[1], bright[2]};
        pl_color_linearize(&color, rgb);
        Check(std::abs(rgb[0] * 203 - 203) < 3, "HDR gray ramp is capped at diffuse white");
        auto collect = [&]()
        {
            for (int i = 0; i < 80; ++i)
            {
                analysis.Begin(d3d, context.Get(), nullptr, 2);
                Check(pl_render_image(renderer, &src, &dst, &params), "processed analysis rendering");
                pl_gpu_flush(d3d->gpu);
                if (!analysis.SampleForTest(0).pixels.empty() && !analysis.SampleForTest(1).pixels.empty())
                    break;
                Sleep(5);
            }
            const auto a = Measure(analysis.SampleForTest(0), 10000, 286, 142);
            const auto b = Measure(analysis.SampleForTest(1), 10000, 286, 142);
            return std::pair(a.peak, b.peak);
        };
        analysis.Reset();
        hooks[0] = analysis.Observer();
        analysis.Begin(d3d, context.Get(), nullptr, 2);
        if (hdr)
        {
            dst.color = pl_color_space_srgb;
            dst.color.hdr.max_luma = 203;
            analysis.Ramp(dst.color, dst.repr);
            const auto [before, after] = collect();
            printf("GPU tone map: before %.2f / after %.2f nits\n", before, after);
            Check(before > 3900 && after > 100 && after < 205, "analysis shows HDR to SDR peak compression");
            dst.color = color;
        }
        else
        {
            const float data[] = {0, 0, 0, .5f, .5f, .5f};
            pl_custom_lut lut = {};
            lut.signature = 0x53434f50454c5554ULL;
            lut.size[0] = 2;
            lut.data = data;
            dst.lut = &lut;
            dst.lut_type = PL_LUT_NATIVE;
            analysis.Ramp(dst.color, dst.repr);
            const auto [before, after] = collect();
            printf("GPU native LUT: before %.2f / after %.2f nits\n", before, after);
            Check(before > 200 && after > 35 && after < 55, "output sample includes the native calibration LUT");
            dst.lut = nullptr;
        }
        analysis.Reset();
        dst.repr.levels = PL_COLOR_LEVELS_LIMITED;
        dst.repr.bits.sample_depth = dst.repr.bits.color_depth = 8;
        analysis.Begin(d3d, context.Get(), nullptr, 2);
        analysis.Ramp(dst.color, dst.repr);
        const auto [limitedBefore, limitedAfter] = collect();
        Check(std::abs(limitedBefore - limitedAfter) < (hdr ? 20 : 3),
              "limited-range output is decoded before analysis");
        hooks[0] = analysis.Ramp(dst.color, dst.repr);
        Check(pl_render_image(renderer, &src, &dst, &params), "limited-range gray ramp");
        Check(pl_tex_download(d3d->gpu, &download), "limited gray ramp pixels");
        const auto black = rendered[(SampleHeight - 2) * SampleWidth + 2][0];
        Check(black > .06f && black < .065f, "gray strip respects limited-range black");
        params.dither_params = &pl_dither_default_params;
        params.force_dither = true;
        Check(pl_render_image(renderer, &src, &dst, &params), "gray ramp with actual 8-bit dithering");
        Check(pl_tex_download(d3d->gpu, &download), "dithered gray strip pixels");
        float low = 1, high = 0;
        for (int y = SampleHeight - 12; y < SampleHeight - 1; ++y)
        {
            const float value = rendered[y * SampleWidth + SampleWidth / 3][0];
            low = std::min(low, value);
            high = std::max(high, value);
        }
        Check(high > low + .001f, "output dither acts on the gray ramp rather than bypassing it");
        analysis.Reset();
        Check(analysis.SampleForTest(0).pixels.empty(), "reset discards stale samples");
        pl_renderer_flush_cache(renderer);
    }
    pl_tex_destroy(d3d->gpu, &input);
    pl_tex_destroy(d3d->gpu, &output);
    pl_renderer_destroy(&renderer);
    pl_d3d11_destroy(&d3d);
    puts("PASS real SDR/PQ GPU hooks, nominal nits, nonblocking before/after readback, gray ramp and reset");
}
