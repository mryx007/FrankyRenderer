#define NOMINMAX
#include "NativeHdrSwapchain.h"
#include "NativeHdrDisplay.h"
#include <libplacebo/renderer.h>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;

static void Check(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}

struct NativeHdrDisplayTest
{
    static inline NativeNvHdr::ColorData state;
    static inline int sets = 0;
    static inline bool fail = false;
    static inline bool delayed = false;
    static inline int pendingReads = 0;

    static int __cdecl Control(unsigned, NativeNvHdr::ColorData* data)
    {
        if (!data->command)
        {
            *data = state;
            if (pendingReads > 0)
            {
                --pendingReads;
                data->mode = 0; // SET accepted; driver has not switched yet.
            }
            return 0;
        }
        ++sets;
        if (fail)
            return -1;
        if (data->mode)
            Check(data->mode == 5, "request raw PQ passthrough, never scRGB mode 2");
        state = *data;
        state.command = 0;
        if (state.mode == 5)
        {
            state.mode = 2; // Actual driver GET normalizes mode 5 to 2.
            if (delayed)
                pendingReads = 1;
        }
        return 0;
    }

    static void Setup(NativeHdrDisplay& output)
    {
        output.initialized = output.injectedTestApi = true;
        output.monitor = reinterpret_cast<HMONITOR>(1);
        output.caps = {NativeHdrDisplay::Vendor::Nvidia, 600, 1};
        output.nvControl = Control;
        sets = 0;
        fail = false;
        delayed = false;
        pendingReads = 0;
        state = {};
    }

    static void Run()
    {
        auto source = pl_color_space_hdr10;
        source.hdr.max_luma = 4000;
        source.hdr.max_cll = 3500;
        {
            NativeHdrDisplay output;
            Setup(output);
            Check(output.Activate(reinterpret_cast<HMONITOR>(1), source), "activate PQ session");
            Check(state.metadata.maximum == 4000 && state.metadata.cll == 3500, "source metadata retained");
            Check(output.Activate(reinterpret_cast<HMONITOR>(1), source) && sets == 1, "no setter per frame");
            output.Restore();
            Check(sets == 2 && !state.mode && !output.Active(), "restore normalized driver state");
            output.Restore();
            Check(sets == 2, "idempotent restoration");
        }
        {
            NativeHdrDisplay output;
            Setup(output);
            state.mode = 2;
            Check(!output.Activate(reinterpret_cast<HMONITOR>(1), source) && !sets, "preserve third-party session");
        }
        for (bool completeBeforeLeaving : {false, true})
        {
            NativeHdrDisplay output;
            Setup(output);
            delayed = true;
            Check(output.Activate(reinterpret_cast<HMONITOR>(1), source), "asynchronous activation accepted");
            Check(output.appliedNv.mode == 5, "cache accepted request rather than transient OFF readback");
            if (completeBeforeLeaving)
                pendingReads = 0;
            Check(output.Restore() && sets == 2 && !state.mode,
                  "leaving restores SDR both before and after asynchronous activation completes");
        }
        {
            NativeHdrDisplay output;
            Setup(output);
            output.caps.displayMetadata = {33593, 15771, 14160, 32617, 7714, 3222, 15673, 16455, 417, 0, 417, 417};
            Check(output.Activate(reinterpret_cast<HMONITOR>(1), source), "monitor EDID normalization setup");
            state.metadata = output.caps.displayMetadata;
            Check(
                output.Restore() && sets == 2 && !state.mode,
                "HDR-to-SDR restores when NVIDIA reports the monitor's 417-nit metadata instead of the 4000-nit source");
        }
        {
            NativeHdrDisplay output;
            Setup(output);
            Check(output.Activate(reinterpret_cast<HMONITOR>(1), source), "restore failure setup");
            fail = true;
            Check(!output.Restore() && output.Active(), "failed restore retains ownership for retry");
            fail = false;
            Check(output.Restore() && !output.Active() && !state.mode, "later restore retries successfully");
        }
        {
            NativeHdrDisplay output;
            Setup(output);
            Check(output.Activate(reinterpret_cast<HMONITOR>(1), source), "external change setup");
            state.metadata.maximum = 1500;
            Check(output.Restore() && sets == 1 && state.mode == 2,
                  "later external active HDR metadata is not overwritten");
        }
        {
            NativeHdrDisplay output;
            Setup(output);
            fail = true;
            Check(!output.Activate(reinterpret_cast<HMONITOR>(1), source), "failed activation reported");
            Check(!output.Activate(reinterpret_cast<HMONITOR>(1), source) && sets == 1, "no repeated failure polling");
            fail = false;
        }
    }
};

int main()
{
    try
    {
        Check(GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0) > 0, "isolated test required");
        NativeHdrDisplayTest::Run();
        pl_d3d11_params params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Check(d3d != nullptr, "D3D11 device");
        HWND window = CreateWindowExW(
            0, L"STATIC", L"PQ test", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "test window");
        {
            NativeHdrSwapchain output;
            Check(SUCCEEDED(output.Ensure(d3d, window, 16, 16, 0)), "initial default latency");
            UINT defaultLatency = 0;
            auto readLatency = [&]()
            {
                ComPtr<IDXGISwapChain2> chain;
                UINT value = 0;
                Check(SUCCEEDED(output.Get()->QueryInterface(IID_PPV_ARGS(&chain))), "latency interface");
                Check(SUCCEEDED(chain->GetMaximumFrameLatency(&value)), "read swapchain latency");
                return value;
            };
            defaultLatency = readLatency();
            for (int manual : {1, 4, 16})
            {
                Check(SUCCEEDED(output.Ensure(d3d, window, 16, 16, manual)), "apply manual latency");
                Check(readLatency() == UINT(manual), "manual latency applied");
                Check(SUCCEEDED(output.Ensure(d3d, window, 16, 16, 0)), "restore default latency");
                Check(readLatency() == defaultLatency, "original default restored");
            }
            Check(SUCCEEDED(output.Ensure(d3d, window, 32, 32, 4)), "resize with manual latency");
            Check(SUCCEEDED(output.Ensure(d3d, window, 16, 16, 0)), "resize and restore default latency");
            Check(readLatency() == defaultLatency, "resize retains original default");
            Check(output.Ensure(d3d, window, 16, 16, -1) == E_INVALIDARG &&
                      output.Ensure(d3d, window, 16, 16, 17) == E_INVALIDARG,
                  "invalid latency rejected");
            output.Destroy();
            Check(SUCCEEDED(output.Ensure(d3d, window, 16, 16, 0)), "recreate default latency");
            Check(readLatency() == defaultLatency, "recreated swapchain default");
            auto renderer = pl_renderer_create(nullptr, d3d->gpu);
            for (int size : {16, 32, 16})
            {
                Check(SUCCEEDED(output.Ensure(d3d, window, size, size, 2)), "10-bit swapchain create/resize");
                std::vector<float> pixels(size * size * 4);
                const float nits[3] = {100, 1000, 4000};
                for (size_t i = 0; i < pixels.size(); ++i)
                    pixels[i] = i % 4 == 3 ? 1.f : pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits[i % 4]);
                pl_tex_params tp = {};
                tp.w = tp.h = size;
                tp.sampleable = true;
                tp.format = pl_find_fmt(d3d->gpu, PL_FMT_FLOAT, 4, 32, 0, PL_FMT_CAP_SAMPLEABLE);
                tp.initial_data = pixels.data();
                auto input = pl_tex_create(d3d->gpu, &tp);
                Check(input != nullptr, "PQ source texture");
                pl_frame source = {}, target = {};
                source.num_planes = target.num_planes = 1;
                source.planes[0].texture = input;
                target.planes[0].texture = output.Target();
                source.planes[0].components = target.planes[0].components = 4;
                for (int c = 0; c < 4; ++c)
                    source.planes[0].component_mapping[c] = target.planes[0].component_mapping[c] = c;
                source.color = target.color = pl_color_space_hdr10;
                source.color.hdr.max_luma = target.color.hdr.max_luma = 4000;
                source.repr.sys = target.repr.sys = PL_COLOR_SYSTEM_RGB;
                source.repr.levels = target.repr.levels = PL_COLOR_LEVELS_FULL;
                source.crop = target.crop = {0, 0, float(size), float(size)};
                auto render = pl_render_default_params;
                render.peak_detect_params = nullptr;
                render.dither_params = nullptr;
                Check(pl_render_image(renderer, &source, &target, &render), "render raw PQ into native output");
                pl_gpu_flush(d3d->gpu);
                ComPtr<ID3D11Texture2D> back, staging;
                ComPtr<ID3D11DeviceContext> context;
                d3d->device->GetImmediateContext(&context);
                Check(SUCCEEDED(output.Get()->GetBuffer(0, IID_PPV_ARGS(&back))), "backbuffer");
                D3D11_TEXTURE2D_DESC desc;
                back->GetDesc(&desc);
                Check(desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM, "no FP16/scRGB surface");
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = desc.MiscFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                Check(SUCCEEDED(d3d->device->CreateTexture2D(&desc, nullptr, &staging)), "staging");
                context->CopyResource(staging.Get(), back.Get());
                D3D11_MAPPED_SUBRESOURCE map = {};
                Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "read PQ signal");
                unsigned value = *static_cast<unsigned*>(map.pData);
                for (int c = 0; c < 3; ++c)
                    Check(std::abs(float((value >> (10 * c)) & 1023) / 1023 - pixels[c]) < .003f,
                          "PQ luminance preserved without scRGB conversion");
                context->Unmap(staging.Get(), 0);
                back.Reset();
                staging.Reset();
                pl_tex_destroy(d3d->gpu, &input);
                Check(SUCCEEDED(output.Submit()), "present signal");
            }
            pl_renderer_destroy(&renderer);
            NativeHdrDisplay driver;
            Check(!driver.Activate(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), pl_color_space_hdr10),
                  "private tests cannot activate physical HDR");
        }
        DestroyWindow(window);
        pl_d3d11_destroy(&d3d);
        puts(
            "PASS NVIDIA PQ: mode 5, source metadata, ownership, failure, restoration, 10-bit GPU signal 100/1000/4000 nits, resize, default latency reset; no physical HDR setter called");
        return 0;
    }
    catch (const std::exception& error)
    {
        printf("FAIL native HDR: %s\n", error.what());
        return 1;
    }
}
