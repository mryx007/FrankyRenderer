#include "DolbyVision/Transport.h"
// libplacebo integration uses only its public API. No existing MPC pixel path.
#include "Renderer/Backend.h"
#include "Renderer/BlackBarAnalysis.h"
#include "Renderer/GpuPacked422.h"
#include "Renderer/NativeHdrDisplay.h"
#include "Renderer/NativeHdrSwapchain.h"
#include "Renderer/ColorManagement.h"
#include "Settings/SettingsChoices.h"
#include "Settings/HdrTuning.h"
#include "Renderer/StatisticsBitmap.h"
#include "Renderer/ImageAnalysis.h"
#include "Renderer/PresentationMeasurement.h"
#include "Renderer/GpuMeasurement.h"
#include "Renderer/RenderProbe.h"
#include "Renderer/MotionTiming.h"
#include "Renderer/ShaderCache.h"
#include "Renderer/ShaderWarmup.h"
#include <deque>
#include <filesystem>
#include <sstream>
#include <iomanip>
#pragma comment(lib, "gdi32.lib")
#include "Renderer/SharpenShader.h"
#include "Renderer/ShaderHooks.h"
#include "Renderer/RtxScaling.h"
#include "Renderer/NvidiaAppSettings.h"
#include "Renderer/RenderOptions.h"
#include "Renderer/TonalHook.h"
#include "Renderer/OutputGammaHook.h"
#include <libplacebo/shaders/custom.h>
#include <charconv>
#include "Renderer/Trace.h"
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
        pl_frame enhancement = {};
        VideoFrame description;
        pl_tex planes[4] = {};
        pl_tex enhancementPlanes[3] = {};
        ComPtr<ID3D11Texture2D> copy;

        ~StoredFrame()
        {
            for (auto& plane : planes)
                pl_tex_destroy(gpu, &plane);
            for (auto& plane : enhancementPlanes)
                pl_tex_destroy(gpu, &plane);
        }
    };

    std::unordered_map<uint64_t, std::unique_ptr<StoredFrame>> history;
    uint64_t nextFrame = 1;
    uint64_t rtxSerial = 0, rtxKey = 0;
    RtxScaling rtx;
    NvidiaAppSettings nvidiaApp;
    // The picture being drawn was upscaled by RTX (statistics).
    bool rtxUpscaled = false;
    // RTX read the decoder picture itself: the driver scaled its chroma.
    bool rtxDirect = false;
    pl_frame mixFrames[16] = {};
    // Gamma for the frame being presented. SDR gamma processing: the gamma
    // x100 the SDR picture should have on the display (0 = off). Output
    // gamma: power raising the encoded output to a pure-power display gamma
    // between libplacebo's fixed curves (1 = none).
    int sdrGammaCurve = 0;
    float outputGamma = 1;
    OutputGammaHook outputGammaHook;
    uint64_t mixSignatures[16] = {};
    double mixTimes[16] = {}, mixDuration = 0;
    int mixCount = 0;
    bool mixing = false;
    // Untimed presents (paused, repaints) show the current frame alone; a
    // Smooth Motion blend of two frames would stay on screen as a ghost.
    bool stillPicture = false;
    pl_deinterlace_params deinterlace = pl_deinterlace_default_params;
    TonalHook tonalHook;
    const pl_hook* userHooks[ShaderCount] = {};
    ShaderConfig userCached[ShaderCount];
    bool userFailed[ShaderCount] = {};
    // Shaders compiled on a worker device, once per session; see ShaderWarmup.h.
    std::shared_ptr<ShaderWarmupJob> warmups[ShaderCount];
    bool warmupMerged[ShaderCount] = {}, warmupReported[ShaderCount] = {};
    uint64_t warmupStarted[ShaderCount] = {};

    LUID AdapterLuid() const
    {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc = {};
        if (d3d && SUCCEEDED(d3d->device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
            SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&desc)))
            return desc.AdapterLuid;
        return {};
    }

    // False while the worker still compiles: this frame runs without the shader.
    bool ShaderReady(int id)
    {
        auto& job = warmups[id];
        if (!job)
        {
            const auto layout = Layout(description.format);
            job = std::make_shared<ShaderWarmupJob>();
            job->source = LoadUserShaderSource(id);
            job->cache = shaderCache.Snapshot();
            job->adapter = AdapterLuid();
            job->featureLevel = d3d ? d3d->device->GetFeatureLevel() : D3D_FEATURE_LEVEL(0);
            job->width = description.width;
            job->height = description.height;
            job->deep = layout.depth > 8;
            job->rgb = layout.rgb;
            job->notify = window;
            warmupMerged[id] = warmupReported[id] = false;
            warmupStarted[id] = GetTickCount64();
            Trace("Shader warm-up start: %s, feature level %x, video %dx%d deep=%d rgb=%d, cache %zu bytes",
                  ShaderCatalog[id].file,
                  unsigned(job->featureLevel),
                  job->width,
                  job->height,
                  int(job->deep),
                  int(job->rgb),
                  job->cache.size());
            StartShaderWarmup(job);
        }
        const int state = job->state.load();
        // The statistics show the shader as preparing meanwhile.
        if (state == 0)
            return false;
        if (!warmupReported[id])
        {
            warmupReported[id] = true;
            Trace("Shader warm-up %s: %s after %llu ms",
                  state == 1 ? "done" : "FAILED, compiling during playback",
                  ShaderCatalog[id].file,
                  GetTickCount64() - warmupStarted[id]);
        }
        if (state == 1 && !warmupMerged[id])
        {
            warmupMerged[id] = true;
            shaderCache.Import(job->cache);
            std::vector<uint8_t>().swap(job->cache);
        }
        return true;
    }

    std::wstring userShaderMessage;
    // Content profile in use, or why none applies; shown in the statistics.
    std::wstring contentProfile;

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
        const pl_hook* hook = ParseUserShader(d3d->gpu, id, c);
        if (!hook)
        {
            Trace("Shader failed to load: %s", ShaderCatalog[id].file);
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
    // Shortcut feedback shown at the top centre until messageUntil.
    StatisticsBitmap messageBitmap;
    std::mutex messageLock;
    std::wstring message;
    uint64_t messageUntil = 0;
    ImageAnalysis imageAnalysis;
    std::atomic<bool> showStatistics = false;
    std::string sourceCodec;
    double statisticsFps = 0;
    bool statisticsDirect = false;
    int statisticsDropped = -1;
    std::wstring statisticsText;
    bool statisticsShown = false;
    ULONGLONG statisticsUpdated = 0;
    PresentationMeasurement presentation;
    DropMeasurement drops;
    std::mutex dropsLock;
    DurationMeasurement renderCpu, presentCpu;
    RenderProbe renderProbe;
    GpuMeasurement renderGpu;
    uint64_t traceGpuCount = 0, traceSummary = 0, traceFrames = 0;
    double traceCpuMax = 0, traceGpuMax = 0;

    // Opt-in diagnostics: render peaks and presentation queue every 10 s.
    void TraceRender(uint64_t now)
    {
        if (!TraceEnabled())
            return;
        ++traceFrames;
        traceCpuMax = std::max(traceCpuMax, renderCpu.last);
        if (renderGpu.duration.count != traceGpuCount)
        {
            traceGpuCount = renderGpu.duration.count;
            traceGpuMax = std::max(traceGpuMax, renderGpu.duration.last);
        }
        if (!traceSummary)
            traceSummary = now;
        if (now - traceSummary < 10000)
            return;
        Trace("Render summary %llu frames, peak %.2f ms CPU / %.2f ms GPU, present peak %.2f ms, "
              "queued 0/1/2/3+ %llu/%llu/%llu/%llu, ticks left out %llu drift / %llu queue",
              static_cast<unsigned long long>(traceFrames),
              traceCpuMax,
              traceGpuMax,
              presentCpu.Maximum(now),
              static_cast<unsigned long long>(traceQueued[0]),
              static_cast<unsigned long long>(traceQueued[1]),
              static_cast<unsigned long long>(traceQueued[2]),
              static_cast<unsigned long long>(traceQueued[3]),
              static_cast<unsigned long long>(driftSkips),
              static_cast<unsigned long long>(backlog.skipped));
        std::fill(std::begin(traceQueued), std::end(traceQueued), 0);
        traceSummary = now;
        traceFrames = 0;
        traceCpuMax = traceGpuMax = 0;
    }

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
            if (value & 4)
            {
                renderCpu.Reset();
                renderGpu.duration.Reset();
                renderGpu.skipped = renderGpu.disjoint = 0;
                presentCpu.Reset();
            }
            statisticsText.clear();
            statisticsUpdated = 0;
        }
    }

    int64_t presentationDue = 0;
    int64_t presentationMediaTime = -1;
    bool presentationTimed = false;
    int presentationLead = 0;
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

    // Windows HDR switch, shared with the window thread.
    std::atomic<int> displayHdrInput = 0, displayHdrState = -1;
    std::atomic_bool displayHdrRebuild = false;
    std::function<void()> displayHdrListener;

    ComPtr<IDXGISwapChain> OutputSwapchain()
    {
        ComPtr<IDXGISwapChain> output;
        if (nativeSwapchain.Active())
            nativeSwapchain.Get()->QueryInterface(IID_PPV_ARGS(&output));
        else if (swapchain)
            output.Attach(pl_d3d11_swapchain_unwrap(swapchain));
        return output;
    }

    PresentBacklog backlog;
    uint64_t driftSkips = 0, traceQueued[4] = {};

    bool SkipForBacklog(int refreshLead)
    {
        // Drift is noticed from scanout latency before the queue deepens; the
        // queue depth remains the safety net when latency feedback is missing.
        // Refresh-locked ticks already absorb drift and vary by design within
        // one refresh of their due time, which is not a surplus. Late ticks
        // are left out before they queue, so only a blocking queue counts.
        if (presentation.drift.TakeSurplus() && !refreshLead)
        {
            backlog.Reset();
            ++driftSkips;
            Trace("Smooth Motion tick left out: reference clock ahead of scanout (total %llu)",
                  static_cast<unsigned long long>(driftSkips));
            return true;
        }
        ComPtr<IDXGISwapChain> native = OutputSwapchain();
        UINT submitted = 0;
        DXGI_FRAME_STATISTICS shown = {};
        if (!native || FAILED(native->GetLastPresentCount(&submitted)) || FAILED(native->GetFrameStatistics(&shown)) ||
            !shown.PresentCount || submitted < shown.PresentCount)
        {
            backlog.Reset();
            return false;
        }
        // Statistics stop advancing while nothing is composed (file change,
        // occlusion); a stale PresentCount is not a queue.
        LARGE_INTEGER now = {}, frequency = {};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        const double period = measuredPeriod > 0 ? measuredPeriod : 1e6;
        if (now.QuadPart - shown.SyncQPCTime.QuadPart > 4 * period * frequency.QuadPart / 1e7)
        {
            backlog.Reset();
            return false;
        }
        const UINT queued = submitted - shown.PresentCount;
        ++traceQueued[std::min<UINT>(queued, 3)];
        if (!backlog.Skip(queued, refreshLead ? refreshLead + 2 : 2))
            return false;
        Trace("Smooth Motion tick left out: %u presents queued ahead of scanout (total %llu)",
              queued,
              static_cast<unsigned long long>(backlog.skipped));
        return true;
    }

    void DestroySwapchain()
    {
        nativeSwapchain.Destroy();
        pl_swapchain_destroy(&swapchain);
        presentation.Reset(false);
        presentationTimed = false;
        periodUpdated = 0;
        outputQueried = 0;
        measuredWidth = measuredHeight = 0;
        lastSyncQpc = 0;
        lastSyncRefresh = 0;
        stableCadence = true;
        backlog.Reset();
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
    pl_frame felEnhancement = {};
    pl_tex felPlanes[3] = {};
    std::vector<uint8_t> unpacked;
    ComPtr<ID3D11Texture2D> copy;
    // CPU memory NV12/P010 for RTX, written here and imported like a decoder texture.
    // Several, used in turn: writing one never waits for the GPU's copy of the last.
    ComPtr<ID3D11Texture2D> semiPlanarUploads[3];
    int semiPlanarNext = 0;
    bool semiPlanarUploadTraced = false;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIDevice1> latencyDevice;
    UINT originalLatency = 0;
    bool latencyChanged = false;
    // Highest Smooth Motion lead the frame latency allows without blocking.
    std::atomic<int> presentCeiling = 2;
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
    std::atomic<bool> resizeHold = false, resizeResumed = false;
    int rotation = 0;
    bool flip = false;
    bool valid = false;
    std::string error;

    static void Log(void* opaque, pl_log_level level, const char* message)
    {
        auto self = static_cast<State*>(opaque);
        if (level <= PL_LOG_ERR)
            self->error = message;
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        Trace("libplacebo: %s", message);
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
        if (d3d)
            for (auto& plane : felPlanes)
                pl_tex_destroy(d3d->gpu, &plane);
        felEnhancement = {};
        copy.Reset();
        unpack422.ResetFrame();
        image = {};
        valid = false;
    }

    // Copies an NV12 or P010 picture from CPU memory into one texture with
    // both planes, as a hardware decoder delivers it.
    HRESULT StageSemiPlanar(const VideoFrame& frame, const uint8_t* data, int stride, ID3D11Texture2D*& staged)
    {
        auto& semiPlanarUpload = semiPlanarUploads[semiPlanarNext];
        semiPlanarNext = (semiPlanarNext + 1) % int(std::size(semiPlanarUploads));
        D3D11_TEXTURE2D_DESC have = {};
        if (semiPlanarUpload)
            semiPlanarUpload->GetDesc(&have);
        HRESULT hr = S_OK;
        const auto format = frame.format == PixelFormat::NV12 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010;
        if (!semiPlanarUpload || have.Width != UINT(frame.width) || have.Height != UINT(frame.height) ||
            have.Format != format)
        {
            semiPlanarUpload.Reset();
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = frame.width;
            desc.Height = frame.height;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = d3d->device->CreateTexture2D(&desc, nullptr, &semiPlanarUpload);
            if (FAILED(hr))
                return hr;
        }
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = context->Map(semiPlanarUpload.Get(), 0, D3D11_MAP_WRITE, 0, &mapped);
        if (FAILED(hr))
            return hr;
        const auto out = static_cast<uint8_t*>(mapped.pData);
        const size_t row = size_t(frame.width) * (frame.format == PixelFormat::NV12 ? 1 : 2);
        for (int y = 0; y < frame.height; ++y)
            memcpy(out + size_t(y) * mapped.RowPitch, data + size_t(y) * stride, row);
        // The chroma plane follows the luma rows of the mapped texture.
        const auto chroma = out + size_t(mapped.RowPitch) * frame.height;
        const auto source = data + size_t(stride) * frame.height;
        for (int y = 0; y < frame.height / 2; ++y)
            memcpy(chroma + size_t(y) * mapped.RowPitch, source + size_t(y) * stride, row);
        context->Unmap(semiPlanarUpload.Get(), 0);
        staged = semiPlanarUpload.Get();
        return S_OK;
    }

    void ClearOverlay()
    {
        if (d3d)
            pl_tex_destroy(d3d->gpu, &overlay);
        overlayView.Reset();
        overlayTexture.Reset();
    }

    // Smooth Motion presenting more than two refreshes ahead was requested
    // explicitly, like a manual frames-in-flight value; queue one more.
    int FramesInFlight() const
    {
        return std::max(settings.maximumFramesInFlight, settings.presentAhead > 2 ? settings.presentAhead + 1 : 0);
    }

    HRESULT ConfigureLatency()
    {
        if (!d3d)
            return S_OK;
        const int requested = FramesInFlight();
        if (!latencyDevice)
        {
            // Auto must not touch a decoder-owned device. Capture its original
            // value only when the user first requests a manual override.
            if (!requested)
            {
                ComPtr<IDXGIDevice1> device;
                UINT current = 0;
                if (SUCCEEDED(d3d->device->QueryInterface(IID_PPV_ARGS(&device))) &&
                    SUCCEEDED(device->GetMaximumFrameLatency(&current)))
                    presentCeiling = std::max(1, int(current) - 1);
                return S_OK;
            }
            ComPtr<IDXGIDevice1> device;
            HRESULT hr = d3d->device->QueryInterface(IID_PPV_ARGS(&device));
            if (FAILED(hr))
                return hr;
            hr = device->GetMaximumFrameLatency(&originalLatency);
            if (FAILED(hr))
                return hr;
            latencyDevice = std::move(device);
        }
        const UINT wanted = requested ? static_cast<UINT>(requested) : originalLatency;
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
        presentCeiling = std::max(1, int(wanted) - 1);
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
        rtx.Reset();
        unpack422.Reset();
        renderGpu.Reset();
        renderCpu.Reset();
        renderProbe.Reset();
        presentCpu.Reset();
        presentation.Reset(false);
        statisticsBitmap.Reset();
        messageBitmap.Reset();
        imageAnalysis.Reset();
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
            warmups[i].reset();
        }
        userShaderMessage.clear();
        pl_d3d11_destroy(&d3d);
        context.Reset();
        shaderCache.Save();
    }

    // The D3D11 texture behind a picture, when it is one decoder picture.
    ID3D11Texture2D* NativePicture(const pl_frame& frame) const
    {
        const auto texture = frame.planes[0].texture;
        if (!texture)
            return nullptr;
        if (copy && planes[0] == texture)
            return copy.Get();
        for (const auto& [id, stored] : history)
            if (stored->copy && stored->image.planes[0].texture == texture)
                return stored->copy.Get();
        return nullptr;
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
        saved->enhancement = felEnhancement;
        if (image.enhancement_layer)
            saved->image.enhancement_layer = &saved->enhancement;
        saved->description = description;
        saved->copy = std::move(copy);
        for (int i = 0; i < 4; i++)
            saved->planes[i] = std::exchange(planes[i], nullptr);
        for (int i = 0; i < 3; i++)
            saved->enhancementPlanes[i] = std::exchange(felPlanes[i], nullptr);
        // Buffered unpacked planes own their output textures. The packed input
        // copy is no longer needed once dispatch has consumed it.
        if (unpack422.HasFrame())
            saved->copy.Reset();
        unpack422.ResetFrame();
        const uint64_t id = nextFrame++;
        history.emplace(id, std::move(saved));
        image = {};
        felEnhancement = {};
        valid = false;
        return id;
    }

    template <class Import> HRESULT Capture(Import&& import, uint64_t& id)
    {
        std::lock_guard guard(lock);
        id = 0;
        auto displayed = image;
        const auto displayedRtxKey = rtxKey;
        const auto displayedDescription = description;
        const bool wasValid = valid, wasMixing = mixing;
        HRESULT hr;
        try
        {
            // Transfer directly owned planes before importing the next frame.
            // Both BL and EL pointers must refer to their stable history owner.
            if (valid && (copy || planes[0]))
            {
                const auto held = StoreCurrent();
                displayed = history.at(held)->image;
            }
            hr = import();
            if (SUCCEEDED(hr))
                id = StoreCurrent();
        }
        catch (const std::bad_alloc&)
        {
            hr = E_OUTOFMEMORY;
        }
        if (FAILED(hr))
        {
            ClearFrame();
            return hr;
        }
        image = displayed;
        rtxKey = displayedRtxKey;
        description = displayedDescription;
        valid = wasValid;
        mixing = wasMixing;
        for (auto it = history.begin(); it != history.end();)
            if (it->first + static_cast<uint64_t>(2 * std::ceil(settings.motionRadius) + 4) < id &&
                !DisplayReferences(*it->second))
                it = history.erase(it);
            else
                ++it;
        return hr;
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

    HRESULT EnsureDevice()
    {
        if (!d3d)
        {
            pl_d3d11_params params = pl_d3d11_default_params;
            params.min_feature_level = D3D_FEATURE_LEVEL_11_0;
            params.flags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
            d3d = pl_d3d11_create(log, &params);
            if (!d3d)
                return E_FAIL;
        }
        return S_OK;
    }

    HRESULT Ensure()
    {
        if (FAILED(EnsureDevice()))
            return E_FAIL;
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
            // Switching the analysis adds or removes its hooks: start over and
            // render cached frames again. Any other change only needs a new
            // sample; libplacebo renders again by itself when its parameters
            // change, and peak detection and Smooth Motion keep their state.
            if (previous.imageAnalysis != settings.imageAnalysis)
            {
                imageAnalysis.Reset();
                if (renderer)
                    pl_renderer_flush_cache(renderer);
            }
            else if (settings.imageAnalysis)
                imageAnalysis.Refresh();
            const bool scalingChanged = previous.scaling.has_value() != settings.scaling.has_value() ||
                                        (previous.scaling && settings.scaling &&
                                         memcmp(&*previous.scaling, &*settings.scaling, sizeof(ScalingSettings)));
            if (!settings.rtxSuperResolution && !settings.rtxHdr)
                rtx.Reset();
            else if (previous.rtxSuperResolution != settings.rtxSuperResolution || previous.rtxHdr != settings.rtxHdr ||
                     previous.deinterlacing != settings.deinterlacing ||
                     previous.chromaUpscaler != settings.chromaUpscaler || scalingChanged)
                rtx.Invalidate();
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
        if (HRESULT hr = BuildRenderOptions(options, settings); FAILED(hr))
            return hr;
        deinterlace.algo = settings.deinterlacing == 2 ? PL_DEINTERLACE_YADIF : PL_DEINTERLACE_BWDIF;
        options->params.deinterlace_params = settings.deinterlacing ? &deinterlace : nullptr;
        return S_OK;
    }

    bool PrepareSharpen()
    {
        if (sharpenHook && sharpenCached == settings.sharpenStrength)
            return true;
        const pl_hook* next = ParseSharpenShader(d3d->gpu, settings.sharpenStrength);
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
        const int64_t timeline =
            presentationMediaTime >= 0
                ? static_cast<int64_t>(std::llround(double(presentationMediaTime) * frequency.QuadPart / 1e7))
                : due;
        presentation.Submit(submitted, due, timed, timeline);
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
        // Markup for StatisticsRasterizer: headings, label/value rows, colours.
        const auto heading = [&](const wchar_t* title)
        {
            if (text.tellp() > 0)
                text << L"\n";
            text << L'\x01' << title;
        };
        const auto row = [&](const wchar_t* label) -> std::wostream& { return text << L"\n"
                                                                                   << label << L'\t'; };
        const auto colored = [&](const wchar_t* label, wchar_t color) -> std::wostream&
        { return row(label) << L'\x02' << color; };
        constexpr wchar_t dot[] = L" · ", arrow[] = L" → ";

        heading(L"Display");
        if (period > 0 || haveDisplay)
        {
            auto& value = row(L"Signal");
            if (period > 0)
                value << std::setprecision(3) << 1e7 / period << L" Hz";
            if (haveDisplay)
            {
                value << (period > 0 ? dot : L"") << display.BitsPerColor << L" bit ";
                if (display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                    value << L"RGB full" << dot << L"Windows HDR";
                else if (display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ||
                         display.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)
                    value << L"RGB full";
                else
                    value << L"DXGI color space " << display.ColorSpace;
            }
        }
        {
            auto& value = row(L"Output");
            value << output.planes[0].texture->params.format->component_depth[0] << L" bit "
                  << (output.planes[0].texture->params.format->type == PL_FMT_FLOAT ? L"float" : L"integer") << dot
                  << (outputLevels == 2 ? L"TV levels" : L"PC levels") << dot << L"dither ";
            if (outputDitherDepth)
                value << outputDitherDepth.load() << L" bit";
            else
                value << L"auto / float";
        }
        row(L"Window") << (window && IsZoomed(GetAncestor(window, GA_ROOT)) ? L"D3D11 maximized"
                                                                            : L"D3D11 windowed / borderless");
        if (settings.hdrMode == 1 && !nativeHdr.Status().empty())
            row(L"HDR output") << wide(nativeHdr.Status());

        heading(L"Video");
        const auto layout = Layout(description.format);
        row(L"Source") << (sourceCodec.empty() ? L"" : wide(sourceCodec) + arrow) << wide(layout.name) << dot
                       << layout.depth << L" bit "
                       << (layout.rgb                           ? L"RGB"
                           : !layout.chromaX && !layout.chromaY ? L"4:4:4"
                           : layout.chromaY                     ? L"4:2:0"
                                                                : L"4:2:2");
        if (statisticsFps > 0 || period > 0)
        {
            auto& value = row(L"Timing");
            if (statisticsFps > 0)
                value << std::setprecision(3) << statisticsFps << L" fps (" << std::setprecision(2)
                      << 1000 / statisticsFps << L" ms)";
            if (period > 0)
                value << (statisticsFps > 0 ? dot : L"") << L"vsync " << std::setprecision(2) << period / 1e4 << L" ms";
        }
        row(L"Size") << description.width << L"×" << description.height << arrow << int(output.crop.x1 - output.crop.x0)
                     << L"×" << int(output.crop.y1 - output.crop.y0) << dot << std::setprecision(2)
                     << double(description.aspectX > 0 ? description.aspectX : description.width) /
                            (description.aspectY > 0 ? description.aspectY : description.height)
                     << L":1";
        row(L"Crop") << int(image.crop.x0) << L"," << int(image.crop.y0) << L"," << int(image.crop.x1) << L","
                     << int(image.crop.y1) << arrow << int(output.crop.x0) << L"," << int(output.crop.y0) << L","
                     << int(output.crop.x1) << L"," << int(output.crop.y1);
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
        row(L"Color") << L"matrix " << wide(matrix) << dot << prim(description.color.primaries) << arrow
                      << prim(output.color.primaries) << dot
                      << (description.levels == PL_COLOR_LEVELS_LIMITED ? L"limited range"
                          : description.levels == PL_COLOR_LEVELS_FULL  ? L"full range"
                                                                        : L"unknown range");
        {
            // Dolby Vision status lines "Name: value" become rows; the
            // processing state is yellow unless active.
            std::wostringstream dv;
            AppendDolbyVisionStatus(
                dv, description.dvSource, bool(image.repr.dovi), description.dvLayered, output.color);
            std::wistringstream lines(dv.str());
            for (std::wstring line; std::getline(lines, line);)
            {
                const auto colon = line.find(L": ");
                if (colon == std::wstring::npos)
                    continue;
                const auto label = line.substr(0, colon), value = line.substr(colon + 2);
                if (label == L"Dolby Vision")
                    (value.find(L"processing active") != std::wstring::npos &&
                             value.find(L"not processed") == std::wstring::npos
                         ? row(L"Dolby Vision")
                         : colored(L"Dolby Vision", L'y'))
                        << value;
                else
                    row(label == L"Source" ? L"Format" : label.c_str()) << value;
            }
        }
        if (pl_color_space_is_hdr(&description.color))
        {
            const bool dovi = bool(image.repr.dovi),
                       plus = pl_hdr_metadata_contains(&description.color.hdr, PL_HDR_METADATA_HDR10PLUS);
            if (!description.dvSource.present && !dovi)
                row(L"HDR") << (plus ? L"HDR10+" : description.color.transfer == PL_COLOR_TRC_HLG ? L"HLG" : L"HDR10");
            pl_hdr_metadata measured = {};
            const bool haveMeasured = pl_renderer_get_hdr_metadata(renderer, &measured);
            if ((std::isfinite(description.color.hdr.max_luma) && description.color.hdr.max_luma > 0) || haveMeasured)
            {
                auto& value = row(L"Peak");
                if (std::isfinite(description.color.hdr.max_luma) && description.color.hdr.max_luma > 0)
                    value << std::setprecision(0) << description.color.hdr.max_luma << L" nits source";
                if (haveMeasured)
                    value << (description.color.hdr.max_luma > 0 ? dot : L"") << std::setprecision(1)
                          << pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, measured.max_pq_y) << L" nits measured";
            }
            if (!actualTone.empty() && actualTone != "n/a")
            {
                auto& value = row(L"Tone mapping");
                value << wide(actualTone);
                if (std::isfinite(actualTarget) && actualTarget > 0)
                    value << arrow << std::setprecision(0) << actualTarget << L" nits";
            }
        }

        heading(L"Processing");
        // libplacebo names a filter built from parts "custom"; spell out its
        // preset, kernel and window, e.g. "custom (ewa_lanczos: jinc / jinc, EWA)".
        // role indexes ScalingSettings::filters for the preset it started from.
        const auto filterName = [&](const pl_filter_config* f, const char* none, int role) -> std::wstring
        {
            if (!f)
                return wide(none);
            if (!f->name || strcmp(f->name, "custom"))
                return wide(f->name ? f->name : none);
            std::wstring detail;
            if (settings.scaling && strcmp(settings.scaling->filters[role].preset, "auto"))
                detail = wide(settings.scaling->filters[role].preset) + L": ";
            detail += wide(f->kernel && f->kernel->name ? f->kernel->name : "?");
            if (f->window && f->window->name)
                detail += L" / " + wide(f->window->name);
            if (f->polar)
                detail += L", EWA";
            return L"custom (" + detail + L")";
        };
        const bool chromaPlane = options->params.plane_upscaler != nullptr;
        const auto* chromaFilter = chromaPlane ? options->params.plane_upscaler : options->params.upscaler;
        if (rtxDirect)
            row(L"Chroma") << L"upscale" << dot << L"NVIDIA driver (RTX)";
        else
            row(L"Chroma") << L"upscale" << dot << filterName(chromaFilter, "bilinear", chromaPlane ? 2 : 0);
        const bool down = output.crop.x1 - output.crop.x0 < image.crop.x1 - image.crop.x0 ||
                          output.crop.y1 - output.crop.y0 < image.crop.y1 - image.crop.y0;
        const bool same = output.crop.x1 - output.crop.x0 == image.crop.x1 - image.crop.x0 &&
                          output.crop.y1 - output.crop.y0 == image.crop.y1 - image.crop.y0;
        row(L"Image") << (same && rtxUpscaled ? L"RTX Super Resolution"
                          : same              ? L"native size"
                          : down              ? L"downscale"
                                              : L"upscale")
                      << (same ? L""
                               : dot + filterName(down ? options->params.downscaler : options->params.upscaler,
                                                  "bilinear",
                                                  down ? 1 : 0));
        {
            auto& value = row(L"Smooth motion");
            if (mixing && settings.smoothMotion)
                value << L"on (" << filterName(options->params.frame_mixer, "none", 4) << L")";
            else
                value << (settings.smoothMotion ? L"off (not needed / no frame mix)" : L"off (settings)");
        }
        row(L"Deinterlacing") << (image.field == PL_FIELD_NONE  ? L"off (progressive / not active)"
                                  : settings.deinterlacing == 2 ? L"Yadif"
                                  : settings.deinterlacing == 1 ? L"Bwdif"
                                                                : L"off (settings)");
        row(L"Debanding") << (settings.deband ? L"on" : L"off");
        row(L"Sharpening") << L"adaptive sharpen "
                           << (sharpenState == 1   ? L"on"
                               : sharpenState == 2 ? L"bypassed"
                                                   : L"off");
        {
            auto& value = row(L"RTX Video");
            const bool appOff =
                (!settings.rtxSuperResolution || nvidiaApp.SuperResolution() == NvidiaAppSettings::State::Off) &&
                (!settings.rtxHdr || nvidiaApp.TrueHdr() == NvidiaAppSettings::State::Off);
            if ((settings.rtxSuperResolution || settings.rtxHdr) && appOff)
                value << L"off in the NVIDIA app";
            else
            {
                value << rtx.Status();
                if (settings.rtxSuperResolution || settings.rtxHdr)
                    value << dot << L"quality: NVIDIA driver; submitted " << rtx.Evaluations();
            }
        }
        if (settings.blackBars.enabled)
        {
            const RECT r = blackBars.detector.Bounds(description.width, description.height);
            auto& value = row(L"Black bars");
            if (blackBars.failed)
                value << L"analysis unavailable (uncropped)";
            else if (!blackBars.detector.measured)
                value << L"detecting";
            else
                value << L"L " << r.left << L", T " << r.top << L", R " << description.width - r.right << L", B "
                      << description.height - r.bottom << L" px" << dot
                      << (settings.blackBars.crop ? L"crop on" : L"detect only");
        }
        {
            // A long chain continues on further rows so the box fits the picture.
            std::wstring shaders;
            const wchar_t* label = L"Shaders";
            const auto finishRow = [&]
            {
                row(label) << shaders;
                label = L"";
                shaders.clear();
            };
            for (int id : ShaderChainOrder(settings.userShaders, settings.shaderAutoOrder))
            {
                if (!settings.userShaders.shaders[id].enabled || !ShaderListed(id))
                    continue;
                std::wstring name = ShaderCatalog[id].label;
                if (warmups[id] && !warmups[id]->state.load())
                    name += L" (preparing)";
                if (!shaders.empty() && shaders.size() + name.size() > 70)
                {
                    shaders += L",";
                    finishRow();
                }
                shaders += (shaders.empty() ? L"" : L", ") + name;
            }
            if (!shaders.empty())
                finishRow();
            if (!userShaderMessage.empty())
                colored(L"Shaders", L'y') << userShaderMessage;
        }
        row(L"Profile") << (contentProfile.empty() ? L"Default" : contentProfile);
        // Which 3D LUT applies: the configured one or the one from the video's folder.
        if (settings.lutEnabled && colors && colors->lut)
            row(L"3D LUT") << std::filesystem::path(settings.lutPath).filename().wstring();
        row(L"Transfer") << (statisticsDirect ? L"D3D11 texture (GPU direct)" : L"CPU memory → GPU upload");

        heading(L"Performance");
        // Time available per rendered picture: one refresh with Smooth Motion,
        // otherwise one video frame (one field when deinterlacing).
        const double budget = mixing && settings.smoothMotion && period > 0 ? period / 1e4
                              : statisticsFps > 0 ? 1000 / statisticsFps / (image.field != PL_FIELD_NONE ? 2 : 1)
                                                  : period / 1e4;
        // Show recent sustained render load. A single expensive cache build
        // must not dominate the display for five seconds as a peak did.
        const auto render = [&]()
        {
            if (!renderCpu.count && !renderGpu.duration.count)
                return;
            double slowest = 0;
            std::wostringstream value;
            value.imbue(std::locale::classic());
            value << std::fixed << std::setprecision(2);
            if (renderCpu.count)
            {
                slowest = renderCpu.RecentAverage(now);
                value << slowest << L" ms CPU";
            }
            if (renderGpu.duration.count)
            {
                const double gpu = renderGpu.duration.RecentAverage(now);
                slowest = std::max(slowest, gpu);
                value << (renderCpu.count ? dot : L"") << gpu << L" ms GPU";
            }
            // CPU and GPU work overlap; the slower one must fit the budget.
            // Plain text: actual problems show in the Drops row.
            if (budget > 0)
            {
                const double load = 100 * slowest / budget;
                value << dot << std::setprecision(0) << load << L"% of " << std::setprecision(2) << budget << L" ms"
                      << (load > 100   ? L", over budget"
                          : load >= 80 ? L", near limit"
                                       : L"");
            }
            row(L"Render") << value.str();
        };
        render();
        if (const auto* peak = renderProbe.Peak(now))
        {
            int first = 0, second = 1;
            if (peak->ms[second] > peak->ms[first])
                std::swap(first, second);
            for (int i = 2; i < RenderProbe::Count; ++i)
                if (peak->ms[i] > peak->ms[first])
                {
                    second = first;
                    first = i;
                }
                else if (peak->ms[i] > peak->ms[second])
                    second = i;
            row(L"CPU peak work") << std::setprecision(2) << RenderProbe::Names[first] << L" " << peak->ms[first]
                                  << L" ms" << dot << RenderProbe::Names[second] << L" " << peak->ms[second] << L" ms";
        }
        if (presentCpu.count)
            row(L"Present") << std::setprecision(2) << presentCpu.Average() << L" ms" << dot << L"peak "
                            << presentCpu.Maximum(now) << L" ms (CPU call)";
        if (presentation.feedback)
        {
            auto& value = row(L"Queue");
            value << presentation.pending;
            UINT latency = 0;
            ComPtr<IDXGIDevice1> dxgi;
            if (SUCCEEDED(d3d->device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
                SUCCEEDED(dxgi->GetMaximumFrameLatency(&latency)))
                value << L" / " << latency << L" (DXGI pending / limit)";
            else
                value << L" (DXGI pending)";
        }
        if (statisticsDropped >= 0 || presentation.haveCounters)
        {
            // Only the counts that are not zero are coloured.
            const auto drops = statisticsDropped >= 0 ? DropTotal() : 0;
            const auto mark = [](bool problem, const wchar_t* code) { return problem ? code : L"\x02w"; };
            auto& value = row(L"Drops");
            if (statisticsDropped >= 0)
                value << mark(drops > 0, L"\x02r") << drops << L" dropped" << L"\x02w";
            if (presentation.haveCounters)
                value << (statisticsDropped >= 0 ? dot : L"") << mark(presentation.repeatedRefreshes > 0, L"\x02y")
                      << presentation.repeatedRefreshes << L" repeated (extra refresh)" << L"\x02w" << dot
                      << mark(presentation.glitches > 0, L"\x02y") << presentation.glitches << L" late (>1 vsync)"
                      << L"\x02w";
        }
        row(L"DXGI timing") << presentation.confirmed << L" confirmed" << dot << presentation.resets << L" resets ("
                            << (presentation.timing ? L"active" : L"collecting") << L")";
        statisticsText = text.str();
    }

    void Describe(const VideoFrame& frame)
    {
        rtxKey = (uint64_t(1) << 63) | (++rtxSerial << 2);
        const auto layout = Layout(frame.format);
        if (frame.width != description.width || frame.height != description.height ||
            frame.format != description.format || frame.color.transfer != description.color.transfer)
            Trace("Video %dx%d %s %d bit, transfer %d, primaries %d",
                  frame.width,
                  frame.height,
                  layout.name,
                  layout.depth,
                  int(frame.color.transfer),
                  int(frame.color.primaries));
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

    // Peak luminance Windows reports for the display in HDR mode, in nits; 0 if
    // unknown or not in HDR mode. On some laptops it follows the brightness slider.
    // What Windows reports for the display: read at most once a second (and
    // at once on another monitor or swapchain), since GetDesc1 is slow on
    // some drivers and these are asked for every frame.
    ULONGLONG outputQueried = 0;
    HMONITOR outputMonitor = nullptr;
    bool outputHdr = false;
    float outputPeak = 0;
    // Set when Windows HDR was switched: read again on the next frame.
    std::atomic<bool> outputStale = false;

    void QueryOutput()
    {
        const auto now = GetTickCount64();
        const auto monitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : nullptr;
        if (!outputStale.exchange(false) && outputQueried && now - outputQueried < 1000 && monitor == outputMonitor)
            return;
        outputQueried = now;
        outputMonitor = monitor;
        outputHdr = false;
        outputPeak = 0;
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput6> output6;
        // unwrap returns an owned reference. Retaining it prevents replacement
        // of the flip-model swapchain on this HWND after an HDR mode change.
        ComPtr<IDXGISwapChain> native;
        native = OutputSwapchain();
        DXGI_OUTPUT_DESC1 desc = {};
        if (!native || FAILED(native->GetContainingOutput(&output)) || FAILED(output.As(&output6)) ||
            FAILED(output6->GetDesc1(&desc)) || desc.ColorSpace != DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
            return;
        outputHdr = true;
        outputPeak = std::isfinite(desc.MaxLuminance) && desc.MaxLuminance >= 80 ? desc.MaxLuminance : 0;
    }

    float DisplayPeak()
    {
        if (!swapchain)
            return 0;
        QueryOutput();
        return outputPeak;
    }

    bool HdrActive()
    {
        if (!swapchain)
            return false;
        if (const int known = displayHdrState.load(); known >= 0)
            return known != 0;
        QueryOutput();
        return outputHdr;
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
        renderProbe.Record(GetTickCount64(), renderCpu.last);
        TraceRender(GetTickCount64());
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

    bool Render(pl_frame& target, bool hdrOutput = false, bool nativeOutput = false, bool windowGeometry = true)
    {
        RenderProbe::Scope probe(renderProbe, showStatistics.load());
        blackBars.Analyze(d3d, context.Get(), image, description.width, description.height, settings.blackBars, &probe);
        probe.Next(RenderProbe::Setup);
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
            pl_rotation rotation;
            pl_rotation rotations[16];
            int count;

            RestoreCrops(pl_frame& i, pl_frame* m, int n)
                : image(i), mix(m), main(i.crop), rotation(i.rotation), count(n)
            {
                for (int k = 0; k < n; k++)
                {
                    saved[k] = m[k].crop;
                    rotations[k] = m[k].rotation;
                }
            }

            ~RestoreCrops()
            {
                image.crop = main;
                image.rotation = rotation;
                for (int k = 0; k < count; k++)
                {
                    mix[k].crop = saved[k];
                    mix[k].rotation = rotations[k];
                }
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
        if (windowGeometry && !IsRectEmpty(&targetRect))
            target.crop = {static_cast<float>(targetRect.left),
                           static_cast<float>(targetRect.top),
                           static_cast<float>(targetRect.right),
                           static_cast<float>(targetRect.bottom)};
        const float originalAspect = description.aspectX > 0 && description.aspectY > 0
                                         ? static_cast<float>(description.aspectX) / description.aspectY
                                         : static_cast<float>(description.width) / description.height;
        float aspect = cropBars ? originalAspect * ((image.crop.x1 - image.crop.x0) / description.width) /
                                      ((image.crop.y1 - image.crop.y0) / description.height)
                                : originalAspect;
        // With automatic crop the renderer chooses the frame, but an aspect ratio
        // the player set (an override in MPC-HC or MPC-BE) still applies: its
        // rectangle then has another shape than the video. A player that only
        // reports the whole window sets no aspect ratio.
        if (autoZoom && windowGeometry && !IsRectEmpty(&targetRect) && rotation % 180 == 0)
        {
            const auto* texture = target.planes[0].texture;
            const bool wholeWindow = texture && targetRect.left == 0 && targetRect.top == 0 &&
                                     targetRect.right == texture->params.w && targetRect.bottom == texture->params.h;
            if (!wholeWindow)
                aspect *= float(targetRect.right - targetRect.left) / float(targetRect.bottom - targetRect.top) /
                          originalAspect;
        }
        if (rotation % 180)
            aspect = 1.f / aspect;
        // With automatic crop, fit the active image into the complete output;
        // the player's old destination can still include encoded letterboxing.
        if (autoZoom)
            target.crop = {0, 0, float(target.planes[0].texture->params.w), float(target.planes[0].texture->params.h)};
        float width = target.crop.x1 - target.crop.x0, height = target.crop.y1 - target.crop.y0;
        const bool fill = autoZoom && settings.blackBars.zoom == 1 && !blackBars.detector.subtitleProtection;
        // MPC-HC and MPC-BE compute the video rectangle themselves, with the
        // aspect ratio, an override, stretching and zoom, like for MPC Video
        // Renderer; draw exactly into it. The renderer fits the picture itself
        // only when it decides the frame: automatic crop, its own rotation, or
        // no rectangle from the player. libplacebo clips a zoomed rectangle
        // that reaches past the window.
        const bool playerRectangle = windowGeometry && !IsRectEmpty(&targetRect) && !autoZoom && rotation % 180 == 0;
        if (playerRectangle)
        {
        }
        else if ((width / height > aspect) != fill)
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

        struct RestoreProcessed
        {
            pl_frame& image;
            pl_frame* mix;
            pl_frame original;
            std::vector<pl_frame> originals;
            bool active = false;

            ~RestoreProcessed()
            {
                if (active)
                {
                    image = original;
                    std::copy(originals.begin(), originals.end(), mix);
                }
            }
        } processed{image, mixFrames, image};

        std::vector<std::shared_ptr<RtxScaling::Frame>> rtxFrames;
        rtxUpscaled = rtxDirect = false;
        probe.Next(RenderProbe::Rtx);
        // The switches only take effect when the NVIDIA app has the feature on;
        // otherwise the driver would just scale plainly instead of libplacebo.
        const bool rtxUpscale =
            settings.rtxSuperResolution && nvidiaApp.SuperResolution() != NvidiaAppSettings::State::Off;
        // RTX Video HDR: an SDR source on HDR output becomes HDR10 in the driver.
        const bool rtxHdr = settings.rtxHdr && nvidiaApp.TrueHdr() != NvidiaAppSettings::State::Off && hdrOutput &&
                            !pl_color_space_is_hdr(&image.color);
        if (rtxUpscale || rtxHdr)
        {
            int outW = int(std::lround(target.crop.x1 - target.crop.x0));
            int outH = int(std::lround(target.crop.y1 - target.crop.y0));
            // The largest picture: the whole target. A crop that follows black
            // bars or a zoom then only moves the driver's rectangles.
            int targetW = target.planes[0].texture ? target.planes[0].texture->params.w : outW;
            int targetH = target.planes[0].texture ? target.planes[0].texture->params.h : outH;
            // A player zoom can make the rectangle larger than the window; the
            // driver need not produce more than twice the window.
            outW = std::min(outW, 2 * targetW);
            outH = std::min(outH, 2 * targetH);
            if (rotation % 180)
            {
                std::swap(outW, outH);
                std::swap(targetW, targetH);
            }
            auto current = rtx.Get(d3d,
                                   context.Get(),
                                   image,
                                   rtxKey,
                                   outW,
                                   outH,
                                   options->params,
                                   rtxUpscale,
                                   rtxHdr,
                                   &restore.main,
                                   targetW,
                                   targetH,
                                   NativePicture(image));
            bool ready = bool(current);
            if (current)
                rtxFrames.push_back(std::move(current));
            for (int i = 0; ready && mixing && i < mixCount; ++i)
            {
                auto mixed = rtx.Get(d3d,
                                     context.Get(),
                                     mixFrames[i],
                                     mixSignatures[i],
                                     outW,
                                     outH,
                                     options->params,
                                     rtxUpscale,
                                     rtxHdr,
                                     &restore.saved[i],
                                     targetW,
                                     targetH,
                                     NativePicture(mixFrames[i]));
                ready = bool(mixed);
                if (mixed)
                    rtxFrames.push_back(std::move(mixed));
            }
            if (ready)
            {
                processed.active = true;
                rtxUpscaled = rtx.Upscaling();
                rtxDirect = rtx.DecoderPicture();
                processed.originals.assign(mixFrames, mixFrames + mixCount);
                image = rtxFrames[0]->image;
                if (mixing)
                    for (int i = 0; i < mixCount; ++i)
                        mixFrames[i] = rtxFrames[i + 1]->image;
                // The driver maps to the brightness set in the NVIDIA app; treat
                // its output as mastered for this display so it is not tone mapped.
                auto display = [&](pl_frame& frame)
                {
                    if (!pl_color_space_is_hdr(&frame.color) || !rtxHdr)
                        return;
                    frame.color.hdr = {};
                    frame.color.hdr.max_luma = target.color.hdr.max_luma > 0 ? target.color.hdr.max_luma : 1000;
                    frame.color.hdr.min_luma = target.color.hdr.min_luma;
                };
                display(image);
                for (int i = 0; mixing && i < mixCount; ++i)
                    display(mixFrames[i]);
                // RTX delivers whole pixels of the rounded size. Put the picture
                // on whole pixels of that size, so libplacebo shows it 1:1
                // instead of resampling it by a fraction of a pixel.
                auto snap = [](float& a, float& b)
                {
                    const bool reversed = b < a;
                    const float low = std::min(a, b), size = std::round(std::abs(b - a));
                    const float start = std::round(low);
                    a = reversed ? start + size : start;
                    b = reversed ? start : start + size;
                };
                snap(target.crop.x0, target.crop.x1);
                snap(target.crop.y0, target.crop.y1);
            }
        }
        // Apply after preprocessing, to the main image and every temporal sample.
        probe.Next(RenderProbe::Hooks);
        // MPC-HC's SetFlip convention mirrors the source horizontally.
        auto transform = [&](pl_frame& frame)
        {
            frame.rotation = static_cast<pl_rotation>(rotation / 90);
            if (flip)
                std::swap(frame.crop.x0, frame.crop.x1);
        };
        transform(image);
        for (int i = 0; i < mixCount; ++i)
            transform(mixFrames[i]);
        pl_color_adjustment adjustment = pl_color_adjustment_neutral;
        if (settings.colorAdjustment)
        {
            adjustment.saturation = settings.saturation;
            adjustment.contrast = settings.contrast;
            adjustment.brightness = settings.brightness;
            adjustment.gamma = settings.gamma;
            adjustment.hue = settings.hue;
            adjustment.temperature = settings.temperature;
        }
        // SDR gamma processing: the picture is decoded as gamma 2.2 after
        // this power on its signal, so its light follows the chosen gamma.
        if (sdrGammaCurve)
            adjustment.gamma *= 220.f / float(sdrGammaCurve);
        pl_render_params params = options->params;
        params.color_adjustment = settings.colorAdjustment || sdrGammaCurve ? &adjustment : nullptr;
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

        const pl_hook* hooks[ShaderCount + 5] = {};
        int hookIds[ShaderCount] = {};
        int users = 0, count = 0;
        if (params.num_hooks)
            hooks[count++] = sharpenHook;
        tonalHook.settings = settings.tonal;
        if (ActiveTonal(settings.tonal) && pl_color_space_is_hdr(&image.color))
            hooks[count++] = &tonalHook.hook;
        // A target LUT is applied before output hooks and already describes
        // the display.
        if (outputGamma != 1 && !target.lut)
            hooks[count++] = outputGammaHook.Prepare(outputGamma, target.repr);
        const pl_hook* analysisHooks[2] = {};
        int analysisCount = 0;
        if (settings.imageAnalysis)
        {
            // Include geometry so resizing/cropping a paused frame refreshes
            // its sample without repeatedly analysing the same image.
            uint64_t sampleKey = rtxKey;
            for (int v : {target.planes[0].texture->params.w,
                          target.planes[0].texture->params.h,
                          int(target.crop.x0),
                          int(target.crop.y0),
                          int(target.crop.x1),
                          int(target.crop.y1),
                          rotation,
                          int(flip)})
                sampleKey = (sampleKey ^ uint64_t(v)) * 0x100000001b3ULL;
            imageAnalysis.Begin(d3d, context.Get(), window, sampleKey, mixing && settings.smoothMotion && mixCount > 1);
            analysisHooks[analysisCount++] = imageAnalysis.Observer();
            if (const auto* ramp = imageAnalysis.Ramp(target.color, target.repr))
                analysisHooks[analysisCount++] = ramp;
        }
        const int builtins = count;
        userShaderMessage.clear();
        for (int id : ShaderChainOrder(settings.userShaders, settings.shaderAutoOrder))
        {
            // A custom shader whose file was deleted simply drops out.
            if (!settings.userShaders.shaders[id].enabled || !ShaderListed(id))
            {
                pl_mpv_user_shader_destroy(&userHooks[id]);
                userCached[id] = {};
                userFailed[id] = false;
                continue;
            }
            if (PrepareUserShader(id))
            {
                if (!ShaderReady(id))
                    continue;
                hooks[count++] = userHooks[id];
                hookIds[users++] = id;
                // SSimDownscaler is tuned for gamma light (mpv: linear-downscaling=no);
                // in linear light it overshoots into ringing.
                const float source = pl_rect_w(image.crop) * pl_rect_h(image.crop),
                            output = pl_rect_w(target.crop) * pl_rect_h(target.crop);
                if (!strcmp(ShaderCatalog[id].file, "SSimDownscaler_RT.glsl") && std::abs(output) < std::abs(source))
                    params.disable_linear_scaling = true;
            }
            else
                userShaderMessage = std::wstring(L"shader failed: ") + ShaderCatalog[id].label;
        }
        // libplacebo turns off a shader that does not compile on this GPU.
        const auto renderErrors = pl_renderer_get_errors(renderer);
        for (int i = 0; i < renderErrors.num_disabled_hooks; i++)
            for (int n = 0; n < users; n++)
                if (userHooks[hookIds[n]]->signature == renderErrors.disabled_hooks[i])
                    userShaderMessage = std::wstring(L"shader failed: ") + ShaderCatalog[hookIds[n]].label;
        // Observe after user filters; inject the test ramp last, immediately
        // before libplacebo's output dithering and target overlays.
        for (int i = 0; i < analysisCount; ++i)
            hooks[count++] = analysisHooks[i];
        params.hooks = hooks;
        params.num_hooks = count;
        auto timedDraw = [&](auto&& draw)
        {
            // A render that compiles a shader stops playback; say which.
            const uint64_t begin = GetTickCount64();
            const size_t cached = pl_cache_size(shaderCache.Handle());
            const bool drawn = draw();
            const uint64_t took = GetTickCount64() - begin;
            if (took > 500)
            {
                std::string names;
                for (int n = 0; n < users; n++)
                    names += std::string(n ? ", " : "") + ShaderCatalog[hookIds[n]].file;
                Trace("Slow render %llu ms, shader cache %zu -> %zu bytes, shaders: %s",
                      took,
                      cached,
                      pl_cache_size(shaderCache.Handle()),
                      names.empty() ? "none" : names.c_str());
            }
            return drawn;
        };
        auto renderWithRecovery = [&](auto&& draw)
        {
            if (timedDraw(draw))
                return true;
            if (!users)
                return false;
            for (int n = 0; n < users; n++)
                userFailed[hookIds[n]] = true;
            userShaderMessage = L"custom shaders bypassed after a render error";
            for (int i = 0; i < analysisCount; ++i)
                hooks[builtins + i] = analysisHooks[i];
            params.hooks = hooks;
            params.num_hooks = builtins + analysisCount;
            pl_renderer_flush_cache(renderer);
            return draw();
        };
        pl_color_map_params dynamicMap =
            params.color_map_params ? *params.color_map_params : pl_color_map_default_params;
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
            if (settings.toneMapping == "auto" && image.color.hdr.ootf.target_luma > 0)
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
        pl_overlay overlays[4] = {};
        pl_overlay_part parts[4] = {};
        auto& overlayInfo = overlays[0];
        auto& part = parts[0];

        probe.Next(RenderProbe::PlayerOverlay);
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
        const bool statistics = showStatistics.load();
        // The visible picture inside the window, for statistics placed on it.
        const float viewW = float(target.planes[0].texture->params.w),
                    viewH = float(target.planes[0].texture->params.h);
        const pl_rect2df picture = {std::clamp(std::min(target.crop.x0, target.crop.x1), 0.f, viewW),
                                    std::clamp(std::min(target.crop.y0, target.crop.y1), 0.f, viewH),
                                    std::clamp(std::max(target.crop.x0, target.crop.x1), 0.f, viewW),
                                    std::clamp(std::max(target.crop.y0, target.crop.y1), 0.f, viewH)};
        const pl_rect2df* placement = settings.statisticsInVideo ? &picture : nullptr;
        probe.Next(RenderProbe::StatisticsInfo);
        // The overlay box keeps its largest size while shown; start over each
        // time the statistics are switched on.
        if (statistics && !statisticsShown)
            statisticsBitmap.Reset();
        statisticsShown = statistics;
        if (statistics)
        {
            UpdateStatistics(target);
            probe.Next(RenderProbe::StatisticsUpload);
            if (statisticsBitmap.Update(d3d->gpu,
                                        statisticsText,
                                        target.planes[0].texture->params.w,
                                        target.planes[0].texture->params.h,
                                        window))
            {
                const int index = target.num_overlays;
                statisticsBitmap.Describe(overlays[index], parts[index], placement);
                target.overlays = overlays;
                target.num_overlays = index + 1;
            }
        }
        {
            std::wstring note;
            {
                std::lock_guard guard(messageLock);
                if (GetTickCount64() < messageUntil)
                    note = message;
            }
            if (note.empty())
            {
                if (messageBitmap.Width())
                    messageBitmap.Reset();
            }
            else if (messageBitmap.Update(d3d->gpu, note, int(viewW), int(viewH), window) && messageBitmap.Width())
            {
                // Top centre of the window, or of the picture like the statistics.
                const float left = placement ? placement->x0 : 0, right = placement ? placement->x1 : viewW;
                const float top =
                    placement ? OverlayTop(placement->y0 + 16, placement->y1, float(messageBitmap.Height())) : 16.f;
                const int index = target.num_overlays;
                messageBitmap.DescribeAt(overlays[index],
                                         parts[index],
                                         std::max(0.f, (left + right - float(messageBitmap.Width())) / 2),
                                         top);
                target.overlays = overlays;
                target.num_overlays = index + 1;
            }
        }
        probe.Next(RenderProbe::Analysis);
        if (settings.imageAnalysis)
        {
            const int index = target.num_overlays;
            if (imageAnalysis.Describe(overlays[index],
                                       parts[index],
                                       target.planes[0].texture->params.w,
                                       target.planes[0].texture->params.h,
                                       placement))
            {
                target.overlays = overlays;
                target.num_overlays = index + 1;
            }
        }
        probe.Next(RenderProbe::Draw);
        pl_frame shown = image;
        pl_frame gammaFrames[16];
        if (sdrGammaCurve)
            shown.color.transfer = PL_COLOR_TRC_GAMMA22;
        if (mixing && settings.smoothMotion && !stillPicture)
        {
            const pl_frame* frames[16] = {};
            for (size_t i = 0; i < static_cast<size_t>(mixCount); i++)
            {
                gammaFrames[i] = mixFrames[i];
                if (sdrGammaCurve)
                    gammaFrames[i].color.transfer = PL_COLOR_TRC_GAMMA22;
                frames[i] = &gammaFrames[i];
            }
            pl_frame_mix mix = {};
            mix.num_frames = mixCount;
            if (mix.num_frames == 2 && mixSignatures[0] == mixSignatures[1])
                mix.num_frames = 1;
            mix.frames = frames;
            uint64_t signatures[16];
            for (int i = 0; i < mixCount; i++)
                signatures[i] =
                    mixSignatures[i] ^ (blackBars.generation * 0x9e3779b97f4a7c15ULL) ^
                    (uint64_t(rotation + int(flip)) * 0xd6e8feb86659fd93ULL) ^
                    (uint64_t(sdrGammaCurve) * 0xbf58476d1ce4e5b9ULL) ^
                    (uint64_t(outputGamma * 1e6f) * 0x2545f4914f6cdd1dULL) ^
                    (processed.active
                         ? uint64_t(reinterpret_cast<uintptr_t>(mixFrames[i].planes[0].texture)) * 0x94d049bb133111ebULL
                         : 0);
            mix.signatures = signatures;
            mix.timestamps = mixTimes;
            mix.vsync_duration = mixDuration;
            if (!settings.scaling)
                params.frame_mixer = &pl_filter_oversample;
            // libplacebo reads the oversample threshold from its constant kernel,
            // so leave out frames below the configured share here, weighting
            // them exactly as libplacebo does: the visible part of the refresh.
            double kept[16];
            if (settings.motionThreshold > 0 && mix.num_frames > 1 && params.frame_mixer &&
                params.frame_mixer->kernel == &pl_filter_function_oversample)
            {
                int count = 0;
                for (int i = 0; i < mix.num_frames; i++)
                {
                    const double start = std::max(mixTimes[i], 0.),
                                 end = std::min(i + 1 < mix.num_frames ? mixTimes[i + 1] : INFINITY, mixDuration);
                    if (end > start && (end - start) / mixDuration < settings.motionThreshold)
                        continue;
                    frames[count] = frames[i];
                    signatures[count] = signatures[i];
                    kept[count++] = mixTimes[i];
                }
                if (count)
                {
                    mix.num_frames = count;
                    mix.timestamps = kept;
                }
            }
            return renderWithRecovery([&]() { return pl_render_image_mix(renderer, &mix, &target, &params); });
        }
        return renderWithRecovery([&]() { return pl_render_image(renderer, &shown, &target, &params); });
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
    return state->Capture([&] { return Upload(frame, bytes, length); }, id);
}

HRESULT PlaceboBackend::CaptureImport(const VideoFrame& frame, ID3D11Texture2D* texture, UINT slice, uint64_t& id)
{
    return state->Capture([&] { return Import(frame, texture, slice); }, id);
}

HRESULT PlaceboBackend::CaptureDolbyVision(const VideoFrame& frame, IDvLayerSample* sample, uint64_t& id)
{
    return state->Capture([&] { return ImportDolbyVision(frame, sample); }, id);
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
    s.rtxKey = current * 4 + static_cast<unsigned>(field);
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
            s.mixTimes[s.mixCount++] = time;
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
    if (!ValidExtras(settings.extras))
        return E_INVALIDARG;
    if (!ValidCombinedMapping(settings))
        return E_INVALIDARG;
    if (settings.hdrMode < 1 || settings.hdrMode > 3)
        return E_INVALIDARG;
    float radius = 0;
    if (settings.scaling && !ValidScaling(*settings.scaling, &radius))
        return E_INVALIDARG;
    if (settings.maximumFramesInFlight < 0 || settings.maximumFramesInFlight > 16)
        return E_INVALIDARG;
    if (settings.presentAhead < 0 || settings.presentAhead > 4)
        return E_INVALIDARG;
    if ((settings.displayGammaCurve != 0 && settings.displayGammaCurve != 1 &&
         !ValidGammaCurve(settings.displayGammaCurve)) ||
        (settings.sdrGammaCurve != 0 && !ValidGammaCurve(settings.sdrGammaCurve)) || settings.displayContrast < 0 ||
        settings.displayContrast > 1000000 || settings.displayPrimaries < 0 || settings.displayPrimaries > 5)
        return E_INVALIDARG;
    if (!(settings.motionThreshold >= 0 && settings.motionThreshold <= .5f))
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
    if (settings.displayDepth != 0 && settings.displayDepth != 4 && settings.displayDepth != 5 &&
        settings.displayDepth != 6 && settings.displayDepth != 8 && settings.displayDepth != 10 &&
        settings.displayDepth != 12 && settings.displayDepth != 16)
        return E_INVALIDARG;
    if (settings.iccIntent < 0 || settings.iccIntent > 1)
        return E_INVALIDARG;
    if ((settings.hdrNits != 0 && !range(settings.hdrNits, 80, 10000)) || !range(settings.antiringing, 0, 1))
        return E_INVALIDARG;
    if (settings.debandIterations < 0 || settings.debandIterations > 16 || settings.ditherMethod < 0 ||
        settings.ditherMethod >= int(std::size(DitherChoices)) || !range(settings.debandThreshold, 0, 1000) ||
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
    // With RTX, an NV12 or P010 picture from CPU memory becomes one texture, as a
    // hardware decoder delivers it, so the driver can read it as it is (see
    // RtxScaling). Planes as before if that fails.
    ID3D11Texture2D* staged = nullptr;
    if ((frame.format == PixelFormat::P010 || frame.format == PixelFormat::NV12) && !frame.bottomUp &&
        (s.settings.rtxSuperResolution || s.settings.rtxHdr) &&
        SUCCEEDED(s.StageSemiPlanar(frame, data, stride, staged)) && SUCCEEDED(Import(frame, staged, 0)))
    {
        if (!s.semiPlanarUploadTraced)
        {
            s.semiPlanarUploadTraced = true;
            Trace("NV12/P010 from CPU memory uploaded as one texture for RTX");
        }
        return S_OK;
    }
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
        // Typed copies per sample instead of a memcpy call for each byte group.
        auto split = [&](auto sample)
        {
            using T = decltype(sample);
            const int y0 = layout.vu ? 1 : 0, y1 = layout.vu ? 3 : 2, u = layout.vu ? 0 : 1, v = layout.vu ? 2 : 3;
            for (int y = 0; y < frame.height; y++)
            {
                T src[4];
                const auto row = data + static_cast<size_t>(y) * stride;
                auto dstY = reinterpret_cast<T*>(s.unpacked.data() + static_cast<size_t>(y) * frame.width * c);
                auto dstUV = reinterpret_cast<T*>(s.unpacked.data() + luma + static_cast<size_t>(y) * frame.width * c);
                for (int x = 0; x < frame.width; x += 2)
                {
                    memcpy(src, row + static_cast<size_t>(x) * c * 2, sizeof(src));
                    dstY[x] = src[y0];
                    dstY[x + 1] = src[y1];
                    dstUV[x] = src[u];
                    dstUV[x + 1] = src[v];
                }
            }
        };
        if (c == 2)
            split(uint16_t());
        else
            split(uint8_t());
        data = s.unpacked.data();
        stride = frame.width * c;
        layout.packing = PixelPacking::SemiPlanar;
    }
    else if (layout.packing == PixelPacking::PackedRGB && layout.channels == 3)
    {
        const int c = layout.bytes;
        s.unpacked.resize(static_cast<size_t>(frame.width) * frame.height * 4 * c);
        // Typed copies per sample instead of a memcpy and memset call per pixel.
        auto widen = [&](auto sample)
        {
            using T = decltype(sample);
            const T opaque = T(~T(0));
            for (int y = 0; y < frame.height; y++)
            {
                const auto row = data + static_cast<size_t>(y) * stride;
                auto dst = reinterpret_cast<T*>(s.unpacked.data() + static_cast<size_t>(y) * frame.width * 4 * c);
                for (int x = 0; x < frame.width; x++)
                {
                    T src[3];
                    memcpy(src, row + static_cast<size_t>(x) * 3 * c, sizeof(src));
                    dst[4 * x] = src[0];
                    dst[4 * x + 1] = src[1];
                    dst[4 * x + 2] = src[2];
                    dst[4 * x + 3] = opaque;
                }
            }
        };
        if (c == 2)
            widen(uint16_t());
        else
            widen(uint8_t());
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
        // The driver's video processor reads NV12/P010 copies directly for
        // RTX; its input views need a render target or decoder texture.
        own.BindFlags = D3D11_BIND_SHADER_RESOURCE | (semi ? D3D11_BIND_RENDER_TARGET : 0);
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
    const int lead = s.presentationLead;
    s.stillPicture = !timed;
    s.presentationTimed = false;
    s.presentationLead = 0;
    if (s.resizeHold)
        return S_FALSE;
    if (s.resizeResumed.exchange(false))
        s.presentation.Reset(false);
    if (!s.valid || !s.window)
        return S_FALSE;
    HRESULT applied = s.ApplySettings();
    if (FAILED(applied))
        return applied;
    if (timed && s.mixing && s.settings.smoothMotion && s.SkipForBacklog(lead))
        return S_FALSE;
    const auto monitor = MonitorFromWindow(s.window, MONITOR_DEFAULTTONEAREST);
    const int hdrInput = 1 | (pl_color_space_is_hdr(&s.image.color) ? 2 : 0) |
                         (s.settings.rtxHdr && s.nvidiaApp.TrueHdr() != NvidiaAppSettings::State::Off ? 4 : 0);
    if (s.displayHdrInput.exchange(hdrInput) != hdrInput && s.displayHdrListener)
        s.displayHdrListener();
    if (s.displayHdrRebuild.exchange(false) && s.swapchain)
        s.DestroySwapchain();
    // Turning Windows HDR on replaces the driver session: Windows then knows
    // about HDR and keeps the picture right under overlapping windows.
    const bool nativeWanted = s.settings.hdrMode == 1 && s.settings.hdrPassthrough && !s.settings.windowsHdrToggle &&
                              pl_color_space_is_hdr(&s.image.color);
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
        HRESULT result = s.nativeSwapchain.Ensure(s.d3d, s.window, bounds.right, bounds.bottom, s.FramesInFlight());
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
    // RTX Video HDR outputs HDR10 for SDR sources while Windows HDR is on.
    const bool rtxHdr = s.settings.rtxHdr && s.nvidiaApp.TrueHdr() != NvidiaAppSettings::State::Off && s.HdrActive() &&
                        !pl_color_space_is_hdr(&s.image.color) && s.rtx.HdrUsable() && s.image.num_planes;
    const bool hdr = rtxHdr || (s.settings.hdrMode != 2 && s.settings.hdrPassthrough && s.HdrActive() &&
                                pl_color_space_is_hdr(&s.image.color));
    // The swapchain hint selects the signal format, not the tone-map peak.
    // libplacebo 7.360.1 treats SDR hints above 203 nits as HDR and may select
    // PQ on 10-bit buffers. Keep SDR signalling fixed; apply the user's peak
    // only to the render target below, after its SDR transfer is established.
    pl_color_space hint = rtxHdr ? pl_color_space_hdr10 : hdr ? s.image.color : pl_color_space_srgb;
    if (rtxHdr)
    {
        const float peak = s.DisplayPeak();
        hint.hdr.max_luma = peak > 0 ? peak : 1000;
    }
    if (!hdr)
        hint.hdr.max_luma = PL_COLOR_SDR_WHITE;
    if (hdr && s.settings.hdrMode == 3)
    {
        // HDR output with highlights rolled off to the display's peak: the
        // target peak is HDR Target Peak, else the peak Windows reports now.
        const float peak = s.settings.hdrNits > 0 ? s.settings.hdrNits : s.DisplayPeak();
        hint.hdr.max_luma = peak > 0 ? peak : 1000;
        hint.hdr.max_cll = hint.hdr.max_fall = 0;
        hint.hdr.scene_max[0] = hint.hdr.scene_max[1] = hint.hdr.scene_max[2] = hint.hdr.scene_avg = 0;
        hint.hdr.max_pq_y = hint.hdr.avg_pq_y = 0;
        hint.hdr.ootf = {};
    }
    pl_swapchain_colorspace_hint(s.swapchain, &hint);
    pl_swapchain_frame frame = {};
    if (!pl_swapchain_start_frame(s.swapchain, &frame))
        return E_FAIL;
    pl_frame target = {};
    pl_frame_from_swapchain(&target, &frame);
    // A 16-bit float swapchain is scRGB (linear, 1.0 = 80 nits), and libplacebo
    // writes SDR white at 203 nits. With Windows HDR off the desktop shows 1.0
    // as the display's white and clips above it, so write linear SDR with white
    // at 1.0 there.
    if (target.color.transfer == PL_COLOR_TRC_SCRGB && !s.HdrActive())
        target.color.transfer = PL_COLOR_TRC_LINEAR;
    if (!hdr)
        target.color.hdr.max_luma = s.SdrTargetNits();
    // Display gamma: HDR tone mapped to SDR is encoded for how the display
    // decodes. SDR only changes with gamma processing, which converts it from
    // the desired gamma to the display gamma; otherwise it stays as before.
    // Only a plain sRGB target qualifies: linear 16-bit float and Windows HDR
    // outputs have no display gamma to match.
    // An ICC profile describes the display itself.
    const bool sdrTarget = !hdr && target.color.transfer == PL_COLOR_TRC_SRGB && !s.settings.iccEnabled;
    const bool hdrSource = pl_color_space_is_hdr(&s.image.color);
    // A wide-gamut display shows the picture's colours correctly only when
    // they are mapped into its gamut.
    static constexpr pl_color_primaries DisplayPrimaries[] = {PL_COLOR_PRIM_UNKNOWN,
                                                              PL_COLOR_PRIM_BT_601_525,
                                                              PL_COLOR_PRIM_BT_601_625,
                                                              PL_COLOR_PRIM_BT_2020,
                                                              PL_COLOR_PRIM_DCI_P3,
                                                              PL_COLOR_PRIM_DISPLAY_P3};
    if (sdrTarget && s.settings.displayPrimaries > 0)
        target.color.primaries = DisplayPrimaries[s.settings.displayPrimaries];
    // A pure-power display gamma is encoded with the nearest of libplacebo's
    // curves (1.8 to 2.8 in steps of 0.2), then raised to the exact gamma.
    static constexpr pl_color_transfer PowerCurves[] = {PL_COLOR_TRC_GAMMA18,
                                                        PL_COLOR_TRC_GAMMA20,
                                                        PL_COLOR_TRC_GAMMA22,
                                                        PL_COLOR_TRC_GAMMA24,
                                                        PL_COLOR_TRC_GAMMA26,
                                                        PL_COLOR_TRC_GAMMA28};
    const int display = s.settings.displayGammaCurve;
    if (sdrTarget && (hdrSource || s.settings.sdrGammaCurve > 0))
    {
        if (display == 1)
            target.color.transfer = PL_COLOR_TRC_BT_1886;
        else if (display >= 180)
        {
            const int nearest = std::clamp((display - 170) / 20, 0, 5);
            target.color.transfer = PowerCurves[nearest];
            s.outputGamma = float(180 + 20 * nearest) / float(display);
        }
    }
    // BT.1886 follows the display's black point. Without a contrast it is an
    // infinite-contrast display (identical to 2.4), not libplacebo's 1000:1.
    if (target.color.transfer == PL_COLOR_TRC_BT_1886)
        target.color.hdr.min_luma = s.settings.displayContrast > 0
                                        ? target.color.hdr.max_luma / float(s.settings.displayContrast)
                                        : PL_COLOR_HDR_BLACK;
    s.sdrGammaCurve = sdrTarget && !hdrSource ? s.settings.sdrGammaCurve : 0;
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
    s.sdrGammaCurve = 0;
    s.outputGamma = 1;
    QueryPerformanceCounter(&end);
    s.renderGpu.End(s.context.Get());
    if (rendered)
    {
        s.renderCpu.Add(GetTickCount64(), 1000. * (end.QuadPart - begin.QuadPart) / frequency.QuadPart);
        s.renderProbe.Record(GetTickCount64(), s.renderCpu.last);
        s.TraceRender(GetTickCount64());
    }
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
PlaceboBackend::RenderOffscreen(
    int width, int height, std::vector<uint8_t>& rgba, bool hdrTarget, int textureDepth, bool windowGeometry)
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
        const float display = s.settings.hdrMode == 3 ? s.DisplayPeak() : 0;
        target.color.hdr.max_luma = s.settings.hdrNits > 0 ? s.settings.hdrNits : display > 0 ? display : 1000;
    }
    bool ok = s.Render(target, hdrTarget, false, windowGeometry);
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
    s.backlog.Reset();
    s.renderCpu.Reset();
    s.renderProbe.Reset();
    s.presentCpu.Reset();
    s.renderGpu.Reset();
    s.statisticsText.clear();
    s.imageAnalysis.Reset();
    if (preserveBlackBars)
        s.blackBars.Discontinuity();
    else
        s.blackBars.Reset();
    s.ClearFrame();
    s.history.clear();
    if (s.renderer)
        pl_renderer_flush_cache(s.renderer);
    s.rtx.Reset();
}

void PlaceboBackend::HoldForResize(bool hold)
{
    auto& s = *state;
    if (s.resizeHold.exchange(hold) && !hold)
        s.resizeResumed = true;
}

HRESULT PlaceboBackend::TryPresent(bool blackWhenEmpty)
{
    auto& s = *state;
    std::unique_lock guard(s.lock, std::try_to_lock);
    if (!guard.owns_lock())
        return HRESULT_FROM_WIN32(ERROR_BUSY);
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

HRESULT PlaceboBackend::SetRotation(int degrees)
{
    if (degrees < 0 || degrees >= 360 || degrees % 90)
        return E_INVALIDARG;
    std::lock_guard guard(state->lock);
    state->rotation = degrees;
    return S_OK;
}

int PlaceboBackend::Rotation() const
{
    std::lock_guard guard(state->lock);
    return state->rotation;
}

void PlaceboBackend::SetFlip(bool mirror)
{
    std::lock_guard guard(state->lock);
    state->flip = mirror;
}

PlaceboBackend::DisplayHdrInput PlaceboBackend::DisplayHdr() const
{
    const int input = state->displayHdrInput.load();
    return {(input & 1) != 0, (input & 2) != 0, (input & 4) != 0};
}

void PlaceboBackend::SetDisplayHdrListener(std::function<void()> listener)
{
    state->displayHdrListener = std::move(listener);
}

void PlaceboBackend::SetDisplayHdrState(int enabled, bool rebuildOutput)
{
    state->displayHdrState = enabled;
    state->outputStale = true;
    if (rebuildOutput)
        state->displayHdrRebuild = true;
}

std::wstring PlaceboBackend::StatisticsSnapshot() const
{
    std::lock_guard guard(state->lock);
    return state->statisticsText;
}

bool PlaceboBackend::Flip() const
{
    std::lock_guard guard(state->lock);
    return state->flip;
}

ID3D11Device* PlaceboBackend::Device() const
{
    return state->d3d ? state->d3d->device : nullptr;
}

ID3D11Device* PlaceboBackend::EnsureDevice()
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    return SUCCEEDED(s.EnsureDevice()) ? s.d3d->device : nullptr;
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

void PlaceboBackend::ShowMessage(const std::wstring& text)
{
    std::lock_guard guard(state->messageLock);
    state->message = text;
    state->messageUntil = GetTickCount64() + 2000;
}

bool PlaceboBackend::StatisticsVisible() const
{
    return state->showStatistics.load();
}

uint64_t PlaceboBackend::RtxFrames() const
{
    std::lock_guard guard(state->lock);
    return state->rtx.Evaluations();
}

void PlaceboBackend::SetContentProfile(const std::wstring& label)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.contentProfile = label;
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

void PlaceboBackend::SetPresentationTiming(int64_t due, bool running, int64_t mediaTime, int refreshLead)
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    s.presentationDue = due;
    s.presentationMediaTime = mediaTime;
    s.presentationTimed = running && due > 0;
    s.presentationLead = refreshLead;
}

int PlaceboBackend::PresentAheadCeiling() const
{
    return state->presentCeiling;
}

bool PlaceboBackend::VsyncGrid(int64_t& qpc, uint32_t& refresh, double& period) const
{
    auto& s = *state;
    std::lock_guard guard(s.lock);
    LARGE_INTEGER now = {}, frequency = {};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if (!s.lastSyncQpc || !s.stableCadence || !(s.measuredPeriod > 0) ||
        now.QuadPart - s.lastSyncQpc > frequency.QuadPart / 2)
        return false;
    qpc = s.lastSyncQpc;
    refresh = s.lastSyncRefresh;
    period = s.measuredPeriod * frequency.QuadPart / 1e7;
    return true;
}

void PlaceboBackend::ResetPresentationMeasurement(bool counters)
{
    state->measurementReset.fetch_or(counters ? 2u : 1u);
}

void PlaceboBackend::ResetStatistics()
{
    state->measurementReset.fetch_or(2u | 4u);
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

HRESULT PlaceboBackend::ImportDolbyVision(const VideoFrame& frame, IDvLayerSample* sample)
{
    if (!sample)
        return E_POINTER;
    DvFrameInfo info = {};
    HRESULT hr = sample->GetInfo(DvTransportVersion, sizeof(info), &info);
    if (FAILED(hr))
        return hr;
    if (info.version != DvTransportVersion || info.placeboApi != PL_API_VER || info.bytes != sizeof(info) ||
        !frame.dovi || info.width[0] != frame.width || info.height[0] != frame.height)
        return E_INVALIDARG;
    const int layers = frame.dovi->nlq_active ? 2 : 1;
    if (layers == 1 && (info.width[1] || info.height[1]))
        return E_INVALIDARG;
    ComPtr<ID3D11Texture2D> textures[2][3];
    ComPtr<ID3D11Device> device;
    for (int l = 0; l < layers; ++l)
    {
        if (!info.width[l] || !info.height[l])
            return E_INVALIDARG;
        for (int p = 0; p < 3; ++p)
        {
            hr = sample->GetPlane(l, p, &textures[l][p]);
            if (FAILED(hr) || !textures[l][p])
                return E_INVALIDARG;
            D3D11_TEXTURE2D_DESC desc = {};
            textures[l][p]->GetDesc(&desc);
            ComPtr<ID3D11Device> owner;
            textures[l][p]->GetDevice(&owner);
            if (!device)
                device = owner;
            if (owner.Get() != device.Get() || desc.Format != DXGI_FORMAT_R16_UNORM || desc.ArraySize != 1 ||
                desc.Width != (p ? (info.width[l] + 1) / 2 : info.width[l]) ||
                desc.Height != (p ? (info.height[l] + 1) / 2 : info.height[l]))
                return E_INVALIDARG;
        }
    }
    hr = Device() == device.Get() ? S_OK : SetDevice(device.Get());
    if (FAILED(hr))
        return hr;
    auto& s = *state;
    std::lock_guard guard(s.lock);
    hr = s.Ensure();
    if (FAILED(hr))
        return hr;
    s.ClearFrame();
    s.Describe(frame);
    s.image.num_planes = 3;
    s.image.repr.bits = {16, 10, 0};
    s.felEnhancement = {};
    s.felEnhancement.num_planes = layers == 2 ? 3 : 0;
    s.felEnhancement.color = frame.color;
    s.felEnhancement.repr.sys = PL_COLOR_SYSTEM_BT_2020_NC;
    s.felEnhancement.repr.levels = PL_COLOR_LEVELS_LIMITED;
    s.felEnhancement.repr.bits = {16, 10, 0};
    s.felEnhancement.crop = {0, 0, float(info.width[1]), float(info.height[1])};
    for (int l = 0; l < layers; ++l)
        for (int p = 0; p < 3; ++p)
        {
            pl_d3d11_wrap_params wrap = {};
            wrap.tex = textures[l][p].Get();
            auto& plane = l ? s.felEnhancement.planes[p] : s.image.planes[p];
            auto& texture = l ? s.felPlanes[p] : s.planes[p];
            texture = pl_d3d11_wrap(s.d3d->gpu, &wrap);
            if (!texture)
            {
                s.ClearFrame();
                return E_FAIL;
            }
            plane.texture = texture;
            plane.components = 1;
            plane.component_mapping[0] = p;
        }
    pl_frame_set_chroma_location(&s.image, PL_CHROMA_TOP_LEFT);
    if (layers == 2)
        pl_frame_set_chroma_location(&s.felEnhancement, PL_CHROMA_TOP_LEFT);
    s.image.enhancement_layer = layers == 2 ? &s.felEnhancement : nullptr;
    s.valid = true;
    s.mixing = false;
    return S_OK;
}
