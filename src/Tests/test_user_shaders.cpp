// Every catalogue shader must load from the shipped folder and parse; the
// RAVU Zoom variants must also render a 2x upscale of a YCbCr frame.
#define NOMINMAX
#include "Renderer/ShaderHooks.h"
#include "Renderer/ShaderWarmup.h"
#include <dxgi.h>
#include <wrl/client.h>
#include "Settings/CustomShaders.h"
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

        const pl_hook* hooks[ShaderBuiltinCount] = {};
        for (int id = 0; id < ShaderBuiltinCount; ++id)
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
            while (id < ShaderBuiltinCount && strcmp(ShaderCatalog[id].file, file))
                ++id;
            Require(id < ShaderBuiltinCount, "RAVU catalogue entry");
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
            // A file from the custom folder loads like a catalogue shader, and
            // a saved value is limited to what the file allows now.
            wchar_t root[80];
            swprintf_s(root, L"Software\\PlaceboRendererTest_shader_%lu", GetCurrentProcessId());
            SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root);
            const auto folder = CustomShaders::Folder();
            std::error_code error;
            std::filesystem::remove_all(folder, error);
            std::filesystem::create_directories(folder);
            std::ofstream(folder / L"tint.glsl", std::ios::binary)
                << "//!PARAM amount\n//!TYPE float\n//!MINIMUM 0.0\n//!MAXIMUM 2.0\n0.5\n\n"
                   "//!HOOK MAIN\n//!BIND HOOKED\nvec4 hook() { return HOOKED_tex(HOOKED_pos) * amount; }\n";
            CustomShaders::Refresh();
            int id = ShaderBuiltinCount;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, "tint.glsl"))
                ++id;
            Require(id < ShaderCount, "custom shader listed");
            ShaderConfig config;
            config.enabled = 1;
            config.overrides = 1;
            config.values[0] = 9;
            const pl_hook* custom = ParseUserShader(gpu, id, config);
            Require(custom != nullptr, "custom shader parses from its folder");
            Require(custom->num_parameters == 1 && custom->parameters[0].data->f == 2.f,
                    "saved value reaches the shader within the file's limits");
            pl_mpv_user_shader_destroy(&custom);
            // A variant of SSimDownscaler is recognised by its name, and its
            // ANTIRING parameter follows the global anti-ringing strength.
            std::ofstream(folder / L"SSimDownscaler AntiRinging.glsl", std::ios::binary)
                << "//!PARAM ANTIRING\n//!TYPE CONSTANT float\n//!MINIMUM 0.0\n//!MAXIMUM 1.0\n1.0\n\n"
                   "//!HOOK MAIN\n//!BIND HOOKED\nvec4 hook() { return HOOKED_tex(HOOKED_pos) * ANTIRING; }\n";
            CustomShaders::Refresh();
            int variant = ShaderBuiltinCount;
            while (variant < ShaderCount && strcmp(ShaderCatalog[variant].file, "SSimDownscaler AntiRinging.glsl"))
                ++variant;
            Require(variant < ShaderCount && IsSsimDownscaler(ShaderCatalog[variant].file) &&
                        IsSsimDownscaler("SSimDownscaler_RT.glsl") && !IsSsimDownscaler("DPID_RT.glsl"),
                    "SSimDownscaler variants are recognised by name");
            const pl_hook* following = ParseUserShader(gpu, variant, {}, .25f);
            Require(following && following->num_parameters == 1 && following->parameters[0].data->f == .25f,
                    "ANTIRING follows the global anti-ringing strength");
            pl_mpv_user_shader_destroy(&following);
            const pl_hook* unchanged = ParseUserShader(gpu, variant, {});
            Require(unchanged && unchanged->parameters[0].data->f == 1.f, "without a strength the file's value stays");
            pl_mpv_user_shader_destroy(&unchanged);
            id = ShaderBuiltinCount;
            while (id < ShaderCount && strcmp(ShaderCatalog[id].file, "tint.glsl"))
                ++id;
            {
                // A worker device compiles the shader; its cache reaches this device.
                ShaderWarmupJob job;
                job.source = LoadUserShaderSource(id);
                Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
                Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
                DXGI_ADAPTER_DESC adapterDesc = {};
                Require(SUCCEEDED(d3d->device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
                            SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&adapterDesc)),
                        "adapter of the playing device");
                job.adapter = adapterDesc.AdapterLuid;
                job.width = 320;
                job.height = 180;
                Require(WarmUpShader(job) && !job.cache.empty(), "a worker device compiles the shader");
                auto cacheParams = pl_cache_default_params;
                pl_cache cache = pl_cache_create(&cacheParams);
                Require(pl_cache_load(cache, job.cache.data(), job.cache.size()) > 0, "its cache can be loaded");
                pl_cache_destroy(&cache);
            }
            // A chain from a package runs as one shader.
            std::filesystem::create_directories(folder / L"shaders");
            for (const wchar_t* part : {L"one.glsl", L"two.glsl"})
                std::ofstream(folder / L"shaders" / part, std::ios::binary)
                    << "//!HOOK MAIN\n//!BIND HOOKED\nvec4 hook() { return HOOKED_tex(HOOKED_pos); }\n";
            std::ofstream(folder / L"input.conf", std::ios::binary)
                << "K glsl-shaders set \"~~/shaders/one.glsl;~~/shaders/two.glsl\"; show-text \"Both\"\n";
            CustomShaders::Refresh();
            int chain = ShaderBuiltinCount;
            while (chain < ShaderCount && wcscmp(ShaderCatalog[chain].label, L"Both"))
                ++chain;
            Require(chain < ShaderCount, "package chain listed");
            const pl_hook* joined = ParseUserShader(gpu, chain, {});
            Require(joined != nullptr, "package chain parses as one shader");
            pl_mpv_user_shader_destroy(&joined);
            std::filesystem::remove_all(folder, error);
            CustomShaders::Refresh();
            RegDeleteTreeW(HKEY_CURRENT_USER, root);
        }
        pl_renderer_destroy(&renderer);
        pl_tex_destroy(gpu, &target);
        for (auto& tex : planes)
            pl_tex_destroy(gpu, &tex);
        for (auto& hook : hooks)
            pl_mpv_user_shader_destroy(&hook);
        pl_d3d11_destroy(&d3d);
        printf("PASS user shaders: %d catalogue shaders load and parse; RAVU Zoom R2/R3 variants render\n",
               ShaderBuiltinCount);

        // Stages come from the files; every packaged shader has one.
        const auto find = [](const char* file)
        {
            for (int id = 0; id < ShaderBuiltinCount; id++)
                if (!strcmp(ShaderCatalog[id].file, file))
                    return id;
            throw std::runtime_error(file);
        };
        for (int id = 0; id < ShaderBuiltinCount; id++)
            Require(ShaderStage(id) != ShaderStageUnknown && *ShaderStageLabel(ShaderStage(id)),
                    ShaderCatalog[id].file);
        const int cas = find("AMD_CAS_scl_RT.glsl"), nlmeans = find("nlmeans_luma_RT.glsl"),
                  artcnn = find("ArtCNN_C4F16.glsl"), clarity = find("eq_clarity_RT.glsl"),
                  hdeband = find("hdeband_rgb_RT.glsl"), krig = find("KrigBilateral.glsl"),
                  ssim = find("SSimDownscaler_RT.glsl"), antiring = find("kantiring_ms_RT.glsl"),
                  dehalo = find("FineDehalo_lite_RT.glsl");
        Require(ShaderStage(artcnn) == 1 && ShaderStage(krig) == 2 && ShaderStage(clarity) == 8 &&
                    ShaderStage(ssim) == 12 && ShaderStage(cas) == 13,
                "stages from //!HOOK lines");
        Require(ShaderStageOfSource("//!HOOK MAINPRESUB\n//!HOOK OUTPUT\n") == 8 &&
                    ShaderStageOfSource("//!HOOK  LUMA\r\n") == 1 &&
                    ShaderStageOfSource("no hooks") == ShaderStageUnknown,
                "earliest stage, MAINPRESUB as MAIN, CRLF and missing hooks");

        // Stored order runs sharpening and contrast first; the automatic order
        // puts cleanup, scaling and sharpening of each stage in turn.
        UserShaderSettings chain;
        const int stored[] = {cas, clarity, antiring, ssim, artcnn, hdeband, krig, nlmeans, dehalo};
        int at = 0;
        for (int id : stored)
            chain.shaders[id].enabled = 1;
        std::vector<int> rest;
        for (int id : chain.order)
            if (std::find(std::begin(stored), std::end(stored), id) == std::end(stored))
                rest.push_back(id);
        for (int id : stored)
            chain.order[at++] = id;
        for (int id : rest)
            chain.order[at++] = id;
        const auto manual = ShaderChainOrder(chain, false);
        Require(std::equal(std::begin(chain.order), std::end(chain.order), manual.begin()), "manual order unchanged");
        const auto automatic = ShaderChainOrder(chain, true);
        std::vector<int> running;
        for (int id : automatic)
            if (chain.shaders[id].enabled)
                running.push_back(id);
        const std::vector<int> expected = {nlmeans, dehalo, artcnn, krig, hdeband, clarity, ssim, antiring, cas};
        Require(running == expected, "automatic order: stage, then cleanup, scaling, sharpening");
        for (size_t i = std::size(stored); i < automatic.size(); i++)
            Require(automatic[i] == chain.order[i], "switched-off shaders keep their places");
        Require(ShaderScalesOfSource("//!WIDTH LUMA.w 2 *\n") && ShaderScalesOfSource("//!HEIGHT LUMA.h 4.0 *\r\n") &&
                    ShaderScalesOfSource("//!WIDTH OUTPUT.w\n") && !ShaderScalesOfSource("//!WIDTH HOOKED.w 2 /\n") &&
                    !ShaderScalesOfSource("//!WIDTH MAIN.w\n") && !ShaderScalesOfSource("//!WIDTH HOOKED.w 1 *\n") &&
                    !ShaderScalesOfSource("//!DESC 2x upscale\n"),
                "scalers recognized only from enlarging or output-sized passes");
        for (int id = 0; id < ShaderBuiltinCount; id++)
            Require(ShaderAutoSorted(id), "packaged shaders are placed automatically");
        // Hand-placed shaders (7, 8) keep their positions while the others sort
        // around them.
        const auto fixedChain =
            AutomaticShaderChain({5, 7, 1, 8, 3}, [](int id) { return id; }, [](int id) { return id != 7 && id != 8; });
        Require(fixedChain == std::vector<int>({1, 7, 3, 8, 5}), "hand-placed shaders keep their positions");
        puts("PASS shader stages and automatic chain order");
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("FAIL %s\n", e.what());
        return 1;
    }
}
