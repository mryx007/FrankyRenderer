// libplacebo integration uses only its public API. No existing MPC pixel path.
#include "Backend.h"
#include "BlackBarAnalysis.h"
#include "GpuPacked422.h"
#include "NativeHdrDisplay.h"
#include "NativeHdrSwapchain.h"
#include "ColorManagement.h"
#include "SettingsChoices.h"
#include "HdrTuning.h"
#include "StatisticsBitmap.h"
#include "PresentationMeasurement.h"
#include "GpuMeasurement.h"
#include "MotionTiming.h"
#include "ShaderCache.h"
#include <deque>
#include <sstream>
#include <iomanip>
#pragma comment(lib, "gdi32.lib")
#include "SharpenShader.h"
#include "UserShaderSource.h"
#include "TonalHook.h"
#include <libplacebo/shaders/custom.h>
#include <charconv>
#include "Trace.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/options.h>
#include <libplacebo/swapchain.h>
#include <libplacebo/utils/upload.h>
#include <dxgi1_6.h>
#include <d3d10.h>
#include <d3d11_1.h>
#include <vfwmsgs.h>
#include <wrl/client.h>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <optional>
#include <new>
#include <unordered_map>
#include <utility>
#include <cmath>
#include <atomic>
#include "build/OverlayVS.h"
#include "build/OverlayPS.h"
#include "build/OverlayFullscreen.h"
#include "build/OverlayAlpha.h"
// Adopting an external device does not make libplacebo load D3D11.dll.
// Keep a normal loader dependency while our COM objects retain that device.
// Otherwise a dynamically loading host can unload the runtime before Release.
#pragma comment(linker, "/include:__imp_D3D11CreateDevice")
using Microsoft::WRL::ComPtr;

struct PlaceboBackend::State
{
    struct StoredFrame
    {
        pl_gpu gpu = nullptr;
        pl_frame image = {};
        VideoFrame description;
        pl_tex planes[4] = {};
        ComPtr<ID3D11Texture2D> copy;

        ~StoredFrame()
        {
            for (auto& plane : planes)
                pl_tex_destroy(gpu, &plane);
        }
    };

    std::unordered_map<uint64_t, std::unique_ptr<StoredFrame>> history;
    uint64_t nextFrame = 1;
    pl_frame mixFrames[16] = {};
    uint64_t mixSignatures[16] = {};
    float mixTimes[16] = {}, mixDuration = 0;
    int mixCount = 0;
    bool mixing = false;
    pl_deinterlace_params deinterlace = pl_deinterlace_default_params;
    TonalHook tonalHook;
    const pl_hook* userHooks[ShaderCount] = {};
    ShaderConfig userCached[ShaderCount];
    bool userFailed[ShaderCount] = {};
    std::wstring userShaderMessage;

    bool PrepareUserShader(int id)
    {
        const auto& c = settings.userShaders.shaders[id];
        auto& cached = userCached[id];
        const bool same = c.enabled == cached.enabled && c.overrides == cached.overrides &&
                          std::equal(std::begin(c.values), std::end(c.values), std::begin(cached.values));
        if (same && userFailed[id])
            return false;
        if (same && userHooks[id])
            return true;
        userFailed[id] = false;
        cached = c;
        const auto shaderSource = LoadUserShaderSource(id);
        const pl_hook* hook = shaderSource.empty()
                                  ? nullptr
                                  : pl_mpv_user_shader_parse(d3d->gpu, shaderSource.data(), shaderSource.size());
        if (hook)
            for (int p = 0; p < ShaderCatalog[id].count; p++)
                if (c.overrides & (1u << p))
                {
                    const pl_hook_par* found = nullptr;
                    for (int n = 0; n < hook->num_parameters; n++)
                        if (!strcmp(hook->parameters[n].name, ShaderCatalog[id].parameters[p].name))
                        {
                            found = &hook->parameters[n];
                            break;
                        }
                    if (!found)
                    {
                        pl_mpv_user_shader_destroy(&hook);
                        break;
                    }
                    if (found->type == PL_VAR_FLOAT)
                        found->data->f = c.values[p];
                    else if (found->type == PL_VAR_SINT)
                        found->data->i = static_cast<int>(c.values[p]);
                    else if (found->type == PL_VAR_UINT)
                        found->data->u = static_cast<unsigned>(c.values[p]);
                }
        if (!hook)
        {
            userFailed[id] = true;
            userShaderMessage = std::wstring(L"shader failed: ") + ShaderCatalog[id].label;
            return false;
        }
        pl_mpv_user_shader_destroy(&userHooks[id]);
        userHooks[id] = hook;
        return true;
    }

    const pl_hook* sharpenHook = nullptr;
    float sharpenCached = -1;
    std::atomic<int> sharpenState = 0;
    StatisticsBitmap statisticsBitmap;
    std::atomic<bool> showStatistics = false;
    std::string sourceCodec;
    double statisticsFps = 0;
    bool statisticsDirect = false;
    int statisticsDropped = -1;
    std::wstring statisticsText;
    ULONGLONG statisticsUpdated = 0;
    PresentationMeasurement presentation;
    DropMeasurement drops;
    std::mutex dropsLock;
    DurationMeasurement renderCpu, presentCpu;
    GpuMeasurement renderGpu;

    uint64_t DropTotal()
    {
        std::lock_guard guard(dropsLock);
        return drops.total;
    }

    std::atomic<unsigned> measurementReset = 0;

    void ApplyMeasurementReset()
    {
        auto value = measurementReset.exchange(0);
        if (value)
        {
            presentation.Reset(!!(value & 2), !(value & 2));
            if (value & 2)
            {
                std::lock_guard guard(dropsLock);
                drops.Reset();
            }
            statisticsText.clear();
            statisticsUpdated = 0;
        }
    }

    int64_t presentationDue = 0;
    bool presentationTimed = false;
    ULONGLONG periodUpdated = 0;
    HMONITOR measuredMonitor = nullptr;
    double measuredPeriod = 0;
    int measuredWidth = 0, measuredHeight = 0;
    int64_t lastSyncQpc = 0;
    UINT lastSyncRefresh = 0;
    bool stableCadence = true;

    NativeHdrDisplay nativeHdr;
    NativeHdrSwapchain nativeSwapchain;
    HMONITOR failedNativeMonitor = nullptr;

    ComPtr<IDXGISwapChain> OutputSwapchain()
    {
        ComPtr<IDXGISwapChain> output;
        if (nativeSwapchain.Active())
            nativeSwapchain.Get()->QueryInterface(IID_PPV_ARGS(&output));
        else if (swapchain)
            output.Attach(pl_d3d11_swapchain_unwrap(swapchain));
        return output;
    }

    void DestroySwapchain()
    {
        nativeSwapchain.Destroy();
        pl_swapchain_destroy(&swapchain);
        presentation.Reset(false);
        presentationTimed = false;
        periodUpdated = 0;
        measuredWidth = measuredHeight = 0;
        lastSyncQpc = 0;
        lastSyncRefresh = 0;
        stableCadence = true;
    }
#ifdef PLACEBO_TEST_PRESENT_PIXELS
    std::function<void(ID3D11Texture2D*)> presentProbe;
    std::function<void(const pl_color_space&)> presentColorProbe;

    void ProbePresent()
    {
        if (presentProbe)
        {
            ComPtr<IDXGISwapChain> native;
            native = OutputSwapchain();
            ComPtr<ID3D11Texture2D> buffer;
            if (native && SUCCEEDED(native->GetBuffer(0, IID_PPV_ARGS(&buffer))))
                presentProbe(buffer.Get());
        }
    }
#endif
    std::string actualTone = "n/a";
    float actualTarget = 0;
    std::atomic<int> outputTextureFormat = 0;
    std::atomic<int> outputLevels = 0, outputDitherDepth = 0;
    std::recursive_mutex lock;
    std::mutex settingsLock;
    std::optional<RenderSettings> pendingSettings;
    std::shared_ptr<ColorAssets> pendingColors, colors;
    HWND window = nullptr;
    HANDLE decoderMutex = nullptr;
    ShaderCache shaderCache;
    pl_log log = nullptr;
    pl_d3d11 d3d = nullptr;
    pl_renderer renderer = nullptr;
    pl_swapchain swapchain = nullptr;
    pl_options options = nullptr;
    pl_tex planes[4] = {};
    pl_frame image = {};
    std::vector<uint8_t> unpacked;
    ComPtr<ID3D11Texture2D> copy;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIDevice1> latencyDevice;
    UINT originalLatency = 0;
    bool latencyChanged = false;
    OverlayPainter painter;
    ComPtr<ID3D11Texture2D> overlayTexture;
    ComPtr<ID3D11RenderTargetView> overlayView;
    pl_tex overlay = nullptr;
    ComPtr<ID3DDeviceContextState> overlayState;
    ComPtr<ID3D11VertexShader> overlayVS;
    ComPtr<ID3D11PixelShader> overlayPS;
    ComPtr<ID3D11InputLayout> overlayLayout;
    bool inverseOverlayAlpha = false;
    ComPtr<ID3D11VertexShader> overlayFullscreen;
    ComPtr<ID3D11PixelShader> overlayAlpha;
    ComPtr<ID3D11BlendState> overlayInvertBlend;
    ComPtr<ID3D11RasterizerState> overlayRasterizer;
    GpuPacked422 unpack422;
    BlackBarAnalysis blackBars;
    RenderSettings settings;
    VideoFrame description;
    RECT source = {}, targetRect = {};
    bool valid = false;
    std::string error;

    static void Log(void* opaque, pl_log_level level, const char* message)
    {
        auto self = static_cast<State*>(opaque);
        if (level <= PL_LOG_ERR)
            self->error = message;
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
    }

    State()
    {
        pl_log_params params = {};
        params.log_cb = Log;
        params.log_priv = this;
        params.log_level = PL_LOG_WARN;
        log = pl_log_create(PL_API_VER, &params);
    }

    ~State()
    {
        Destroy();
        if (decoderMutex)
            CloseHandle(decoderMutex);
        pl_log_destroy(&log);
    }

    void ClearFrame()
    {
        mixing = false;
        if (d3d)
            for (auto& plane : planes)
                pl_tex_destroy(d3d->gpu, &plane);
        copy.Reset();
        unpack422.ResetFrame();
        image = {};
        valid = false;
    }

    void ClearOverlay()
    {
        if (d3d)
            pl_tex_destroy(d3d->gpu, &overlay);
        overlayView.Reset();
        overlayTexture.Reset();
    }

    HRESULT ConfigureLatency()
    {
        if (!d3d)
            return S_OK;
        if (!latencyDevice)
        {
            // Auto must not touch a decoder-owned device. Capture its original
            // value only when the user first requests a manual override.
            if (!settings.maximumFramesInFlight)
                return S_OK;
            ComPtr<IDXGIDevice1> device;
            HRESULT hr = d3d->device->QueryInterface(IID_PPV_ARGS(&device));
            if (FAILED(hr))
                return hr;
            hr = device->GetMaximumFrameLatency(&originalLatency);
            if (FAILED(hr))
                return hr;
            latencyDevice = std::move(device);
        }
        const UINT wanted =
            settings.maximumFramesInFlight ? static_cast<UINT>(settings.maximumFramesInFlight) : originalLatency;
        UINT actual = 0;
        HRESULT hr = latencyDevice->GetMaximumFrameLatency(&actual);
        if (FAILED(hr))
            return hr;
        if (actual != wanted)
        {
            hr = latencyDevice->SetMaximumFrameLatency(wanted);
            if (FAILED(hr))
                return hr;
        }
        latencyChanged = wanted != originalLatency;
        return S_OK;
    }

    void RestoreLatency()
    {
        if (latencyDevice && latencyChanged)
            latencyDevice->SetMaximumFrameLatency(originalLatency);
        latencyChanged = false;
        latencyDevice.Reset();
        originalLatency = 0;
    }

    void Destroy()
    {
        blackBars.Reset();
        unpack422.Reset();
        renderGpu.Reset();
        renderCpu.Reset();
        presentCpu.Reset();
        presentation.Reset(false);
        statisticsBitmap.Reset();
        statisticsText.clear();
        statisticsUpdated = 0;
        RestoreLatency();
        ClearFrame();
        history.clear();
        ClearOverlay();
        overlayState.Reset();
        overlayVS.Reset();
        overlayPS.Reset();
        overlayLayout.Reset();
        overlayFullscreen.Reset();
        overlayAlpha.Reset();
        overlayInvertBlend.Reset();
        overlayRasterizer.Reset();
        DestroySwapchain();
        nativeHdr.Restore();
        failedNativeMonitor = nullptr;
        pl_options_free(&options);
        pl_renderer_destroy(&renderer);
        pl_mpv_user_shader_destroy(&sharpenHook);
        sharpenCached = -1;
        for (int i = 0; i < ShaderCount; i++)
        {
            pl_mpv_user_shader_destroy(&userHooks[i]);
            userFailed[i] = false;
            userCached[i] = {};
        }
        userShaderMessage.clear();
        pl_d3d11_destroy(&d3d);
        context.Reset();
        shaderCache.Save();
    }

    bool DisplayReferences(const StoredFrame& stored) const
    {
        const auto texture = stored.image.planes[0].texture;
        auto uses = [&](const pl_frame& frame)
        {
            return (texture && frame.planes[0].texture == texture) || frame.prev == &stored.image ||
                   frame.next == &stored.image;
        };
        if (uses(image))
            return true;
        if (mixing)
            for (int i = 0; i < mixCount; i++)
                if (uses(mixFrames[i]))
                    return true;
        return false;
    }

    uint64_t StoreCurrent()
    {
        auto saved = std::make_unique<StoredFrame>();
        saved->gpu = d3d->gpu;
        saved->image = image;
        saved->description = description;
        saved->copy = std::move(copy);
        for (int i = 0; i < 4; i++)
            saved->planes[i] = std::exchange(planes[i], nullptr);
        // Buffered unpacked planes own their output textures. The packed input
        // copy is no longer needed once dispatch has consumed it.
        if (unpack422.HasFrame())
            saved->copy.Reset();
        unpack422.ResetFrame();
        const uint64_t id = nextFrame++;
        history.emplace(id, std::move(saved));
        image = {};
        valid = false;
        return id;
    }

    HRESULT PaintOverlay(int width, int height, const RECT& video)
    {
        if (!overlayPS)
        {
            HRESULT hr = d3d->device->CreateVertexShader(OverlayVS, sizeof(OverlayVS), nullptr, &overlayVS);
            if (FAILED(hr))
                return hr;
            const D3D11_INPUT_ELEMENT_DESC elements[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            hr = d3d->device->CreateInputLayout(elements, 2, OverlayVS, sizeof(OverlayVS), &overlayLayout);
            if (FAILED(hr))
                return hr;
            hr = d3d->device->CreatePixelShader(OverlayPS, sizeof(OverlayPS), nullptr, &overlayPS);
            if (FAILED(hr))
                return hr;
        }
        if (inverseOverlayAlpha && !overlayInvertBlend)
        {
            HRESULT hr = d3d->device->CreateVertexShader(
                OverlayFullscreen, sizeof(OverlayFullscreen), nullptr, &overlayFullscreen);
            if (FAILED(hr))
                return hr;
            hr = d3d->device->CreatePixelShader(OverlayAlpha, sizeof(OverlayAlpha), nullptr, &overlayAlpha);
            if (FAILED(hr))
                return hr;
            D3D11_RASTERIZER_DESC raster = {};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.DepthClipEnable = TRUE;
            hr = d3d->device->CreateRasterizerState(&raster, &overlayRasterizer);
            if (FAILED(hr))
                return hr;
            // Host bitmaps use 0=opaque, 1=transparent. Normalize only alpha
            // in place: source(1) - destination(alpha), preserving all RGB.
            D3D11_BLEND_DESC blend = {};
            auto& output = blend.RenderTarget[0];
            output.BlendEnable = TRUE;
            output.SrcBlend = D3D11_BLEND_ONE;
            output.DestBlend = D3D11_BLEND_ZERO;
            output.BlendOp = D3D11_BLEND_OP_ADD;
            output.SrcBlendAlpha = output.DestBlendAlpha = D3D11_BLEND_ONE;
            output.BlendOpAlpha = D3D11_BLEND_OP_SUBTRACT;
            output.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALPHA;
            hr = d3d->device->CreateBlendState(&blend, &overlayInvertBlend);
            if (FAILED(hr))
                return hr;
        }
        D3D11_TEXTURE2D_DESC desc = {};
        if (overlayTexture)
            overlayTexture->GetDesc(&desc);
        if (!overlayTexture || desc.Width != static_cast<UINT>(width) || desc.Height != static_cast<UINT>(height))
        {
            ClearOverlay();
            desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = d3d->device->CreateTexture2D(&desc, nullptr, &overlayTexture);
            if (FAILED(hr))
                return hr;
            hr = d3d->device->CreateRenderTargetView(overlayTexture.Get(), nullptr, &overlayView);
            if (FAILED(hr))
            {
                ClearOverlay();
                return hr;
            }
            pl_d3d11_wrap_params wrap = {};
            wrap.tex = overlayTexture.Get();
            overlay = pl_d3d11_wrap(d3d->gpu, &wrap);
            if (!overlay)
            {
                ClearOverlay();
                return E_FAIL;
            }
        }
        ComPtr<ID3D11DeviceContext1> context1;
        ComPtr<ID3DDeviceContextState> previous;
        HRESULT hr = context.As(&context1);
        if (FAILED(hr))
            return hr;
        if (!overlayState)
        {
            ComPtr<ID3D11Device1> device1;
            hr = d3d->device->QueryInterface(IID_PPV_ARGS(&device1));
            if (FAILED(hr))
                return hr;
            D3D_FEATURE_LEVEL level = d3d->device->GetFeatureLevel();
            hr = device1->CreateDeviceContextState(
                0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr, &overlayState);
            if (FAILED(hr))
                return hr;
        }
        context1->SwapDeviceContextState(overlayState.Get(), &previous);
        const float transparent[4] = {0, 0, 0, inverseOverlayAlpha ? 1.f : 0.f};
        context->ClearRenderTargetView(overlayView.Get(), transparent);
        auto view = overlayView.Get();
        context->OMSetRenderTargets(1, &view, nullptr);
        D3D11_VIEWPORT viewport = {0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(overlayLayout.Get());
        context->VSSetShader(overlayVS.Get(), nullptr, 0);
        context->PSSetShader(overlayPS.Get(), nullptr, 0);
        // Alpha normalization below leaves an alpha-only write mask in this
        // saved context. Restore normal writes before the next host bitmap.
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        hr = painter(d3d->device, context.Get(), width, height, video);
        if (SUCCEEDED(hr) && inverseOverlayAlpha)
        {
            context->OMSetRenderTargets(1, &view, nullptr);
            context->RSSetViewports(1, &viewport);
            context->RSSetState(overlayRasterizer.Get());
            context->OMSetDepthStencilState(nullptr, 0);
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(overlayFullscreen.Get(), nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            context->PSSetShader(overlayAlpha.Get(), nullptr, 0);
            context->OMSetBlendState(overlayInvertBlend.Get(), nullptr, 0xffffffff);
            context->Draw(3, 0);
        }
        context1->SwapDeviceContextState(previous.Get(), nullptr);
        return hr;
    }

    HRESULT Ensure()
    {
        if (!d3d)
        {
            pl_d3d11_params params = pl_d3d11_default_params;
            params.min_feature_level = D3D_FEATURE_LEVEL_11_0;
            d3d = pl_d3d11_create(log, &params);
            if (!d3d)
                return E_FAIL;
        }
        if (!context)
            d3d->device->GetImmediateContext(&context);
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(d3d->device->QueryInterface(IID_PPV_ARGS(&multithread))))
            multithread->SetMultithreadProtected(TRUE);
        if (!renderer)
        {
            shaderCache.Attach(d3d->gpu);
            renderer = pl_renderer_create(log, d3d->gpu);
        }
        if (!options)
            options = pl_options_alloc(log);
        if (!renderer || !options)
            return E_FAIL;
        HRESULT hr = ApplySettings();
        return FAILED(hr) ? hr : Configure();
    }

    HRESULT ApplySettings()
    {
        std::optional<RenderSettings> next;
        std::shared_ptr<ColorAssets> nextColors;
        {
            std::lock_guard guard(settingsLock);
            next.swap(pendingSettings);
            if (next)
                nextColors = std::move(pendingColors);
        }
        if (!next)
            return S_OK;
        const auto oldBars = settings.blackBars;
        auto previous = settings;
        auto previousColors = colors;
        settings = std::move(*next);
        colors = std::move(nextColors);
        HRESULT hr = Configure();
        Trace("Backend settings applied HDRMode=%d brightness=%.4f result=%08lX",
              settings.hdrMode,
              settings.brightness,
              static_cast<unsigned long>(hr));
        if (FAILED(hr))
        {
            settings = std::move(previous);
            colors = std::move(previousColors);
            Configure();
        }
        else
        {
            if (memcmp(&previous.tonal, &settings.tonal, sizeof(settings.tonal)) && renderer)
                pl_renderer_flush_cache(renderer);
            if (memcmp(&oldBars, &settings.blackBars, sizeof(oldBars)))
                blackBars.Reset();
            if (previous.outputFormat != settings.outputFormat)
            {
                DestroySwapchain();
                outputTextureFormat = 0;
            }
        }
        return hr;
    }

    void RepaintPending()
    {
        std::lock_guard guard(settingsLock);
        if (pendingSettings && window)
            PostMessageW(window, WM_PAINT, 0, 0);
    }

    HRESULT Configure()
    {
        if (!options)
            return S_OK;
        HRESULT latency = ConfigureLatency();
        if (FAILED(latency))
            return latency;
        pl_options_reset(options, &pl_render_default_params);
        if (!settings.scaling)
        {
            if (!pl_options_set_str(options, "upscaler", settings.upscaler.c_str()) ||
                !pl_options_set_str(options, "downscaler", settings.downscaler.c_str()))
                return E_INVALIDARG;
            if (settings.chromaUpscaler != "auto" &&
                !pl_options_set_str(options, "plane_upscaler", settings.chromaUpscaler.c_str()))
                return E_INVALIDARG;
        }
        else if (!ApplyScaling(options, *settings.scaling))
            return E_INVALIDARG;
        if (settings.toneMapping != "auto")
        {
            if (!pl_options_set_str(options, "tone_mapping", settings.toneMapping.c_str()))
                return E_INVALIDARG;
            options->params.color_map_params = &options->color_map_params;
        }
        options->peak_detect_params = pl_peak_detect_default_params;
        options->peak_detect_params.smoothing_period = settings.peakSmoothing;
        options->peak_detect_params.scene_threshold_low = settings.sceneLow;
        options->peak_detect_params.scene_threshold_high = settings.sceneHigh;
        options->peak_detect_params.percentile = settings.peakPercentile;
        options->params.peak_detect_params = settings.peakDetect ? &options->peak_detect_params : nullptr;
        options->peak_detect_params.black_cutoff = settings.blackCutoff;
        options->peak_detect_params.allow_delayed = settings.allowDelayedPeak;
        if (settings.gamutMapping &&
            !pl_options_set_str(options, "gamut_mapping", GamutChoices[settings.gamutMapping].name))
            return E_INVALIDARG;
        options->color_map_params.contrast_recovery = settings.contrastRecovery;
        options->color_map_params.contrast_smoothness = settings.contrastSmoothness;
        options->color_map_params.tone_constants.knee_adaptation = settings.kneeAdaptation;
        options->color_map_params.tone_constants.spline_contrast = settings.splineContrast;
        options->color_map_params.tone_constants.knee_minimum = settings.kneeMinimum;
        options->color_map_params.tone_constants.knee_maximum = settings.kneeMaximum;
        options->color_map_params.tone_constants.knee_default = std::clamp(
            options->color_map_params.tone_constants.knee_default, settings.kneeMinimum, settings.kneeMaximum);
        options->color_map_params.gamut_constants.perceptual_strength = settings.perceptualStrength;
        options->color_map_params.gamut_constants.softclip_knee = settings.softclipKnee;
        options->color_map_params.show_clipping = settings.showClipping;
        options->params.color_map_params = &options->color_map_params;
        if (!ApplyMappingOverrides(options, settings.colorMapping))
            return E_INVALIDARG;
        if (!settings.scaling)
        {
            options->params.antiringing_strength = settings.antiringing;
            if (settings.upscalerBlur > 0 && options->params.upscaler && options->params.upscaler->kernel)
            {
                options->upscaler = *options->params.upscaler;
                options->upscaler.blur = settings.upscalerBlur;
                options->params.upscaler = &options->upscaler;
            }
            if (settings.downscalerBlur > 0 && options->params.downscaler && options->params.downscaler->kernel)
            {
                options->downscaler = *options->params.downscaler;
                options->downscaler.blur = settings.downscalerBlur;
                options->params.downscaler = &options->downscaler;
            }
        }
        options->deband_params = pl_deband_default_params;
        options->deband_params.iterations = settings.debandIterations;
        options->deband_params.threshold = settings.debandThreshold;
        options->deband_params.radius = settings.debandRadius;
        options->deband_params.grain = settings.debandGrain;
        options->params.deband_params = settings.deband ? &options->deband_params : nullptr;
        options->dither_params = pl_dither_default_params;
        const pl_dither_method methods[] = {PL_DITHER_BLUE_NOISE, PL_DITHER_ORDERED_FIXED, PL_DITHER_WHITE_NOISE};
        if (settings.ditherMethod < 3)
            options->dither_params.method = methods[settings.ditherMethod];
        options->dither_params.temporal = settings.ditherTemporal;
        options->params.dither_params = settings.ditherMethod == 3 ? nullptr : &options->dither_params;
        if (!ApplyExtras(options, settings.extras))
            return E_INVALIDARG;
        deinterlace.algo = settings.deinterlacing == 2 ? PL_DEINTERLACE_YADIF : PL_DEINTERLACE_BWDIF;
        options->params.deinterlace_params = settings.deinterlacing ? &deinterlace : nullptr;
        return S_OK;
    }

    bool PrepareSharpen()
    {
        if (sharpenHook && sharpenCached == settings.sharpenStrength)
            return true;
        std::string shaderText = AdaptiveSharpenSource;
        auto stage = shaderText.find("//!HOOK OUTPUT");
        if (stage == std::string::npos)
            return false;
        shaderText.replace(stage, 14, "//!HOOK PREOUTPUT");
        auto first = shaderText.find("#define curve_height"), last = shaderText.find('\n', first);
        if (first == std::string::npos || last == std::string::npos)
            return false;
        char value[32];
        auto converted = std::to_chars(value, value + 32, settings.sharpenStrength);
        if (converted.ec != std::errc{})
            return false;
        shaderText.replace(
            first, last - first, std::string("#define curve_height ") + std::string(value, converted.ptr));
        const pl_hook* next = pl_mpv_user_shader_parse(d3d->gpu, shaderText.data(), shaderText.size());
        if (!next)
            return false;
        pl_mpv_user_shader_destroy(&sharpenHook);
        sharpenHook = next;
        sharpenCached = settings.sharpenStrength;
        return true;
    }

    void MeasurePresentation(int64_t due, bool timed)
    {
        const auto now = GetTickCount64();
        auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (!periodUpdated || now - periodUpdated > 1000 || monitor != measuredMonitor)
        {
            measuredMonitor = monitor;
            measuredPeriod = MonitorPeriod(window);
            periodUpdated = now;
        }
        LARGE_INTEGER frequency = {};
        QueryPerformanceFrequency(&frequency);
        presentation.Period(measuredPeriod * frequency.QuadPart / 1e7);
        ComPtr<IDXGISwapChain> native;
        native = OutputSwapchain();
        if (!native)
        {
            presentation.Unavailable();
            return;
        }
        UINT submitted = 0;
        if (FAILED(native->GetLastPresentCount(&submitted)))
        {
            presentation.Unavailable();
            return;
        }
        presentation.Submit(submitted, due, timed);
        DXGI_FRAME_STATISTICS result = {};
        auto hr = native->GetFrameStatistics(&result);
        if (hr == DXGI_ERROR_FRAME_STATISTICS_DISJOINT)
        {
            presentation.Disjoint();
            lastSyncQpc = 0;
            lastSyncRefresh = 0;
            stableCadence = true;
        }
        else if (FAILED(hr))
            presentation.Unavailable();
        else
        {
            // A fixed nominal display rate cannot predict variable-refresh
            // scanout. Gate the error counters if DXGI's clock samples disagree.
            if (lastSyncQpc && result.SyncRefreshCount > lastSyncRefresh && measuredPeriod > 0)
            {
                const double expected =
                    (result.SyncRefreshCount - lastSyncRefresh) * measuredPeriod * frequency.QuadPart / 1e7;
                stableCadence = std::abs((result.SyncQPCTime.QuadPart - lastSyncQpc) / expected - 1) < .1;
            }
            if (result.SyncQPCTime.QuadPart > 0)
            {
                lastSyncQpc = result.SyncQPCTime.QuadPart;
                lastSyncRefresh = result.SyncRefreshCount;
            }
            const auto refreshDelta = static_cast<int64_t>(result.PresentRefreshCount) - result.SyncRefreshCount;
            const auto actual = result.SyncQPCTime.QuadPart +
                                static_cast<int64_t>(refreshDelta * measuredPeriod * frequency.QuadPart / 1e7);
            presentation.Observe(result.PresentCount,
                                 result.PresentRefreshCount,
                                 result.SyncQPCTime.QuadPart > 0,
                                 stableCadence,
                                 actual);
        }
    }

    void UpdateStatistics(const pl_frame& output)
    {
        const auto now = GetTickCount64();
        if (!statisticsText.empty() && now - statisticsUpdated < 250)
            return;
        statisticsUpdated = now;
        std::wostringstream text;
        text.imbue(std::locale::classic());
        text << std::fixed;
        auto wide = [](const std::string& s) { return std::wstring(s.begin(), s.end()); };
        const double period = window ? MonitorPeriod(window) : 0;
        DXGI_OUTPUT_DESC1 display = {};
        bool haveDisplay = false;
        if (swapchain || nativeSwapchain.Active())
        {
            ComPtr<IDXGISwapChain> native;
            native = OutputSwapchain();
            ComPtr<IDXGIOutput> out;
            ComPtr<IDXGIOutput6> out6;
            if (native && SUCCEEDED(native->GetContainingOutput(&out)) && SUCCEEDED(out.As(&out6)) &&
                SUCCEEDED(out6->GetDesc1(&display)))
                haveDisplay = true;
        }
        if (period > 0 || haveDisplay)
            text << L"display ";
        if (period > 0)
            text << std::setprecision(5) << 1e7 / period << L"Hz";
        if (haveDisplay)
            text << L" (" << display.BitsPerColor << L" bit, "
                 << (display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ||
                             display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                             display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
                         ? L"RGB, full"
                         : L"DXGI color space " + std::to_wstring(display.ColorSpace))
                 << L")";
        if (period > 0 || haveDisplay)
            text << L"\n";
        text << L"smooth motion ";
        if (mixing && settings.smoothMotion)
            text << L"on (" << wide(options->params.frame_mixer ? options->params.frame_mixer->name : "none") << L")";
        else
            text << (settings.smoothMotion ? L"off (not needed / no frame mix)" : L"off (settings)");
        text << L"\nD3D11 "
             << (window && IsZoomed(GetAncestor(window, GA_ROOT)) ? L"maximized" : L"windowed / borderless")
             << L"\noutput " << output.planes[0].texture->params.format->component_depth[0] << L" bit "
             << (output.planes[0].texture->params.format->type == PL_FMT_FLOAT ? L"float" : L"integer") << L", "
             << (outputLevels == 2 ? L"TV levels" : L"PC levels") << L", dither ";
        if (outputDitherDepth)
            text << outputDitherDepth.load() << L" bit";
        else
            text << L"auto / float";
        const auto layout = Layout(description.format);
        text << L"\n"
             << (sourceCodec.empty() ? L"" : wide(sourceCodec) + L" -> ") << wide(layout.name) << L", " << layout.depth
             << L" bit, "
             << (layout.rgb                           ? L"RGB"
                 : !layout.chromaX && !layout.chromaY ? L"4:4:4"
                 : layout.chromaY                     ? L"4:2:0"
                                                      : L"4:2:2");
        if (statisticsFps > 0)
            text << L"\nvideo " << std::setprecision(3) << statisticsFps << L" fps (says source filter)";
        text << L"\n"
             << description.width << L"x" << description.height << L" -> " << int(output.crop.x1 - output.crop.x0)
             << L"x" << int(output.crop.y1 - output.crop.y0) << L", " << std::setprecision(2)
             << double(description.aspectX > 0 ? description.aspectX : description.width) /
                    (description.aspectY > 0 ? description.aspectY : description.height)
             << L":1";
        text << L"\nscale " << int(image.crop.x0) << L"," << int(image.crop.y0) << L"," << int(image.crop.x1) << L","
             << int(image.crop.y1) << L" -> " << int(output.crop.x0) << L"," << int(output.crop.y0) << L","
             << int(output.crop.x1) << L"," << int(output.crop.y1);
        text << L"\ntouch window from inside";
        const auto* chromaFilter =
            options->params.plane_upscaler ? options->params.plane_upscaler : options->params.upscaler;
        text << L"\nchroma > " << wide(chromaFilter ? chromaFilter->name : "bilinear");
        const bool down = output.crop.x1 - output.crop.x0 < image.crop.x1 - image.crop.x0 ||
                          output.crop.y1 - output.crop.y0 < image.crop.y1 - image.crop.y0;
        text << L"\nimage " << (down ? L"< " : L"> ")
             << wide((down ? options->params.downscaler : options->params.upscaler)
                         ? (down ? options->params.downscaler : options->params.upscaler)->name
                         : "bilinear")
             << ((output.crop.x1 - output.crop.x0 == image.crop.x1 - image.crop.x0 &&
                  output.crop.y1 - output.crop.y0 == image.crop.y1 - image.crop.y0)
                     ? L" (1:1, bypassed)"
                     : L" (selected)");
        if (period > 0)
            text << L"\nvsync " << std::setprecision(2) << period / 1e4 << L"ms";
        if (statisticsFps > 0)
            text << (period > 0 ? L", frame " : L"\nframe ") << std::setprecision(2) << 1000 / statisticsFps << L"ms";
        const char* matrix =
            description.matrix == PL_COLOR_SYSTEM_BT_2020_NC || description.matrix == PL_COLOR_SYSTEM_BT_2020_C
                ? "BT.2020"
            : description.matrix == PL_COLOR_SYSTEM_BT_709 ? "BT.709"
            : description.matrix == PL_COLOR_SYSTEM_BT_601 ? "BT.601"
                                                           : "other/unknown";
        auto prim = [](pl_color_primaries p)
        {
            return p == PL_COLOR_PRIM_BT_2020      ? L"BT.2020"
                   : p == PL_COLOR_PRIM_BT_709     ? L"BT.709"
                   : p == PL_COLOR_PRIM_DCI_P3     ? L"DCI-P3"
                   : p == PL_COLOR_PRIM_DISPLAY_P3 ? L"Display-P3"
                                                   : L"other/unknown";
        };
        text << L"\nmatrix " << wide(matrix) << L" (says upstream)\nprimaries " << prim(description.color.primaries)
             << L" -> " << prim(output.color.primaries);
        const bool hdrSource = pl_color_space_is_hdr(&description.color);
        if (hdrSource)
        {
            const bool dovi = bool(image.repr.dovi),
                       plus = pl_hdr_metadata_contains(&description.color.hdr, PL_HDR_METADATA_HDR10PLUS);
            text << L"\nHDR "
                 << (dovi                                             ? L"Dolby Vision"
                     : plus                                           ? L"HDR10+"
                     : description.color.transfer == PL_COLOR_TRC_HLG ? L"HLG"
                                                                      : L"HDR10");
            if (std::isfinite(description.color.hdr.max_luma) && description.color.hdr.max_luma > 0)
                text << L", " << std::setprecision(0) << description.color.hdr.max_luma << L" nits";
            pl_hdr_metadata measured = {};
            if (pl_renderer_get_hdr_metadata(renderer, &measured))
                text << L"\nmeasured frame " << std::setprecision(1)
                     << pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, measured.max_pq_y) << L" nits";
            if (!actualTone.empty() && actualTone != "n/a")
            {
                text << L"\ntone mapping " << wide(actualTone);
                if (std::isfinite(actualTarget) && actualTarget > 0)
                    text << L"; target " << std::setprecision(0) << actualTarget << L" nits";
            }
        }
        text << L"\n"
             << (description.levels == PL_COLOR_LEVELS_LIMITED ? L"limited range"
                 : description.levels == PL_COLOR_LEVELS_FULL  ? L"full range"
                                                               : L"unknown range")
             << L" (says upstream)";
        text << L"\ndeinterlacing "
             << (image.field == PL_FIELD_NONE  ? L"off (progressive / not active)"
                 : settings.deinterlacing == 2 ? L"Yadif"
                 : settings.deinterlacing == 1 ? L"Bwdif"
                                               : L"off (settings)");
        if (presentation.feedback)
        {
            text << L"\npresent queue ~" << presentation.pending;
            UINT latency = 0;
            ComPtr<IDXGIDevice1> dxgi;
            if (SUCCEEDED(d3d->device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
                SUCCEEDED(dxgi->GetMaximumFrameLatency(&latency)))
                text << L" / " << latency << L" (DXGI pending / limit)";
            else
                text << L" (DXGI pending)";
        }
        if (statisticsDropped >= 0 && presentation.haveCounters)
            text << L"\ndrop/rep/glitch " << DropTotal() << L"/" << presentation.repeatedRefreshes << L"/"
                 << presentation.glitches;
        else
        {
            if (statisticsDropped >= 0)
                text << L"\ndrop " << DropTotal();
            if (presentation.haveCounters)
                text << L"\nrep/glitch " << presentation.repeatedRefreshes << L"/" << presentation.glitches;
        }
        text << L"\nDXGI timing " << presentation.confirmed << L" confirmed; " << presentation.resets << L" resets ("
             << (presentation.timing ? L"active" : L"collecting") << L")";
        text << L"\nrep: extra refreshes; glitch: late >1 vsync";
        auto durations = [&](bool maximum)
        {
            if (!renderCpu.count && !renderGpu.duration.count && !presentCpu.count)
                return;
            text << (maximum ? L"\nmax stats (5s)" : L"\naverage stats") << std::setprecision(2);
            if (renderCpu.count || renderGpu.duration.count)
            {
                text << L"\n  render ";
                if (renderCpu.count)
                    text << (maximum ? renderCpu.Maximum(now) : renderCpu.Average()) << L"ms (CPU)";
                if (renderGpu.duration.count)
                {
                    if (renderCpu.count)
                        text << L", ";
                    text << (maximum ? renderGpu.duration.Maximum(now) : renderGpu.duration.Average()) << L"ms (GPU)";
                }
            }
            if (presentCpu.count)
                text << L"\n  present " << (maximum ? presentCpu.Maximum(now) : presentCpu.Average())
                     << L"ms (CPU call)";
        };
        durations(false);
        durations(true);
        if (settings.hdrMode == 1 && !nativeHdr.Status().empty())
            text << L"\n" << wide(nativeHdr.Status());
        text << L"\ndebanding " << (settings.deband ? L"on" : L"off") << L"; adaptive sharpen "
             << (sharpenState == 1   ? L"on"
                 : sharpenState == 2 ? L"bypassed"
                                     : L"off");
        if (settings.blackBars.enabled)
        {
            const RECT r = blackBars.detector.Bounds(description.width, description.height);
            text << L"\nblack bars ";
            if (blackBars.failed)
                text << L"analysis unavailable (uncropped)";
            else if (!blackBars.detector.measured)
                text << L"detecting";
            else
                text << L"L " << r.left << L", T " << r.top << L", R " << description.width - r.right << L", B "
                     << description.height - r.bottom << L" px; "
                     << (settings.blackBars.crop ? L"crop on" : L"detect only");
        }
        {
            bool first = true;
            for (int id : settings.userShaders.order)
                if (settings.userShaders.shaders[id].enabled)
                {
                    text << (first ? L"\nshaders: " : L", ") << ShaderCatalog[id].label;
                    first = false;
                }
            if (!userShaderMessage.empty())
                text << L"\n" << userShaderMessage;
        }
        text << L"\ntransfer " << (statisticsDirect ? L"D3D11 texture (GPU direct)" : L"CPU memory -> GPU upload");
        statisticsText = text.str();
    }

    void Describe(const VideoFrame& frame)
    {
        const auto layout = Layout(frame.format);
        description = frame;
        image = {};
        image.num_planes = layout.planes;
        image.repr.sys = layout.rgb ? PL_COLOR_SYSTEM_RGB : frame.matrix;
        image.repr.levels = layout.rgb ? PL_COLOR_LEVELS_FULL : frame.levels;
        image.repr.alpha = layout.alpha ? PL_ALPHA_INDEPENDENT : PL_ALPHA_UNKNOWN;
        image.repr.bits.sample_depth = layout.bytes * 8;
        image.repr.bits.color_depth = layout.depth;
        image.repr.bits.bit_shift = layout.shift;
        image.color = frame.color;
        if (image.color.primaries == PL_COLOR_PRIM_UNKNOWN)
            image.color.primaries = PL_COLOR_PRIM_BT_709;
        if (image.color.transfer == PL_COLOR_TRC_UNKNOWN)
            image.color.transfer = PL_COLOR_TRC_BT_1886;
        if (frame.dovi && !layout.rgb)
        {
            image.repr.sys = PL_COLOR_SYSTEM_DOLBYVISION;
            image.repr.dovi = description.dovi.get();
        }
        image.crop = {0.f, 0.f, static_cast<float>(frame.width), static_cast<float>(frame.height)};
        if (!IsRectEmpty(&source))
            image.crop = {static_cast<float>(source.left),
                          static_cast<float>(source.top),
                          static_cast<float>(source.right),
                          static_cast<float>(source.bottom)};
    }

    void Chroma()
    {
        pl_chroma_location_offset(description.chroma, &image.planes[1].shift_x, &image.planes[1].shift_y);
    }

    // SDR-to-SDR is display-referred: changing the assumed display peak must
    // not trigger HDR classification or inverse tone mapping. Keep its normal
    // reference white. The user peak remains active for HDR-to-SDR mapping;
    // ICC calibration separately receives the physical display peak.
    float SdrTargetNits() const
    {
        return pl_color_space_is_hdr(&description.color) ? settings.sdrNits : PL_COLOR_SDR_WHITE;
    }

    bool HdrActive()
    {
        if (!swapchain)
            return false;
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput6> output6;
        // unwrap returns an owned reference. Retaining it prevents replacement
        // of the flip-model swapchain on this HWND after an HDR mode change.
        ComPtr<IDXGISwapChain> native;
        native = OutputSwapchain();
        DXGI_OUTPUT_DESC1 desc = {};
        return native && SUCCEEDED(native->GetContainingOutput(&output)) && SUCCEEDED(output.As(&output6)) &&
               SUCCEEDED(output6->GetDesc1(&desc)) && desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    }

    HRESULT PresentNative(int64_t due, bool timed)
    {
        pl_frame target = {};
        target.num_planes = 1;
        target.planes[0].texture = nativeSwapchain.Target();
        target.planes[0].components = 4;
        for (int i = 0; i < 4; ++i)
            target.planes[0].component_mapping[i] = i;
        target.crop = {0, 0, float(target.planes[0].texture->params.w), float(target.planes[0].texture->params.h)};
        target.repr.sys = PL_COLOR_SYSTEM_RGB;
        target.repr.levels = PL_COLOR_LEVELS_FULL;
        target.repr.alpha = PL_ALPHA_INDEPENDENT;
        target.color = pl_color_space_hdr10;
        target.color.hdr = image.color.hdr;
        LARGE_INTEGER begin = {}, end = {}, frequency = {};
        QueryPerformanceFrequency(&frequency);
        renderGpu.Begin(d3d->device, context.Get());
        QueryPerformanceCounter(&begin);
        const bool rendered = Render(target, true, true);
        QueryPerformanceCounter(&end);
        renderGpu.End(context.Get());
        if (!rendered)
            return E_FAIL;
        renderCpu.Add(GetTickCount64(), 1000. * (end.QuadPart - begin.QuadPart) / frequency.QuadPart);
        QueryPerformanceCounter(&begin);
        const HRESULT result = nativeSwapchain.Submit();
        QueryPerformanceCounter(&end);
        presentCpu.Add(GetTickCount64(), 1000. * (end.QuadPart - begin.QuadPart) / frequency.QuadPart);
        if (FAILED(result))
            return result;
        outputTextureFormat = 10;
        MeasurePresentation(due, timed);
        RepaintPending();
        return S_OK;
    }

    bool Render(pl_frame& target, bool hdrOutput = false, bool nativeOutput = false)
    {
        blackBars.Analyze(d3d, context.Get(), image, description.width, description.height, settings.blackBars);
        const bool autoZoom = settings.blackBars.enabled && settings.blackBars.crop && !blackBars.failed;
        bool cropBars = autoZoom && blackBars.detector.measured;

        // Apply only temporary crops: stored frames and decoder references keep
        // their original geometry. Every temporal sample uses identical bounds.
        struct RestoreCrops
        {
            pl_frame& image;
            pl_frame* mix;
            pl_rect2df main;
            pl_rect2df saved[16];
            int count;

            RestoreCrops(pl_frame& i, pl_frame* m, int n) : image(i), mix(m), main(i.crop), count(n)
            {
                for (int k = 0; k < n; k++)
                    saved[k] = m[k].crop;
            }

            ~RestoreCrops()
            {
                image.crop = main;
                for (int k = 0; k < count; k++)
                    mix[k].crop = saved[k];
            }
        } restore(image, mixFrames, mixCount);

        if (cropBars)
        {
            const RECT r = blackBars.detector.Bounds(description.width, description.height);
            auto crop = [&](pl_frame& f)
            {
                f.crop.x0 = std::max(f.crop.x0, float(r.left));
                f.crop.y0 = std::max(f.crop.y0, float(r.top));
                f.crop.x1 = std::min(f.crop.x1, float(r.right));
                f.crop.y1 = std::min(f.crop.y1, float(r.bottom));
            };
            if (r.left < image.crop.x1 && r.right > image.crop.x0 && r.top < image.crop.y1 && r.bottom > image.crop.y0)
            {
                crop(image);
                for (int i = 0; i < mixCount; i++)
                    crop(mixFrames[i]);
            }
            else
                cropBars = false;
        }
        if (settings.outputLevels)
            target.repr.levels = settings.outputLevels == 1 ? PL_COLOR_LEVELS_FULL : PL_COLOR_LEVELS_LIMITED;
        if (settings.displayDepth)
        {
            const auto fmt = target.planes[0].texture->params.format;
            const int depth = fmt->type == PL_FMT_FLOAT ? settings.displayDepth
                                                        : std::min(settings.displayDepth, fmt->component_depth[0]);
            target.repr.bits.sample_depth = target.repr.bits.color_depth = depth;
            target.repr.bits.bit_shift = 0;
        }
        outputLevels = target.repr.levels == PL_COLOR_LEVELS_LIMITED ? 2 : 1;
        outputDitherDepth = target.repr.bits.color_depth ? target.repr.bits.color_depth
                            : target.planes[0].texture->params.format->type == PL_FMT_UNORM
                                ? target.planes[0].texture->params.format->component_depth[0]
                                : 0;
        if (!hdrOutput && colors)
        {
            target.icc = colors->icc;
            target.lut = colors->lut;
            target.lut_type = PL_LUT_NATIVE;
        }
        if (!IsRectEmpty(&targetRect))
            target.crop = {static_cast<float>(targetRect.left),
                           static_cast<float>(targetRect.top),
                           static_cast<float>(targetRect.right),
                           static_cast<float>(targetRect.bottom)};
        const float originalAspect = description.aspectX > 0 && description.aspectY > 0
                                         ? static_cast<float>(description.aspectX) / description.aspectY
                                         : static_cast<float>(description.width) / description.height;
        const float aspect = cropBars ? originalAspect * ((image.crop.x1 - image.crop.x0) / description.width) /
                                            ((image.crop.y1 - image.crop.y0) / description.height)
                                      : originalAspect;
        // With automatic crop, fit the active image into the complete output;
        // the player's old destination can still include encoded letterboxing.
        if (autoZoom)
            target.crop = {0, 0, float(target.planes[0].texture->params.w), float(target.planes[0].texture->params.h)};
        float width = target.crop.x1 - target.crop.x0, height = target.crop.y1 - target.crop.y0;
        const bool fill = autoZoom && settings.blackBars.zoom == 1 && !blackBars.detector.subtitleProtection;
        if ((width / height > aspect) != fill)
        {
            float pad = (width - height * aspect) * .5f;
            target.crop.x0 += pad;
            target.crop.x1 -= pad;
        }
        else
        {
            float pad = (height - width / aspect) * .5f;
            target.crop.y0 += pad;
            target.crop.y1 -= pad;
        }
        pl_color_adjustment adjustment = pl_color_adjustment_neutral;
        adjustment.saturation = settings.saturation;
        adjustment.contrast = settings.contrast;
        adjustment.brightness = settings.brightness;
        adjustment.gamma = settings.gamma;
        adjustment.hue = settings.hue;
        adjustment.temperature = settings.temperature;
        pl_render_params params = options->params;
        params.color_adjustment = settings.colorAdjustment ? &adjustment : nullptr;
        // Default preserves the established output-calibration stage. Manual
        // LUT types use libplacebo's actual color-mapping LUT semantics.
        if (!hdrOutput && colors && colors->lut && settings.extras.values[14] >= 0)
        {
            params.lut = colors->lut;
            params.lut_type = static_cast<pl_lut_type>(static_cast<int>(settings.extras.values[14]));
            target.lut = nullptr;
        }
        sharpenState = 0;
        if (settings.adaptiveSharpen && settings.sharpenStrength > 0)
        {
            // Upstream filter expects bounded non-linear RGB. Do not apply it
            // to linear scRGB/float output, where valid HDR values exceed 1.
            const bool supported =
                target.color.transfer != PL_COLOR_TRC_LINEAR && target.color.transfer != PL_COLOR_TRC_UNKNOWN &&
                (!pl_color_transfer_is_hdr(target.color.transfer) || target.color.transfer == PL_COLOR_TRC_PQ);
            if (supported)
            {
                if (!PrepareSharpen())
                    return false;
                params.hooks = &sharpenHook;
                params.num_hooks = 1;
                sharpenState = 1;
            }
            else
                sharpenState = 2;
        }

        const pl_hook* hooks[ShaderCount + 2] = {};
        int hookIds[ShaderCount] = {};
        int users = 0, count = 0;
        if (params.num_hooks)
            hooks[count++] = sharpenHook;
        tonalHook.settings = settings.tonal;
        if (ActiveTonal(settings.tonal) && pl_color_space_is_hdr(&image.color))
            hooks[count++] = &tonalHook.hook;
        const int builtins = count;
        userShaderMessage.clear();
        for (int id : settings.userShaders.order)
        {
            if (!settings.userShaders.shaders[id].enabled)
            {
                pl_mpv_user_shader_destroy(&userHooks[id]);
                userCached[id] = {};
                userFailed[id] = false;
                continue;
            }
            if (ShaderCatalog[id].outputStage && target.color.transfer == PL_COLOR_TRC_LINEAR)
            {
                userShaderMessage = L"output-stage shaders bypassed for linear scRGB";
                continue;
            }
            if (PrepareUserShader(id))
            {
                hooks[count++] = userHooks[id];
                hookIds[users++] = id;
            }
            else
                userShaderMessage = std::wstring(L"shader failed: ") + ShaderCatalog[id].label;
        }
        params.hooks = hooks;
        params.num_hooks = count;
        auto renderWithRecovery = [&](auto&& draw)
        {
            if (draw())
                return true;
            if (!users)
                return false;
            for (int n = 0; n < users; n++)
                userFailed[hookIds[n]] = true;
            userShaderMessage = L"custom shaders bypassed after a render error";
            params.hooks = hooks;
            params.num_hooks = builtins;
            pl_renderer_flush_cache(renderer);
            return draw();
        };
        pl_color_map_params dynamicMap =
            params.color_map_params ? *params.color_map_params : pl_color_map_default_params;
        const bool protectHighlights =
            settings.clippingProtection && settings.hdrMode != 1 && pl_color_space_is_hdr(&image.color);
        if (protectHighlights)
        {
            dynamicMap.tone_mapping_function = &pl_tone_map_spline;
            dynamicMap.inverse_tone_mapping = false;
            params.peak_detect_params = &options->peak_detect_params;
            params.color_map_params = &dynamicMap;
        }
        const bool explicitMetadata = settings.colorMapping.values[MappingIndex("tone_map_metadata")][0] != 0;
        if (image.repr.dovi && !explicitMetadata)
        {
            const bool authoredBrightness = pl_hdr_metadata_contains(&image.color.hdr, PL_HDR_METADATA_CIE_Y);
            dynamicMap.metadata = authoredBrightness ? PL_HDR_METADATA_CIE_Y : PL_HDR_METADATA_ANY;
            if (authoredBrightness)
                params.peak_detect_params = nullptr;
            params.color_map_params = &dynamicMap;
        }
        else if (!explicitMetadata && pl_hdr_metadata_contains(&image.color.hdr, PL_HDR_METADATA_HDR10PLUS))
        {
            // Prefer authored scene metadata to libplacebo's measured peak.
            dynamicMap.metadata = PL_HDR_METADATA_HDR10PLUS;
            if ((settings.toneMapping == "auto" || protectHighlights) && image.color.hdr.ootf.target_luma > 0)
                dynamicMap.tone_mapping_function = &pl_tone_map_st2094_40;
            params.color_map_params = &dynamicMap;
        }
        if (hdrOutput && settings.hdrMode == 1)
        {
            // Preserve source luminance/gamut for the HDR display. Decoder DV
            // reshaping and required transfer/format conversion remain active.
            if (!nativeOutput)
                target.color.primaries = image.color.primaries;
            target.color.hdr = image.color.hdr;
            dynamicMap.tone_mapping_function = &pl_tone_map_clip;
            dynamicMap.gamut_mapping = &pl_gamut_map_clip;
            dynamicMap.contrast_recovery = 0;
            dynamicMap.inverse_tone_mapping = false;
            dynamicMap.show_clipping = false;
            params.color_map_params = &dynamicMap;
            params.peak_detect_params = nullptr;
        }
        actualTone = dynamicMap.tone_mapping_function ? dynamicMap.tone_mapping_function->name : "n/a";
        actualTarget = target.color.hdr.max_luma;
        pl_overlay overlays[2] = {};
        pl_overlay_part parts[2] = {};
        auto& overlayInfo = overlays[0];
        auto& part = parts[0];

        if (painter)
        {
            const int w = target.planes[0].texture->params.w, h = target.planes[0].texture->params.h;
            RECT video = {static_cast<LONG>(target.crop.x0),
                          static_cast<LONG>(target.crop.y0),
                          static_cast<LONG>(target.crop.x1),
                          static_cast<LONG>(target.crop.y1)};
            if (autoZoom)
            {
                video.left = std::max<LONG>(0, video.left);
                video.top = std::max<LONG>(0, video.top);
                video.right = std::min<LONG>(w, video.right);
                video.bottom = std::min<LONG>(h, video.bottom);
            }
            if (FAILED(PaintOverlay(w, h, video)))
                return false;
            part.src = part.dst = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h)};
            overlayInfo.tex = overlay;
            overlayInfo.mode = PL_OVERLAY_NORMAL;
            overlayInfo.coords = PL_OVERLAY_COORDS_DST_FRAME;
            overlayInfo.repr.sys = PL_COLOR_SYSTEM_RGB;
            overlayInfo.repr.levels = PL_COLOR_LEVELS_FULL;
            overlayInfo.repr.alpha = PL_ALPHA_PREMULTIPLIED;
            overlayInfo.color = pl_color_space_srgb;
            overlayInfo.parts = &part;
            overlayInfo.num_parts = 1;
            target.overlays = &overlayInfo;
            target.num_overlays = 1;
        }
        if (showStatistics.load())
        {
            UpdateStatistics(target);
            if (statisticsBitmap.Update(d3d->gpu,
                                        statisticsText,
                                        target.planes[0].texture->params.w,
                                        target.planes[0].texture->params.h,
                                        window))
            {
                const int index = target.num_overlays;
                statisticsBitmap.Describe(overlays[index], parts[index]);
                target.overlays = overlays;
                target.num_overlays = index + 1;
            }
        }
        if (mixing && settings.smoothMotion)
        {
            const pl_frame* frames[16] = {};
            for (size_t i = 0; i < static_cast<size_t>(mixCount); i++)
                frames[i] = &mixFrames[i];
            pl_frame_mix mix = {};
            mix.num_frames = mixCount;
            if (mix.num_frames == 2 && mixSignatures[0] == mixSignatures[1])
                mix.num_frames = 1;
            mix.frames = frames;
            uint64_t signatures[16];
            for (int i = 0; i < mixCount; i++)
                signatures[i] = mixSignatures[i] ^ (blackBars.generation * 0x9e3779b97f4a7c15ULL);
            mix.signatures = signatures;
            mix.timestamps = mixTimes;
            mix.vsync_duration = mixDuration;
            if (!settings.scaling)
                params.frame_mixer = &pl_filter_oversample;
            return renderWithRecovery([&]() { return pl_render_image_mix(renderer, &mix, &target, &params); });
        }
        return renderWithRecovery([&]() { return pl_render_image(renderer, &image, &target, &params); });
    }
};

PlaceboBackend::PlaceboBackend() : state(std::make_unique<State>()) {}

PlaceboBackend::~PlaceboBackend() = default;

HRESULT PlaceboBackend::SetWindow(HWND window)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (s.window != window)
    {
        s.DestroySwapchain();
        if (!s.nativeHdr.Restore())
            return E_FAIL;
        s.failedNativeMonitor = nullptr;
        s.window = window;
    }
    return S_OK;
}

HRESULT PlaceboBackend::SetDevice(ID3D11Device* device, HANDLE mutex)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    HANDLE next = nullptr;
    if (mutex &&
        !DuplicateHandle(GetCurrentProcess(), mutex, GetCurrentProcess(), &next, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    if (s.decoderMutex)
        CloseHandle(s.decoderMutex);
    s.decoderMutex = next;
    if (s.d3d && s.d3d->device == device)
        return S_OK;
    s.Destroy();
    pl_d3d11_params params = pl_d3d11_default_params;
    params.device = device;
    s.d3d = pl_d3d11_create(s.log, &params);
    if (!s.d3d)
        return E_FAIL;
    return s.Ensure();
}

void PlaceboBackend::SetOverlayPainter(OverlayPainter painter, bool inverseAlpha)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.painter = std::move(painter);
    s.inverseOverlayAlpha = inverseAlpha;
    s.ClearOverlay();
}

HRESULT PlaceboBackend::CaptureUpload(const VideoFrame& frame, const uint8_t* bytes, size_t length, uint64_t& id)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    id = 0;
    const auto displayed = s.image;
    const auto description = s.description;
    const bool valid = s.valid, mixing = s.mixing;
    HRESULT hr;
    try
    {
        // A displayed, directly owned frame must survive the capture too:
        // Upload/Import may replace its textures even on a successful call.
        if (valid && (s.copy || s.planes[0]))
            s.StoreCurrent();
        hr = Upload(frame, bytes, length);
        if (SUCCEEDED(hr))
            id = s.StoreCurrent();
    }
    catch (const std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }
    // Upload may already have destroyed/replaced the displayed textures. A
    // shallow frame snapshot cannot restore ownership after any failure.
    if (FAILED(hr))
    {
        s.ClearFrame();
        return hr;
    }
    s.image = displayed;
    s.description = description;
    s.valid = valid;
    s.mixing = mixing;
    if (SUCCEEDED(hr))
        for (auto it = s.history.begin(); it != s.history.end();)
            if (it->first + static_cast<uint64_t>(2 * std::ceil(s.settings.motionRadius) + 4) < id &&
                !s.DisplayReferences(*it->second))
                it = s.history.erase(it);
            else
                ++it;
    return hr;
}

HRESULT PlaceboBackend::CaptureImport(const VideoFrame& frame, ID3D11Texture2D* texture, UINT slice, uint64_t& id)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    id = 0;
    const auto displayed = s.image;
    const auto description = s.description;
    const bool valid = s.valid, mixing = s.mixing;
    HRESULT hr;
    try
    {
        if (valid && (s.copy || s.planes[0]))
            s.StoreCurrent();
        hr = Import(frame, texture, slice);
        if (SUCCEEDED(hr))
            id = s.StoreCurrent();
    }
    catch (const std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }
    // Import may have freed the old frame or created only some planes. Do not
    // revive its copied pointers, and release partial resources on failure.
    if (FAILED(hr))
    {
        s.ClearFrame();
        return hr;
    }
    s.image = displayed;
    s.description = description;
    s.valid = valid;
    s.mixing = mixing;
    if (SUCCEEDED(hr))
        for (auto it = s.history.begin(); it != s.history.end();)
            if (it->first + static_cast<uint64_t>(2 * std::ceil(s.settings.motionRadius) + 4) < id &&
                !s.DisplayReferences(*it->second))
                it = s.history.erase(it);
            else
                ++it;
    return hr;
}

HRESULT PlaceboBackend::SelectField(uint64_t current, uint64_t previous, uint64_t next, pl_field field, pl_field first)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    auto cur = s.history.find(current);
    if (cur == s.history.end())
        return VFW_E_WRONG_STATE;
    s.mixing = false;
    if (!previous)
        previous = current;
    if (!next)
        next = current;
    auto prev = s.history.find(previous), future = s.history.find(next);
    if (prev == s.history.end() || future == s.history.end())
        return VFW_E_WRONG_STATE;
    auto& c = *cur->second;
    auto& p = *prev->second;
    auto& n = *future->second;
    if (c.description.width != p.description.width || c.description.height != p.description.height ||
        c.description.format != p.description.format || c.description.width != n.description.width ||
        c.description.height != n.description.height || c.description.format != n.description.format)
        return E_INVALIDARG;
    s.image = c.image;
    s.description = c.description;
    s.image.field = field;
    s.image.first_field = first;
    s.image.prev = &p.image;
    s.image.next = &n.image;
    s.valid = true;
    return S_OK;
}

HRESULT PlaceboBackend::SelectMix(uint64_t current,
                                  uint64_t previous,
                                  uint64_t next,
                                  pl_field field,
                                  pl_field first,
                                  pl_field following,
                                  float position,
                                  float duration,
                                  uint64_t afterNext)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (!std::isfinite(position) || !std::isfinite(duration) || position < 0 || duration <= 0)
        return E_INVALIDARG;
    HRESULT hr = SelectField(current, previous, next, field, first);
    if (FAILED(hr))
        return hr;
    s.mixFrames[0] = s.mixFrames[1] = s.image;
    s.mixCount = 2;
    const bool same = field != PL_FIELD_NONE && following != first;
    if (next && !same)
    {
        auto found = s.history.find(next);
        if (found == s.history.end())
            return VFW_E_WRONG_STATE;
        auto& n = *found->second;
        s.mixFrames[1] = n.image;
        s.mixFrames[1].field = following;
        s.mixFrames[1].first_field = first;
        s.mixFrames[1].prev = &s.history.at(current)->image;
        auto later = s.history.find(afterNext);
        s.mixFrames[1].next = later != s.history.end() ? &later->second->image : &n.image;
    }
    else
        s.mixFrames[1].field = following;
    s.mixSignatures[0] = current * 4 + static_cast<unsigned>(field);
    s.mixSignatures[1] = (same || !next ? current : next) * 4 + static_cast<unsigned>(following);
    s.mixTimes[0] = -position;
    s.mixTimes[1] = 1.f - position;
    s.mixDuration = duration;
    s.mixing = true;
    return S_OK;
}

HRESULT PlaceboBackend::SelectTemporalMix(const std::vector<TemporalFrame>& frames,
                                          uint64_t current,
                                          uint64_t previous,
                                          uint64_t next,
                                          pl_field field,
                                          pl_field first,
                                          int64_t timestamp,
                                          float duration)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    HRESULT hr = SelectField(current, previous, next, field, first);
    if (FAILED(hr))
        return hr;
    auto cur = std::find_if(frames.begin(), frames.end(), [&](const auto& f) { return f.id == current; });
    if (cur == frames.end() || cur->stop <= cur->start || !std::isfinite(duration) || duration <= 0)
        return E_INVALIDARG;
    const double unit = double(cur->stop - cur->start) / (first == PL_FIELD_NONE ? 1 : 2);
    const double radius = s.settings.motionRadius;
    s.mixCount = 0;
    for (size_t i = 0; i < frames.size(); i++)
    {
        const auto& f = frames[i];
        auto found = s.history.find(f.id);
        if (found == s.history.end())
            return VFW_E_WRONG_STATE;
        auto prev = s.history.find(i ? frames[i - 1].id : f.id),
             nextFrame = s.history.find(i + 1 < frames.size() ? frames[i + 1].id : f.id);
        if (prev == s.history.end() || nextFrame == s.history.end())
            return VFW_E_WRONG_STATE;
        const int count = f.first == PL_FIELD_NONE ? 1 : 2;
        for (int parity = 0; parity < count; parity++)
        {
            const double time = double(f.start + (f.stop - f.start) * parity / count - timestamp) / unit;
            if (std::abs(time) >= radius && f.id != current)
                continue;
            if (s.mixCount == 16)
                return E_INVALIDARG;
            auto image = found->second->image;
            image.field = count == 1 ? PL_FIELD_NONE : parity ? pl_field_other(f.first) : f.first;
            image.first_field = f.first;
            image.prev = &prev->second->image;
            image.next = &nextFrame->second->image;
            s.mixFrames[s.mixCount] = image;
            s.mixSignatures[s.mixCount] = f.id * 4 + static_cast<unsigned>(image.field);
            s.mixTimes[s.mixCount++] = static_cast<float>(time);
        }
    }
    if (!s.mixCount)
        return VFW_E_WRONG_STATE;
    s.mixDuration = duration;
    s.mixing = true;
    return S_OK;
}

HRESULT PlaceboBackend::SetSettings(const RenderSettings& settings)
{
    if (!ValidTonal(settings.tonal))
        return E_INVALIDARG;
    if (!ValidUserShaders(settings.userShaders))
        return E_INVALIDARG;
    for (int i = 0; i < ShaderCount; i++)
        if (settings.userShaders.shaders[i].enabled && LoadUserShaderSource(i).empty())
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (!ValidExtras(settings.extras))
        return E_INVALIDARG;
    if (!ValidCombinedMapping(settings))
        return E_INVALIDARG;
    if (settings.hdrMode < 1 || settings.hdrMode > 2)
        return E_INVALIDARG;
    float radius = 0;
    if (settings.scaling && !ValidScaling(*settings.scaling, &radius))
        return E_INVALIDARG;
    if (settings.maximumFramesInFlight < 0 || settings.maximumFramesInFlight > 16)
        return E_INVALIDARG;
    if (!ValidHdrTuning(settings))
        return E_INVALIDARG;
    // Validate without the render lock: a host callback can synchronously need
    // the settings/UI thread while a frame is in progress.
    const auto range = [](float v, float low, float high) { return std::isfinite(v) && v >= low && v <= high; };
    if (!range(settings.peakSmoothing, 0, 1000) || !range(settings.sceneLow, 0, 100) ||
        !range(settings.sceneHigh, 0, 100) ||
        (settings.sceneLow > 0 && settings.sceneHigh > 0 && settings.sceneLow > settings.sceneHigh) ||
        !range(settings.peakPercentile, 0, 100) || !range(settings.contrastRecovery, 0, 2) ||
        !range(settings.contrastSmoothness, 1, 32) || settings.gamutMapping < 0 ||
        settings.gamutMapping >= static_cast<int>(std::size(GamutChoices)))
        return E_INVALIDARG;
    if (!range(settings.saturation, 0, 100) || !range(settings.contrast, 0, 100) ||
        !range(settings.brightness, -1, 1) || !range(settings.gamma, 0, 100) ||
        !range(settings.hue, -3.141593f, 3.141593f) || !range(settings.temperature, -1.143f, 5.286f))
        return E_INVALIDARG;
    if (!ValidBlackBars(settings.blackBars))
        return E_INVALIDARG;
    if (!range(settings.sharpenStrength, 0, 2))
        return E_INVALIDARG;
    if (settings.outputFormat < 0 || settings.outputFormat > 3)
        return E_INVALIDARG;
    if (settings.outputLevels < 0 || settings.outputLevels > 2)
        return E_INVALIDARG;
    if (settings.displayDepth != 0 && settings.displayDepth != 6 && settings.displayDepth != 8 &&
        settings.displayDepth != 10 && settings.displayDepth != 12 && settings.displayDepth != 16)
        return E_INVALIDARG;
    if (settings.iccIntent < 0 || settings.iccIntent > 1)
        return E_INVALIDARG;
    if ((settings.hdrNits != 0 && !range(settings.hdrNits, 80, 10000)) || !range(settings.antiringing, 0, 1))
        return E_INVALIDARG;
    if (settings.debandIterations < 0 || settings.debandIterations > 16 || settings.ditherMethod < 0 ||
        settings.ditherMethod > 3 || !range(settings.debandThreshold, 0, 1000) ||
        !range(settings.debandRadius, 0, 1000) || !range(settings.debandGrain, 0, 1000))
        return E_INVALIDARG;
    for (float width : {settings.upscalerBlur, settings.downscalerBlur})
        if (width != 0 && !range(width, .5f, 2))
            return E_INVALIDARG;
    pl_options validation = pl_options_alloc(nullptr);
    if (!validation)
        return E_OUTOFMEMORY;
    bool valid = pl_options_set_str(validation, "upscaler", settings.upscaler.c_str()) &&
                 pl_options_set_str(validation, "downscaler", settings.downscaler.c_str()) &&
                 (settings.chromaUpscaler == "auto" ||
                  pl_options_set_str(validation, "plane_upscaler", settings.chromaUpscaler.c_str())) &&
                 (settings.toneMapping == "auto" ||
                  pl_options_set_str(validation, "tone_mapping", settings.toneMapping.c_str()));
    pl_options_free(&validation);
    if (!valid)
        return E_INVALIDARG;
    std::shared_ptr<ColorAssets> colors;
    HRESULT prepared = PrepareColorAssets(settings, colors);
    if (FAILED(prepared))
        return prepared;
    auto& s = *state;
    std::lock_guard guard(s.settingsLock);
    s.pendingSettings = settings;
    s.pendingSettings->motionRadius = radius;
    s.pendingColors = std::move(colors);
    return S_OK;
}

HRESULT PlaceboBackend::Upload(const VideoFrame& frame, const uint8_t* data, size_t bytes)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    auto layout = Layout(frame.format);
    int stride = frame.stride;
    if (!data || frame.width <= 0 || frame.height <= 0 || frame.stride < MinimumStride(frame.format, frame.width) ||
        ((frame.width & 1) && layout.chromaX) || ((frame.height & 1) && layout.chromaY))
        return E_INVALIDARG;
    if (bytes < ImageBytes(frame.format, stride, frame.height))
        return E_INVALIDARG;
    HRESULT hr = s.Ensure();
    if (FAILED(hr))
        return hr;
    if (s.copy)
        s.ClearFrame();
    s.Describe(frame);
    if (frame.format == PixelFormat::Y410)
    {
        s.unpacked.resize(static_cast<size_t>(frame.width) * frame.height * 8);
        for (int y = 0; y < frame.height; y++)
            for (int x = 0; x < frame.width; x++)
            {
                uint32_t pixel;
                memcpy(&pixel, data + size_t(y) * stride + x * 4, 4);
                uint16_t components[] = {uint16_t((pixel & 1023) << 6),
                                         uint16_t(((pixel >> 10) & 1023) << 6),
                                         uint16_t(((pixel >> 20) & 1023) << 6),
                                         uint16_t((pixel >> 30) * 21845)};
                memcpy(s.unpacked.data() + (size_t(y) * frame.width + x) * 8, components, 8);
            }
        data = s.unpacked.data();
        stride = frame.width * 8;
    }
    if (layout.packing == PixelPacking::Packed422)
    {
        // Lossless byte rearrangement only; all color processing remains in libplacebo.
        const int c = layout.bytes;
        const size_t luma = static_cast<size_t>(frame.width) * frame.height * c;
        s.unpacked.resize(luma * 2);
        for (int y = 0; y < frame.height; y++)
            for (int x = 0; x < frame.width; x += 2)
            {
                const auto src = data + static_cast<size_t>(y) * stride + x * c * 2;
                auto dstY = s.unpacked.data() + static_cast<size_t>(y) * frame.width * c + x * c;
                auto dstUV = s.unpacked.data() + luma + static_cast<size_t>(y) * frame.width * c + x * c;
                memcpy(dstY, src + (layout.vu ? 1 : 0) * c, c);
                memcpy(dstY + c, src + (layout.vu ? 3 : 2) * c, c);
                memcpy(dstUV, src + (layout.vu ? 0 : 1) * c, c);
                memcpy(dstUV + c, src + (layout.vu ? 2 : 3) * c, c);
            }
        data = s.unpacked.data();
        stride = frame.width * c;
        layout.packing = PixelPacking::SemiPlanar;
    }
    else if (layout.packing == PixelPacking::PackedRGB && layout.channels == 3)
    {
        const int c = layout.bytes;
        s.unpacked.resize(static_cast<size_t>(frame.width) * frame.height * 4 * c);
        for (int y = 0; y < frame.height; y++)
            for (int x = 0; x < frame.width; x++)
            {
                auto src = data + static_cast<size_t>(y) * stride + x * 3 * c;
                auto dst = s.unpacked.data() + (static_cast<size_t>(y) * frame.width + x) * 4 * c;
                memcpy(dst, src, 3 * c);
                memset(dst + 3 * c, 255, c);
            }
        data = s.unpacked.data();
        stride = frame.width * 4 * c;
        layout.channels = 4;
    }
    size_t offset = 0;
    for (int i = 0; i < layout.planes; i++)
    {
        pl_plane_data plane = {};
        plane.type = PL_FMT_UNORM;
        std::fill_n(plane.component_map, 4, -1);
        plane.width = frame.width >> (i ? layout.chromaX : 0);
        plane.height = frame.height >> (i ? layout.chromaY : 0);
        const bool semi = layout.packing == PixelPacking::SemiPlanar;
        const bool rgb = layout.packing == PixelPacking::PackedRGB;
        const bool packed444 = layout.packing == PixelPacking::Packed444;
        int components = rgb || packed444 ? layout.channels : (semi && i ? 2 : 1);
        plane.pixel_stride = layout.bytes * components;
        plane.row_stride = layout.packing == PixelPacking::Planar && i ? stride >> layout.chromaX : stride;
        plane.pixels = data + offset;
        for (int k = 0; k < components; k++)
            plane.component_size[k] = layout.bytes * 8;
        if (rgb)
        {
            plane.component_map[0] = layout.vu ? 2 : 0;
            plane.component_map[1] = 1;
            plane.component_map[2] = layout.vu ? 0 : 2;
            if (layout.alpha)
                plane.component_map[3] = 3;
        }
        else if (packed444)
        {
            plane.component_map[0] = 1;
            plane.component_map[1] = 0;
            plane.component_map[2] = 2;
            plane.component_map[3] = 3;
        }
        else if (layout.rgb)
        {
            const int map[] = {1, 2, 0};
            plane.component_map[0] = map[i];
        }
        else
        {
            plane.component_map[0] = i ? (layout.vu ? 3 - i : i) : 0;
            if (semi && i)
            {
                plane.component_map[0] = 1;
                plane.component_map[1] = 2;
            }
        }
        if (!pl_upload_plane(s.d3d->gpu, &s.image.planes[i], &s.planes[i], &plane))
        {
            s.valid = false;
            return E_FAIL;
        }
        s.image.planes[i].flipped = frame.bottomUp;
        offset += plane.row_stride * plane.height;
    }
    if (!layout.rgb && layout.planes > 1)
    {
        for (int i = 1; i < layout.planes; i++)
        {
            float x = 0, y = 0;
            pl_chroma_location_offset(frame.chroma, &x, &y);
            s.image.planes[i].shift_x = layout.chromaX ? x : 0;
            s.image.planes[i].shift_y = layout.chromaY ? y : 0;
        }
    }
    s.valid = true;
    return S_OK;
}

HRESULT PlaceboBackend::Import(const VideoFrame& frame, ID3D11Texture2D* texture, UINT slice)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (!texture)
        return E_POINTER;
    HRESULT hr = s.Ensure();
    if (FAILED(hr))
        return hr;
    ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    if (device.Get() != s.d3d->device)
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    if (slice >= desc.ArraySize || frame.width > static_cast<int>(desc.Width) ||
        frame.height > static_cast<int>(desc.Height))
        return E_INVALIDARG;
    const auto pixel = frame.format;
    const auto type = desc.Format;
    const bool semi = (type == DXGI_FORMAT_NV12 && pixel == PixelFormat::NV12) ||
                      (type == DXGI_FORMAT_P010 && pixel == PixelFormat::P010) ||
                      (type == DXGI_FORMAT_P016 && (pixel == PixelFormat::P016 || pixel == PixelFormat::P012));
    // Semi-planar/planar media subtypes may use a packed DXGI video container.
    // The DXGI type, not the CPU byte layout, defines the native texture views.
    const bool packed = (type == DXGI_FORMAT_YUY2 && pixel == PixelFormat::YUY2) ||
                        (type == DXGI_FORMAT_Y210 && (pixel == PixelFormat::Y210 || pixel == PixelFormat::P210)) ||
                        (type == DXGI_FORMAT_Y216 && (pixel == PixelFormat::Y216 || pixel == PixelFormat::P216));
    const bool y410 = type == DXGI_FORMAT_Y410 && pixel == PixelFormat::Y410;
    const bool y416 = type == DXGI_FORMAT_Y416 && (pixel == PixelFormat::Y416 || pixel == PixelFormat::YUV444P16);
    const bool rgb8 = (type == DXGI_FORMAT_B8G8R8A8_UNORM || type == DXGI_FORMAT_B8G8R8X8_UNORM) &&
                      (pixel == PixelFormat::BGRX32 || pixel == PixelFormat::BGRA32);
    const bool rgb16 = type == DXGI_FORMAT_R16G16B16A16_UNORM &&
                       (pixel == PixelFormat::RGB48 || pixel == PixelFormat::BGR48 || pixel == PixelFormat::BGRA64);
    const auto layout = Layout(pixel);
    if (frame.width <= 0 || frame.height <= 0 || ((frame.width & 1) && layout.chromaX) ||
        ((frame.height & 1) && layout.chromaY) || desc.MipLevels != 1 || desc.SampleDesc.Count != 1)
        return E_INVALIDARG;
    if (!semi && !packed && !y410 && !y416 && !rgb8 && !rgb16)
        return VFW_E_TYPE_NOT_ACCEPTED;
    D3D11_TEXTURE2D_DESC own = {};
    if (s.copy)
        s.copy->GetDesc(&own);
    if (!s.copy || own.Width != desc.Width || own.Height != desc.Height || own.Format != desc.Format)
    {
        s.ClearFrame();
        own = desc;
        own.ArraySize = 1;
        own.MipLevels = 1;
        own.Usage = D3D11_USAGE_DEFAULT;
        own.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        own.CPUAccessFlags = 0;
        own.MiscFlags = 0;
        hr = s.d3d->device->CreateTexture2D(&own, nullptr, &s.copy);
        if (FAILED(hr))
            return hr;
    }
    bool held = false;
    if (s.decoderMutex)
    {
        DWORD wait = WaitForSingleObject(s.decoderMutex, 1000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        held = true;
    }
    s.context->CopySubresourceRegion(
        s.copy.Get(), 0, 0, 0, 0, texture, D3D11CalcSubresource(0, slice, desc.MipLevels), nullptr);
    s.context->Flush();
    if (held)
        ReleaseMutex(s.decoderMutex);
    s.Describe(frame);
    if (semi || packed)
    {
        if (packed)
        {
            hr = s.unpack422.Run(s.d3d, s.context.Get(), s.copy.Get(), type, s.planes);
            if (FAILED(hr))
            {
                s.ClearFrame();
                return hr;
            }
        }
        s.image.num_planes = 2;
        for (int i = 0; i < 2; i++)
        {
            if (!s.planes[i])
            {
                pl_d3d11_wrap_params wrap = {};
                wrap.tex = s.copy.Get();
                wrap.fmt = type == DXGI_FORMAT_NV12 ? (i ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8_UNORM)
                                                    : (i ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R16_UNORM);
                wrap.w = desc.Width / (i ? 2 : 1);
                wrap.h = desc.Height / (i ? 2 : 1);
                s.planes[i] = pl_d3d11_wrap(s.d3d->gpu, &wrap);
                if (!s.planes[i])
                {
                    s.ClearFrame();
                    return E_FAIL;
                }
            }
            auto& plane = s.image.planes[i];
            plane.texture = s.planes[i];
            plane.components = i ? 2 : 1;
            plane.component_mapping[0] = i ? 1 : 0;
            if (i)
                plane.component_mapping[1] = 2;
        }
        s.Chroma();
        if (packed)
            s.image.planes[1].shift_y = 0;
    }
    else
    {
        s.image.num_planes = 1;
        if (!s.planes[0])
        {
            pl_d3d11_wrap_params wrap = {};
            wrap.tex = s.copy.Get();
            if (y410 || y416)
            {
                wrap.fmt = y410 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_UNORM;
                wrap.w = desc.Width;
                wrap.h = desc.Height;
            }
            s.planes[0] = pl_d3d11_wrap(s.d3d->gpu, &wrap);
            if (!s.planes[0])
            {
                s.ClearFrame();
                return E_FAIL;
            }
        }
        auto& plane = s.image.planes[0];
        plane.texture = s.planes[0];
        plane.components = layout.alpha ? 4 : 3;
        plane.component_mapping[0] = y410 || y416 ? 1 : (rgb8 || (rgb16 && pixel != PixelFormat::RGB48)) ? 2 : 0;
        plane.component_mapping[1] = y410 || y416 ? 0 : 1;
        plane.component_mapping[2] = (rgb8 || (rgb16 && pixel != PixelFormat::RGB48)) ? 0 : 2;
        plane.component_mapping[3] = layout.alpha ? 3 : -1;
        if (y410)
        {
            s.image.repr.bits.sample_depth = 10;
            s.image.repr.bits.color_depth = 10;
            s.image.repr.bits.bit_shift = 0;
        }
    }
    s.valid = true;
    return S_OK;
}

HRESULT PlaceboBackend::Present()
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.ApplyMeasurementReset();
    const auto due = s.presentationDue;
    const bool timed = s.presentationTimed;
    s.presentationTimed = false;
    if (!s.valid || !s.window)
        return S_FALSE;
    HRESULT applied = s.ApplySettings();
    if (FAILED(applied))
        return applied;
    const auto monitor = MonitorFromWindow(s.window, MONITOR_DEFAULTTONEAREST);
    const bool nativeWanted =
        s.settings.hdrMode == 1 && s.settings.hdrPassthrough && pl_color_space_is_hdr(&s.image.color);
    if (!nativeWanted || (s.nativeHdr.Monitor() && s.nativeHdr.Monitor() != monitor))
    {
        if (s.nativeHdr.Active())
            Trace("HDR leave monitor=%p next=%p wanted=%d swapchain=%d",
                  s.nativeHdr.Monitor(),
                  monitor,
                  nativeWanted,
                  s.nativeSwapchain.Active());
        if (s.nativeSwapchain.Active())
            s.DestroySwapchain();
        if (!s.nativeHdr.Restore())
            return E_FAIL;
        s.failedNativeMonitor = nullptr;
    }
    if (nativeWanted && s.failedNativeMonitor != monitor && s.nativeHdr.Select(monitor).Supported())
    {
        RECT bounds = {};
        if (!GetClientRect(s.window, &bounds) || bounds.right <= 0 || bounds.bottom <= 0)
            return S_FALSE;
        if (s.swapchain)
            s.DestroySwapchain();
        HRESULT result =
            s.nativeSwapchain.Ensure(s.d3d, s.window, bounds.right, bounds.bottom, s.settings.maximumFramesInFlight);
        if (SUCCEEDED(result) && s.nativeHdr.Activate(monitor, s.image.color))
            return s.PresentNative(due, timed);
        s.failedNativeMonitor = monitor;
        s.nativeSwapchain.Destroy();
        s.nativeHdr.Restore();
    }
    if (!s.swapchain)
    {
        pl_d3d11_swapchain_params params = {};
        params.window = s.window;
        params.disable_10bit_sdr = s.settings.outputFormat == 1;
        params.color_bits = s.settings.outputFormat == 3 ? 16 : s.settings.outputFormat == 2 ? 10 : 0;
        s.swapchain = pl_d3d11_create_swapchain(s.d3d, &params);
        if (!s.swapchain)
            return E_FAIL;
    }
    RECT client;
    GetClientRect(s.window, &client);
    int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0)
    {
        s.presentation.Unavailable();
        return S_FALSE;
    }
    if (width != s.measuredWidth || height != s.measuredHeight)
    {
        s.presentation.Reset(false);
        s.measuredWidth = width;
        s.measuredHeight = height;
    }
    if (!pl_swapchain_resize(s.swapchain, &width, &height))
        return E_FAIL;
    const bool hdr =
        s.settings.hdrMode != 2 && s.settings.hdrPassthrough && s.HdrActive() && pl_color_space_is_hdr(&s.image.color);
    // The swapchain hint selects the signal format, not the tone-map peak.
    // libplacebo 7.360.1 treats SDR hints above 203 nits as HDR and may select
    // PQ on 10-bit buffers. Keep SDR signalling fixed; apply the user's peak
    // only to the render target below, after its SDR transfer is established.
    pl_color_space hint = hdr ? s.image.color : pl_color_space_srgb;
    if (!hdr)
        hint.hdr.max_luma = PL_COLOR_SDR_WHITE;
    if (hdr && s.settings.hdrMode != 1 && s.settings.hdrNits > 0)
        hint.hdr.max_luma = s.settings.hdrNits;
    pl_swapchain_colorspace_hint(s.swapchain, &hint);
    pl_swapchain_frame frame = {};
    if (!pl_swapchain_start_frame(s.swapchain, &frame))
        return E_FAIL;
    pl_frame target = {};
    pl_frame_from_swapchain(&target, &frame);
    if (!hdr)
        target.color.hdr.max_luma = s.SdrTargetNits();
#ifdef PLACEBO_TEST_PRESENT_PIXELS
    if (s.presentColorProbe)
        s.presentColorProbe(target.color);
#endif
    const auto format = target.planes[0].texture->params.format;
    const int textureFormat = format->component_depth[0] * (format->type == PL_FMT_FLOAT ? -1 : 1);
    LARGE_INTEGER begin = {}, end = {}, frequency = {};
    QueryPerformanceFrequency(&frequency);
    s.renderGpu.Begin(s.d3d->device, s.context.Get());
    QueryPerformanceCounter(&begin);
    const bool rendered = s.Render(target, hdr);
    QueryPerformanceCounter(&end);
    s.renderGpu.End(s.context.Get());
    if (rendered)
        s.renderCpu.Add(GetTickCount64(), 1000. * (end.QuadPart - begin.QuadPart) / frequency.QuadPart);
    const bool submitted = pl_swapchain_submit_frame(s.swapchain);
    if (!rendered || !submitted)
        return E_FAIL;
#ifdef PLACEBO_TEST_PRESENT_PIXELS
    s.ProbePresent();
#endif
    QueryPerformanceCounter(&begin);
    pl_swapchain_swap_buffers(s.swapchain);
    QueryPerformanceCounter(&end);
    s.presentCpu.Add(GetTickCount64(), 1000. * (end.QuadPart - begin.QuadPart) / frequency.QuadPart);
    s.MeasurePresentation(due, timed);
    s.outputTextureFormat = textureFormat;
    s.RepaintPending();
    return S_OK;
}

HRESULT
PlaceboBackend::RenderOffscreen(int width, int height, std::vector<uint8_t>& rgba, bool hdrTarget, int textureDepth)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (!s.valid || width <= 0 || height <= 0)
        return E_INVALIDARG;
    HRESULT applied = s.ApplySettings();
    if (FAILED(applied))
        return applied;
    if (s.settings.hdrMode == 2)
        hdrTarget = false;
    if (textureDepth != 8 && textureDepth != 16)
        return E_INVALIDARG;
    pl_fmt format = pl_find_named_fmt(s.d3d->gpu, textureDepth == 16 ? "rgba16" : "rgba8");
    if (!format || !(format->caps & PL_FMT_CAP_RENDERABLE) || !(format->caps & PL_FMT_CAP_HOST_READABLE))
        return E_FAIL;
    pl_tex_params params = {};
    params.w = width;
    params.h = height;
    params.format = format;
    params.renderable = true;
    params.host_readable = true;
    params.blit_dst = true;
    pl_tex tex = pl_tex_create(s.d3d->gpu, &params);
    if (!tex)
        return E_FAIL;
    pl_frame target = {};
    target.num_planes = 1;
    target.planes[0].texture = tex;
    target.planes[0].components = 4;
    for (int i = 0; i < 4; i++)
        target.planes[0].component_mapping[i] = i;
    target.crop = {0.f, 0.f, static_cast<float>(width), static_cast<float>(height)};
    target.color = pl_color_space_srgb;
    target.repr.sys = PL_COLOR_SYSTEM_RGB;
    target.repr.levels = PL_COLOR_LEVELS_FULL;
    target.repr.alpha = PL_ALPHA_INDEPENDENT;
    target.color.hdr.max_luma = s.SdrTargetNits();
    if (hdrTarget)
    {
        target.color = pl_color_space_hdr10;
        target.color.hdr.max_luma = s.settings.hdrNits > 0 ? s.settings.hdrNits : 1000;
    }
    bool ok = s.Render(target, hdrTarget);
    rgba.resize(static_cast<size_t>(width) * height * format->texel_size);
    pl_tex_transfer_params transfer = {};
    transfer.tex = tex;
    transfer.ptr = rgba.data();
    if (ok)
        ok = pl_tex_download(s.d3d->gpu, &transfer);
    pl_tex_destroy(s.d3d->gpu, &tex);
    return ok ? S_OK : E_FAIL;
}

void PlaceboBackend::Flush(bool preserveBlackBars)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (s.swapchain)
        s.MeasurePresentation(0, false);
    s.presentation.Reset(false, true);
    s.presentationTimed = false;
    s.renderCpu.Reset();
    s.presentCpu.Reset();
    s.renderGpu.Reset();
    s.statisticsText.clear();
    if (preserveBlackBars)
        s.blackBars.Discontinuity();
    else
        s.blackBars.Reset();
    s.ClearFrame();
    s.history.clear();
    if (s.renderer)
        pl_renderer_flush_cache(s.renderer);
}

HRESULT PlaceboBackend::TryPresent(bool blackWhenEmpty)
{
    auto& s = *state;
    std::unique_lock guard(s.lock, std::try_to_lock);
    if (!guard.owns_lock())
        return S_FALSE;
    s.presentationTimed = false;
    if (blackWhenEmpty && !s.valid && s.nativeHdr.Active())
    {
        s.DestroySwapchain();
        if (!s.nativeHdr.Restore())
            return E_FAIL;
    }
    if (!blackWhenEmpty || s.valid || !s.swapchain || !s.window)
        return Present();
    // A stopped renderer has no valid image, but still owns a DXGI surface.
    // Clear that surface through the GPU; GDI must not paint over a swapchain.
    // This is an untimed redraw, not another video frame or measurement sample.
    RECT client = {};
    if (!GetClientRect(s.window, &client))
        return HRESULT_FROM_WIN32(GetLastError());
    int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0)
        return S_FALSE;
    if (!pl_swapchain_resize(s.swapchain, &width, &height))
        return E_FAIL;
    pl_swapchain_frame frame = {};
    if (!pl_swapchain_start_frame(s.swapchain, &frame))
        return E_FAIL;
    const float black[4] = {0.f, 0.f, 0.f, 1.f};
    pl_tex_clear(s.d3d->gpu, frame.fbo, black);
    if (!pl_swapchain_submit_frame(s.swapchain))
        return E_FAIL;
#ifdef PLACEBO_TEST_PRESENT_PIXELS
    s.ProbePresent();
#endif
    pl_swapchain_swap_buffers(s.swapchain);
    return S_OK;
}
#ifdef PLACEBO_TEST_PRESENT_PIXELS
void PlaceboBackend::SetPresentProbeForTest(std::function<void(ID3D11Texture2D*)> probe)
{
    state->presentProbe = std::move(probe);
}

void PlaceboBackend::SetPresentColorProbeForTest(std::function<void(const pl_color_space&)> probe)
{
    state->presentColorProbe = std::move(probe);
}
#endif
// Before the swapchain exists, GDI owns the loading background. Once DXGI
// presents, leave its surface alone; never wait for the rendering thread here.
bool PlaceboBackend::PaintLoadingBackground(HDC dc)
{
    auto& s = *state;
    std::unique_lock guard(s.lock, std::try_to_lock);
    if (!guard.owns_lock() || s.swapchain || s.nativeSwapchain.Active() || !s.window || !dc)
        return false;
    RECT client = {};
    if (!GetClientRect(s.window, &client))
        return false;
    return FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))) != 0;
}

void PlaceboBackend::SetGeometry(const RECT& source, const RECT& target)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.source = source;
    s.targetRect = target;
    for (auto& entry : s.history)
    {
        auto& f = *entry.second;
        f.image.crop =
            IsRectEmpty(&source)
                ? pl_rect2df{0, 0, float(f.description.width), float(f.description.height)}
                : pl_rect2df{float(source.left), float(source.top), float(source.right), float(source.bottom)};
    }
    if (s.valid)
    {
        const auto saved = s.image;
        s.Describe(s.description);
        for (int i = 0; i < saved.num_planes; i++)
            s.image.planes[i] = saved.planes[i];
        s.image.field = saved.field;
        s.image.first_field = saved.first_field;
        s.image.prev = saved.prev;
        s.image.next = saved.next;
        if (s.mixing)
            for (auto& f : s.mixFrames)
                f.crop = s.image.crop;
    }
}

ID3D11Device* PlaceboBackend::Device() const
{
    return state->d3d ? state->d3d->device : nullptr;
}

std::string PlaceboBackend::LastError() const
{
    return state->error;
}

int PlaceboBackend::OutputTextureFormat() const
{
    return state->outputTextureFormat.load();
}

int PlaceboBackend::OutputLevels() const
{
    return state->outputLevels.load();
}

int PlaceboBackend::OutputDitherDepth() const
{
    return state->outputDitherDepth.load();
}

int PlaceboBackend::SharpenState() const
{
    return state->sharpenState.load();
}

void PlaceboBackend::ShowStatistics(bool enabled)
{
    state->showStatistics = enabled;
}

bool PlaceboBackend::StatisticsVisible() const
{
    return state->showStatistics.load();
}

void PlaceboBackend::SetSourceCodec(const std::string& codec)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (s.sourceCodec != codec)
    {
        s.sourceCodec = codec;
        s.statisticsText.clear();
    }
}

void PlaceboBackend::SetStatisticsInput(double fps, bool direct, int dropped)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.statisticsFps = fps;
    s.statisticsDirect = direct;
    s.statisticsDropped = dropped;
    std::lock_guard dropGuard(s.dropsLock);
    s.drops.Observe(dropped);
}

void PlaceboBackend::RestartDropMeasurement(int rawBeforeReset)
{
    auto& s = *state;
    std::lock_guard guard(s.dropsLock);
    s.drops.Observe(rawBeforeReset);
    s.drops.RestartSource();
}

std::wstring PlaceboBackend::StatisticsText() const
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    return s.statisticsText;
}

void PlaceboBackend::SetPresentationTiming(int64_t due, bool running)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.presentationDue = due;
    s.presentationTimed = running && due > 0;
}

void PlaceboBackend::ResetPresentationMeasurement(bool counters)
{
    state->measurementReset.fetch_or(counters ? 2u : 1u);
}

MeasurementSnapshot PlaceboBackend::Measurements() const
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.ApplyMeasurementReset();
    MeasurementSnapshot out;
    out.feedback = s.presentation.feedback;
    out.timing = s.presentation.timing;
    out.countersAvailable = s.presentation.haveCounters;
    out.drops = s.DropTotal();
    out.pending = s.presentation.pending;
    out.submissions = s.presentation.submissions;
    out.confirmed = s.presentation.confirmed;
    out.repeats = s.presentation.repeatedRefreshes;
    out.glitches = s.presentation.glitches;
    out.resets = s.presentation.resets;
    const auto now = GetTickCount64();
    out.renderCpuSamples = s.renderCpu.count;
    out.renderGpuSamples = s.renderGpu.duration.count;
    out.presentCpuSamples = s.presentCpu.count;
    out.renderCpuAverage = s.renderCpu.Average();
    out.renderCpuMax = s.renderCpu.Maximum(now);
    out.renderGpuAverage = s.renderGpu.duration.Average();
    out.renderGpuMax = s.renderGpu.duration.Maximum(now);
    out.presentCpuAverage = s.presentCpu.Average();
    out.presentCpuMax = s.presentCpu.Maximum(now);
    return out;
}

int PlaceboBackend::CacheLoadedObjects() const
{
    return state->shaderCache.Loaded();
}

int PlaceboBackend::CacheObjects() const
{
    return state->shaderCache.Objects();
}

BlackBarStatus PlaceboBackend::BlackBars() const
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    BlackBarStatus out;
    out.active = s.blackBars.detector.Bounds(s.description.width, s.description.height);
    out.measured = s.blackBars.detector.measured;
    out.failed = s.blackBars.failed;
    out.protectingSubtitles = s.blackBars.detector.subtitleProtection;
    out.samples = s.blackBars.detector.samples;
    return out;
}
