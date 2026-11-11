// Every catalogue shader must load from the shipped folder and parse; the
// RAVU Zoom variants must also render a 2x upscale of a YCbCr frame.
#define NOMINMAX
#include "Renderer/NguScaler.h"
#include "Renderer/UserShaderSource.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/utils/upload.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Require(d3d != nullptr, "D3D11 GPU");
        const pl_gpu gpu = d3d->gpu;

        NguScalerRequest lumaRequest;
        lumaRequest.sourceWidth = 1920;
        lumaRequest.sourceHeight = 1080;
        lumaRequest.stage = NguScalerStage::VeryHighLuma1080Core;
        Require(ProbeNguScaler(lumaRequest) == NguScalerSupport::Ready, "1080p VeryHigh luma capability");

        NguScalerRequest chromaRequest;
        chromaRequest.sourceWidth = 640;
        chromaRequest.sourceHeight = 360;
        chromaRequest.stage = NguScalerStage::VeryHighChromaReference;
        Require(ProbeNguScaler(chromaRequest) == NguScalerSupport::Ready, "reference VeryHigh chroma capability");

        auto experimental = lumaRequest;
        experimental.quality = NguQuality::High;
        Require(ProbeNguScaler(experimental) == NguScalerSupport::ExperimentalUnimplemented,
                "High NGU remains experimental");
        auto wrongGeometry = lumaRequest;
        wrongGeometry.sourceWidth = 1280;
        wrongGeometry.sourceHeight = 720;
        Require(ProbeNguScaler(wrongGeometry) == NguScalerSupport::UnsupportedGeometry,
                "unobserved NGU geometry is rejected");

        NguScaler ngu;
        Require(ngu.PrepareValidation(gpu, lumaRequest), "prepare 1080p NGU luma validation stage");
        const pl_hook* lumaHook = ngu.Hook();
        Require(lumaHook && ngu.Stage() == lumaRequest.stage, "NGU luma hook ownership");
        Require(ngu.PrepareValidation(gpu, lumaRequest) && ngu.Hook() == lumaHook,
                "identical NGU request reuses parsed hook");
        Require(ngu.PrepareValidation(gpu, chromaRequest), "prepare reference NGU chroma validation stage");
        Require(ngu.Hook() && ngu.Stage() == chromaRequest.stage, "NGU stage replacement");
        Require(!ngu.PrepareValidation(gpu, wrongGeometry), "unsupported NGU request fails closed");
        Require(ngu.LastError()[0] != '\0', "failed NGU request reports its boundary");
        ngu.Reset();
        Require(!ngu.Hook() && ngu.Stage() == NguScalerStage::None, "NGU reset releases hook state");

        const pl_hook* hooks[ShaderCount] = {};
        for (int id = 0; id < ShaderCount; ++id)
        {
            const auto source = LoadUserShaderSource(id);
            Require(!source.empty(), ShaderCatalog[id].file);
            hooks[id] = pl_mpv_user_shader_parse(gpu, source.data(), source.size());
            Require(hooks[id] != nullptr, ShaderCatalog[id].file);
        }

        constexpr int w = 960, h = 540;
        std::vector<uint8_t> plane(w * h);
        pl_tex planes[3] = {};
        pl_plane frame_planes[3] = {};
        for (int p = 0; p < 3; ++p)
        {
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    plane[y * w + x] = uint8_t(p ? 128 + 20 * std::sin(x * .05f)
                                                 : 16 + 219 * (.5f + .5f * std::sin(x * .3f + y * .2f)));
            pl_plane_data data = {};
            data.type = PL_FMT_UNORM;
            data.width = w;
            data.height = h;
            data.pixel_stride = 1;
            data.component_size[0] = 8;
            data.component_map[0] = p;
            data.pixels = plane.data();
            Require(pl_upload_plane(gpu, &frame_planes[p], &planes[p], &data), "upload plane");
        }
        pl_frame image = {};
        image.num_planes = 3;
        for (int p = 0; p < 3; ++p)
            image.planes[p] = frame_planes[p];
        image.repr = pl_color_repr_sdtv;
        image.repr.sys = PL_COLOR_SYSTEM_BT_709;
        image.color = pl_color_space_bt709;

        const pl_fmt format = pl_find_fmt(gpu, PL_FMT_UNORM, 4, 8, 8, PL_FMT_CAP_RENDERABLE);
        Require(format != nullptr, "target format");
        pl_tex_params targetParams = {};
        targetParams.w = w * 2;
        targetParams.h = h * 2;
        targetParams.format = format;
        targetParams.renderable = true;
        targetParams.host_readable = true;
        pl_tex target = pl_tex_create(gpu, &targetParams);
        Require(target != nullptr, "target texture");
        pl_frame out = {};
        out.num_planes = 1;
        out.planes[0].texture = target;
        out.planes[0].components = 4;
        for (int c = 0; c < 4; ++c)
            out.planes[0].component_mapping[c] = c;
        out.repr = pl_color_repr_rgb;
        out.color = pl_color_space_bt709;

        pl_renderer renderer = pl_renderer_create(nullptr, gpu);
        auto download = [&]
        {
            std::vector<uint8_t> pixels(size_t(targetParams.w) * targetParams.h * 4);
            pl_tex_transfer_params read = {};
            read.tex = target;
            read.ptr = pixels.data();
            Require(pl_tex_download(gpu, &read), "download");
            return pixels;
        };
        // Without a hook, for proof that each shader really changes the picture.
        Require(pl_render_image(renderer, &image, &out, &pl_render_default_params), "plain upscale");
        const auto plain = download();
        std::vector<std::vector<uint8_t>> results;
        for (const char* file : {"ravu_zoom_ar_r2.glsl", "ravu_zoom_r3.glsl", "ravu_zoom_ar_r3.glsl"})
        {
            int id = 0;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, file))
                ++id;
            Require(id < ShaderCount, "RAVU catalogue entry");
            auto render = pl_render_default_params;
            render.hooks = &hooks[id];
            render.num_hooks = 1;
            Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const auto start = std::chrono::steady_clock::now();
            constexpr int frames = 30;
            for (int i = 0; i < frames; ++i)
                Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
            const auto pixels = download();
            double sum = 0;
            size_t changed = 0;
            for (size_t i = 0; i < pixels.size(); i += 4)
            {
                sum += pixels[i + 1];
                changed += pixels[i + 1] != plain[i + 1];
            }
            Require(sum / (pixels.size() / 4) > 20, "upscaled picture is not empty");
            Require(changed > pixels.size() / 400, "the shader changes the upscaled picture");
            for (const auto& other : results)
                Require(other != pixels, "each RAVU variant gives its own picture");
            results.push_back(pixels);
            printf("%-22s 960x540 -> 1920x1080: %.2f ms per frame, %.1f%% of pixels differ from plain\n",
                   file,
                   ms,
                   100.0 * changed / (pixels.size() / 4));
        }
        {
            const char* file = "ngu_chroma_cubici2.glsl";
            int id = 0;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, file))
                ++id;
            Require(id < ShaderCount, "NGU catalogue entry");
            auto render = pl_render_default_params;
            render.hooks = &hooks[id];
            render.num_hooks = 1;
            Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const auto start = std::chrono::steady_clock::now();
            constexpr int frames = 10;
            for (int frame = 0; frame < frames; ++frame)
                Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
            printf("PASS %s: 4 MRT-split passes, %.2f ms per frame\n", file, ms);
        }
        {
            const char* file = "ngu_sharp_veryhigh_chroma_stage1.glsl";
            int id = 0;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, file))
                ++id;
            Require(id < ShaderCount, "NGU stage-1 catalogue entry");
            auto render = pl_render_default_params;
            render.hooks = &hooks[id];
            render.num_hooks = 1;
            Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const auto start = std::chrono::steady_clock::now();
            constexpr int frames = 10;
            for (int frame = 0; frame < frames; ++frame)
                Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
            printf("PASS %s: 33 passes, %.2f ms per frame\n", file, ms);
        }
        {
            const char* file = "ngu_sharp_veryhigh_chroma_dual_core.glsl";
            int id = 0;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, file))
                ++id;
            Require(id < ShaderCount, "NGU dual-core catalogue entry");
            auto render = pl_render_default_params;
            render.hooks = &hooks[id];
            render.num_hooks = 1;
            Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const auto start = std::chrono::steady_clock::now();
            constexpr int frames = 10;
            for (int frame = 0; frame < frames; ++frame)
                Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
            printf("PASS %s: 66 passes, %.2f ms per frame\n", file, ms);
        }
        {
            const char* file = "ngu_sharp_veryhigh_chroma_full.glsl";
            int id = 0;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, file))
                ++id;
            Require(id < ShaderCount, "NGU full chroma catalogue entry");
            auto render = pl_render_default_params;
            render.hooks = &hooks[id];
            render.num_hooks = 1;
            Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const auto start = std::chrono::steady_clock::now();
            constexpr int frames = 10;
            for (int frame = 0; frame < frames; ++frame)
                Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
            printf("PASS %s: 73 passes, %.2f ms per frame\n", file, ms);
        }
        {
            const char* file = "ngu_sharp_veryhigh_luma_1080_core.glsl";
            int id = 0;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, file))
                ++id;
            Require(id < ShaderCount, "NGU luma core catalogue entry");
            auto render = pl_render_default_params;
            render.hooks = &hooks[id];
            render.num_hooks = 1;
            Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const auto start = std::chrono::steady_clock::now();
            constexpr int frames = 10;
            for (int frame = 0; frame < frames; ++frame)
                Require(pl_render_image(renderer, &image, &out, &render), file);
            pl_gpu_finish(gpu);
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
            printf("PASS %s: 19 passes, %.2f ms per frame\n", file, ms);
        }
        pl_renderer_destroy(&renderer);
        pl_tex_destroy(gpu, &target);
        for (auto& tex : planes)
            pl_tex_destroy(gpu, &tex);
        for (auto& hook : hooks)
            pl_mpv_user_shader_destroy(&hook);
        pl_d3d11_destroy(&d3d);
        printf("PASS user shaders: %d catalogue shaders load and parse; NGU lifecycle is fail-closed; "
               "RAVU Zoom R2/R3 variants render\n",
               ShaderCount);
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("FAIL %s\n", e.what());
        return 1;
    }
}
