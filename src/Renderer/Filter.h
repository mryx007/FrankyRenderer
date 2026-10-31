#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "DirectShow/external/BaseClasses/streams.h"
#include <d3d11.h>
#include "DirectShow/Include/ID3DVideoMemoryConfiguration.h"
#include "DirectShow/Include/ISubRender11.h"
#include "DirectShow/Include/FilterInterfaces.h"
#include "DirectShow/Include/FilterInterfacesImpl.h"
#include "Renderer/MediaFormat.h"
#include "Settings/Settings.h"
#include "Renderer/FieldSample.h"
#include "Renderer/Diagnostics.h"
#include "DolbyVision/Processor.h"
#include "Renderer/DisplayModes.h"
#include <atomic>
#include <map>
#include <mutex>

class Renderer;
struct ModePause;

class DecoderPin final : public CRendererInputPin,
                         public ID3D11DecoderConfiguration,
                         public ID3D11DecoderTextureConfiguration
{
    Renderer* owner;
    std::atomic<unsigned> queried = 0;
    std::atomic<bool> nativeDecoder = false;

  public:
    DecoderPin(Renderer*, HRESULT*);
    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID, void**) override;
    STDMETHODIMP GetAllocator(IMemAllocator**) override;
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES*) override;
    HRESULT BreakConnect() override;
    STDMETHODIMP ActivateD3D11Decoding(ID3D11Device*, ID3D11DeviceContext*, HANDLE, UINT) override;
    UINT STDMETHODCALLTYPE GetD3D11AdapterIndex() override;

    UINT STDMETHODCALLTYPE GetD3D11TextureBindFlags() override;

    UINT STDMETHODCALLTYPE GetD3D11TextureMiscFlags() override
    {
        return 0;
    }

    STDMETHODIMP BeginFlush() override;
    STDMETHODIMP NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override;
    STDMETHODIMP EndOfStream() override;
};

class VideoWindow final : public CBaseControlWindow
{
    Renderer* owner;
    HRESULT SetHiddenStyle(long, int);
    HANDLE thread = nullptr, ready = nullptr;
    DWORD threadId = 0;
    HRESULT prepared = E_UNEXPECTED;
    static DWORD WINAPI WindowThread(void*);
    HRESULT PrepareOnThread();

  public:
    VideoWindow(Renderer*, CCritSec*, IUnknown*, HRESULT*);
    ~VideoWindow();
    HRESULT PrepareWindow() override;
    HRESULT DoneWithWindow() override;
    HRESULT UninitialiseWindow() override;
    LPTSTR GetClassWindowStyles(DWORD*, DWORD*, DWORD*) override;
    STDMETHODIMP put_Owner(OAHWND) override;
    STDMETHODIMP put_WindowStyle(long) override;
    STDMETHODIMP put_WindowStyleEx(long) override;
    STDMETHODIMP SetWindowPosition(long, long, long, long) override;
    BOOL OnSize(LONG, LONG) override;
    INT_PTR OnReceiveMessage(HWND, UINT, WPARAM, LPARAM) override;
};

class VideoControl final : public CBaseControlVideo
{
    Renderer* owner;

  public:
    VideoControl(Renderer*, CCritSec*, IUnknown*, HRESULT*);
    HRESULT IsDefaultTargetRect() override;
    HRESULT SetDefaultTargetRect() override;
    HRESULT SetTargetRect(RECT*) override;
    HRESULT GetTargetRect(RECT*) override;
    HRESULT IsDefaultSourceRect() override;
    HRESULT SetDefaultSourceRect() override;
    HRESULT SetSourceRect(RECT*) override;
    HRESULT GetSourceRect(RECT*) override;
    STDMETHODIMP GetCurrentImage(long*, long*) override;
    HRESULT GetStaticImage(long*, long*) override;
    VIDEOINFOHEADER* GetVideoFormat() override;
    STDMETHODIMP GetPreferredAspectRatio(long*, long*) override;
};

// HDR metadata read from the local file for one sample (see Renderer::fileMetadata).
struct FileMetadata
{
    std::vector<BYTE> dovi, hdr10Plus;
    StaticHdr container;
};

class Renderer final : public CBaseVideoRenderer,
                       public IAMFilterMiscFlags,
                       public ISpecifyPropertyPages,
                       public ISubRender11,
                       public IPlaceboSettings,
                       public IPlaceboDiagnostics,
                       public CExFilterConfigImpl
{
#ifdef PLACEBO_TEST_PRESENT_PIXELS
    friend class RendererRestartTest;
#endif
    friend class DecoderPin;
    friend class VideoWindow;
    friend class VideoControl;
    PlaceboBackend backend;
    DvProcessor dv;
    MediaFormat format;
    PlaceboSettings settings;
    PlaceboStatus status;
    PlaceboDiagnostics diagnostics;
    std::mutex statusLock;
    // Covers preprocessing and temporal delivery outside CBaseRenderer::Receive.
    // Flush/Stop must wake the base scheduler before waiting on this lock.
    CCritSec receiveLock;
    std::unique_ptr<VideoWindow> window;
    std::unique_ptr<VideoControl> control;
    RECT source = {}, target = {};
    REFERENCE_TIME segment = 0;
    std::atomic<REFERENCE_TIME> frameStart = 0, frameStop = 0, frameDuration = 0;
    unsigned sideDataLogged = 0;
    bool fieldDecisionLogged = false;
    std::atomic<FILTER_STATE> windowState = State_Stopped;
    // The paused sample already presented as preview. Run renders that same
    // held sample again; skip it so each picture is presented and counted once.
    std::mutex previewLock;
    IMediaSample* previewed = nullptr;
    // HDR metadata read from the local file for samples whose decoder attached
    // none (Dolby Vision Profile 5/8, HDR10+, static HDR10), keyed by sample
    // start until DoRenderSample applies it.
    std::mutex fileMetadataLock;
    std::map<REFERENCE_TIME, FileMetadata> fileMetadata;
    bool LookupFileMetadata(IMediaSample*, bool hdr, FileMetadata&);
    void ClearFileMetadata();
    void SetPreviewed(IMediaSample*);
    bool TakePreviewed(IMediaSample*);
    QueuedVideoFrame pending, previous;
    std::atomic<bool> deinterlacingActive = false;
    bool buffering = false, smoothBuffering = false;
    QueuedVideoFrame followingQueued;
    std::atomic<bool> smoothEnabled = false;
    std::atomic<double> motionPeriod = 0;
    std::atomic<bool> refreshDirty = true;
    HMONITOR motionMonitor = nullptr;
    std::vector<QueuedVideoFrame> motionQueue, motionPast;
    std::atomic<float> motionRadius = 0;
    std::atomic<int> motionLookahead = 2;
    int bufferedLookahead = 2;
    HRESULT DeliverQueuedMotion();
    HRESULT DeliverMotion(const QueuedVideoFrame&,
                          uint64_t,
                          uint64_t,
                          uint64_t afterNext = 0,
                          std::shared_ptr<const std::vector<TemporalFrame>> temporal = {});
    void ResetFields(bool preserveBlackBars = true, bool preserveVideo = false);
    HRESULT DeliverFields(const QueuedVideoFrame&, uint64_t, uint64_t);
    HRESULT PrepareWindow();
    std::atomic<HMONITOR> profileMonitor = nullptr;
    HRESULT UpdateDisplayProfile(bool refresh = false);
    // Display mode switching in fullscreen. The window thread owns the switcher;
    // other threads publish the active profile's list and request a check.
    struct ModeConfig
    {
        bool enabled = false;
        std::vector<DisplayModes::Mode> modes;
        float delay = 0;
    };
    std::mutex modeLock;
    ModeConfig modeConfig;
    std::shared_ptr<ModePause> modePause; // guarded by modeLock
    DisplayModes::Switcher modeSwitcher;
    RECT modeWindow = {}; // fullscreen window rectangle when the mode was switched
    std::atomic<uint64_t> modeHoldUntil = 0;
    // The source's frame duration. frameDuration follows the presented samples,
    // which Smooth Motion delivers at the display rate.
    std::atomic<REFERENCE_TIME> contentDuration = 0;
    void PublishDisplayModes();
    void RequestModeCheck(UINT delay = 250);
    void CheckDisplayMode();
    void PausePlayback(DWORD milliseconds);
    bool ExternalStateChange();

  public:
    Renderer(IUnknown*, HRESULT*);
    ~Renderer();

    ULONG STDMETHODCALLTYPE GetMiscFlags() override
    {
        return AM_FILTER_MISC_FLAGS_IS_RENDERER;
    }

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID, void**) override;
    CBasePin* GetPin(int) override;
    HRESULT CheckMediaType(const CMediaType*) override;
    HRESULT SetMediaType(const CMediaType*) override;
    HRESULT DoRenderSample(IMediaSample*) override;
    void OnReceiveFirstSample(IMediaSample*) override;
    HRESULT Receive(IMediaSample*) override;
    HRESULT BeginFlush() override;
    HRESULT DrainFields();
    HRESULT CompleteConnect(IPin*) override;
    HRESULT BreakConnect() override;
    HRESULT EndFlush() override;
    // The source names the trigger in the opt-in diagnostic log.
    void Repaint(const char* source = "other");
    uint64_t traceDrops = 0, traceRepeats = 0, traceGlitches = 0;
    // Tick of the last scheduled frame presented while running. A repaint
    // during active playback is left to the next scheduled frame.
    std::atomic<uint64_t> lastTimedPresent = 0;
    HRESULT ShouldDrawSampleNow(IMediaSample*, REFERENCE_TIME*, REFERENCE_TIME*) override;
    void NewSegment(REFERENCE_TIME);
    void ToggleStatistics();
    STDMETHODIMP Stop() override;
    STDMETHODIMP Run(REFERENCE_TIME) override;
    STDMETHODIMP Pause() override;
    HRESULT OnStartStreaming() override;
    STDMETHODIMP GetPages(CAUUID*) override;
    STDMETHODIMP SetCallback11(ISubRender11Callback*) override;
    STDMETHODIMP GetSettings(PlaceboSettings*) override;
    STDMETHODIMP SetSettings(const PlaceboSettings*) override;
    STDMETHODIMP GetStatus(PlaceboStatus*) override;
    STDMETHODIMP GetDiagnostics(PlaceboDiagnostics*) override;
    STDMETHODIMP ReadPixels(int, int, BYTE*, SIZE_T) override;
    STDMETHODIMP Flt_GetBool(LPCSTR, bool*) override;
    STDMETHODIMP Flt_GetInt(LPCSTR, int*) override;
    STDMETHODIMP Flt_GetInt64(LPCSTR, __int64*) override;
    STDMETHODIMP Flt_GetDouble(LPCSTR, double*) override;
    STDMETHODIMP Flt_GetBin(LPCSTR, LPVOID*, unsigned*) override;
    STDMETHODIMP Flt_SetBool(LPCSTR, bool) override;
    STDMETHODIMP Flt_SetInt(LPCSTR, int) override;
};
