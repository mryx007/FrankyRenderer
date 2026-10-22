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
#include "MediaFormat.h"
#include "Settings.h"
#include "FieldSample.h"
#include "Diagnostics.h"
#include <atomic>
#include <mutex>

class Renderer;

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

    UINT STDMETHODCALLTYPE GetD3D11TextureBindFlags() override
    {
        return 0;
    }

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
    OAHWND pendingOwner = 0;
    bool havePendingOwner = false;

  public:
    VideoWindow(Renderer*, CCritSec*, IUnknown*, HRESULT*);
    HRESULT ApplyPendingOwner();
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
    HRESULT GetStaticImage(long*, long*) override;
    VIDEOINFOHEADER* GetVideoFormat() override;
    STDMETHODIMP GetPreferredAspectRatio(long*, long*) override;
};

class Renderer final : public CBaseVideoRenderer,
                       public ISpecifyPropertyPages,
                       public ISubRender11,
                       public IPlaceboSettings,
                       public IPlaceboDiagnostics,
                       public CExFilterConfigImpl
{
    friend class DecoderPin;
    friend class VideoWindow;
    friend class VideoControl;
    PlaceboBackend backend;
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
    std::atomic<bool> stopped = true;
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

  public:
    Renderer(IUnknown*, HRESULT*);
    ~Renderer();
    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID, void**) override;
    CBasePin* GetPin(int) override;
    HRESULT CheckMediaType(const CMediaType*) override;
    HRESULT SetMediaType(const CMediaType*) override;
    HRESULT DoRenderSample(IMediaSample*) override;
    HRESULT Receive(IMediaSample*) override;
    HRESULT BeginFlush() override;
    HRESULT DrainFields();
    HRESULT CompleteConnect(IPin*) override;
    HRESULT BreakConnect() override;
    HRESULT EndFlush() override;
    void Repaint();
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
    STDMETHODIMP Flt_SetBool(LPCSTR, bool) override;
};
