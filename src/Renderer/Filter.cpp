#include "DolbyVision/Transport.h"
#include "Renderer/SourceCodec.h"
// DirectShow pin and state management use the MPC-distributed BaseClasses.
// The D3D11 decoder interface follows MPC Source/VideoRendererInputPin.cpp.
#include "Renderer/Filter.h"
#include "Renderer/Trace.h"
#include "Renderer/SideDataAllocator.h"
#include "Renderer/MotionTiming.h"
#include "Renderer/StatisticsKey.h"
#include "Renderer/ImageAnalysis.h"
#include "Renderer/StatisticsRasterizer.h"
#include "Renderer/VideoBackground.h"
#include "Renderer/MonitorChange.h"
#include "Renderer/ResizeGesture.h"
#include "Settings/SettingsJournal.h"
#include <wrl/client.h>
#include <dxgi1_2.h>
#include <vector>
#include <cstring>
#include <algorithm>
using Microsoft::WRL::ComPtr;

static HRESULT DescribeDolbyVision(IDvLayerSample* sample, VideoFrame& frame)
{
    DvFrameInfo info = {};
    HRESULT hr = sample->GetInfo(DvTransportVersion, sizeof(info), &info);
    if (FAILED(hr))
        return hr;
    if (info.version != DvTransportVersion || info.placeboApi != PL_API_VER || info.bytes != sizeof(info))
        return E_INVALIDARG;
    frame.dovi = std::make_shared<pl_dovi_metadata>(info.dovi);
    frame.color = info.color;
    frame.dvLayered = info.dovi.nlq_active && info.width[1] > 0;
    return S_OK;
}

static HRESULT
CaptureDib(PlaceboBackend& backend, int width, int height, bool windowGeometry, std::vector<uint8_t>& dib)
try
{
    if (width <= 0 || height <= 0)
        return VFW_E_NOT_CONNECTED;
    const uint64_t pixelBytes = uint64_t(width) * height * 4;
    if (sizeof(BITMAPINFOHEADER) + pixelBytes > LONG_MAX)
        return E_OUTOFMEMORY;
    std::vector<uint8_t> pixels;
    const HRESULT hr = backend.RenderOffscreen(width, height, pixels, false, 8, windowGeometry);
    if (FAILED(hr))
        return hr;
    if (pixels.size() != pixelBytes)
        return E_UNEXPECTED;
    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(header);
    header.biWidth = width;
    header.biHeight = -height;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(pixelBytes);
    dib.resize(sizeof(header) + pixels.size());
    memcpy(dib.data(), &header, sizeof(header));
    auto out = dib.data() + sizeof(header);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        out[i] = pixels[i + 2];
        out[i + 1] = pixels[i + 1];
        out[i + 2] = pixels[i];
        out[i + 3] = 255;
    }
    return S_OK;
}
catch (const std::bad_alloc&)
{
    return E_OUTOFMEMORY;
}

DecoderPin::DecoderPin(Renderer* renderer, HRESULT* hr) : CRendererInputPin(renderer, hr, L"Input"), owner(renderer) {}

STDMETHODIMP DecoderPin::GetAllocator(IMemAllocator** out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    CAutoLock guard(m_pLock);
    // Native decoder samples belong to the decoder's texture allocator.
    // Offering CPU samples here prevents the host from supplying GPU surfaces.
    if (nativeDecoder)
    {
        Trace("Native D3D11 decoder owns sample allocator");
        return E_NOTIMPL;
    }
    if (!m_pAllocator)
    {
        HRESULT hr = S_OK;
        auto allocator = new (std::nothrow) SideDataAllocator(&hr);
        if (!allocator)
            return E_OUTOFMEMORY;
        if (FAILED(hr))
        {
            delete allocator;
            return hr;
        }
        hr = allocator->QueryInterface(IID_IMemAllocator, reinterpret_cast<void**>(&m_pAllocator));
        if (FAILED(hr))
        {
            delete allocator;
            return hr;
        }
        Trace("Decoder allocator provides IMediaSideData samples");
    }
    *out = m_pAllocator;
    (*out)->AddRef();
    return S_OK;
}

STDMETHODIMP DecoderPin::GetAllocatorRequirements(ALLOCATOR_PROPERTIES* properties)
{
    if (!properties)
        return E_POINTER;
    CAutoLock guard(m_pLock);
    if (!nativeDecoder)
        return CRendererInputPin::GetAllocatorRequirements(properties);
    // MPC-BE's native decoder only supplies its surface count in
    // DecideBufferSize. Its surface allocator still calls CBaseAllocator::Alloc,
    // which requires positive byte size and alignment even for GPU samples.
    *properties = {1, 1, 1, 0};
    Trace("Native decoder allocator requirements: buffers=1 bytes=1 alignment=1");
    return S_OK;
}

STDMETHODIMP DecoderPin::NonDelegatingQueryInterface(REFIID id, void** out)
{
    TraceInterface("input pin", id);
    if (id == __uuidof(ID3D11DecoderConfiguration))
    {
        if (!(queried.fetch_or(1) & 1))
            Trace("Decoder queried public D3D11 device interface: supported");
        return GetInterface(static_cast<ID3D11DecoderConfiguration*>(this), out);
    }
    if (id == __uuidof(ID3D11DecoderTextureConfiguration))
    {
        if (!(queried.fetch_or(2) & 2))
            Trace("Decoder queried D3D11 texture flags: supported");
        return GetInterface(static_cast<ID3D11DecoderTextureConfiguration*>(this), out);
    }
    return CRendererInputPin::NonDelegatingQueryInterface(id, out);
}

STDMETHODIMP DecoderPin::ActivateD3D11Decoding(ID3D11Device* device, ID3D11DeviceContext* context, HANDLE mutex, UINT)
{
    if (!device || !context)
        return E_POINTER;
    ComPtr<ID3D11Device> contextDevice;
    context->GetDevice(&contextDevice);
    if (contextDevice.Get() != device)
        return E_INVALIDARG;
    HRESULT hr = owner->backend.SetDevice(device, mutex);
    if (SUCCEEDED(hr))
        hr = owner->dv.SetDevice(device, mutex);
    nativeDecoder = SUCCEEDED(hr);
    Trace("D3D11 decoder activation result=%08lX", static_cast<unsigned long>(hr));
    return hr;
}

HRESULT DecoderPin::BreakConnect()
{
    Trace("Input disconnect begin; native=%d", nativeDecoder.load());
    HRESULT hr = CRendererInputPin::BreakConnect();
    if (SUCCEEDED(hr))
        nativeDecoder = false;
    Trace("Input disconnect completed result=%08lX", static_cast<unsigned long>(hr));
    return hr;
}

UINT DecoderPin::GetD3D11TextureBindFlags()
{
    return owner->dv.Active() ? D3D11_BIND_SHADER_RESOURCE : 0;
}

UINT DecoderPin::GetD3D11AdapterIndex()
{
    auto device = owner->backend.Device();
    if (!device)
        return 0;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory1> factory;
    DXGI_ADAPTER_DESC own = {};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&own)) || FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
        return 0;
    for (UINT i = 0;; i++)
    {
        ComPtr<IDXGIAdapter1> candidate;
        if (FAILED(factory->EnumAdapters1(i, &candidate)))
            break;
        DXGI_ADAPTER_DESC1 desc = {};
        if (SUCCEEDED(candidate->GetDesc1(&desc)) && desc.AdapterLuid.HighPart == own.AdapterLuid.HighPart &&
            desc.AdapterLuid.LowPart == own.AdapterLuid.LowPart)
            return i;
    }
    return 0;
}

STDMETHODIMP DecoderPin::BeginFlush()
{
    // Release a Receive waiting for its presentation time before joining the
    // whole temporal-input operation. Never wait on receiveLock with the base
    // interface/render locks held: the source may need those locks to return.
    owner->dv.Cancel();
    HRESULT hr = CRendererInputPin::BeginFlush();
    if (FAILED(hr))
        return hr;
    CAutoLock receive(&owner->receiveLock);
    owner->dv.ResetTimeline();
    CAutoLock render(&owner->m_RendererLock);
    owner->ResetFields();
    return hr;
}

STDMETHODIMP DecoderPin::NewSegment(REFERENCE_TIME start, REFERENCE_TIME stop, double rate)
{
    CAutoLock receive(&owner->receiveLock);
    if (owner->dv.Active() && rate != 1.0)
        return E_INVALIDARG;
    owner->NewSegment(start);
    return CRendererInputPin::NewSegment(start, stop, rate);
}

STDMETHODIMP DecoderPin::EndOfStream()
{
    CAutoLock receive(&owner->receiveLock);
    if (IsFlushing())
        return S_FALSE;
    HRESULT hr = owner->DrainFields();
    return FAILED(hr) ? hr : CRendererInputPin::EndOfStream();
}

VideoWindow::VideoWindow(Renderer* renderer, CCritSec* lock, IUnknown* outer, HRESULT* hr)
    : CBaseControlWindow(renderer, lock, NAME("FrankyRenderer video window"), outer, hr), owner(renderer)
{
}

LPTSTR VideoWindow::GetClassWindowStyles(DWORD* cls, DWORD* style, DWORD* extra)
{
    *cls = CS_HREDRAW | CS_VREDRAW;
    *style = WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    *extra = 0;
    return const_cast<LPTSTR>(TEXT("PlaceboRendererVideoWindow"));
}

VideoWindow::~VideoWindow()
{
    DoneWithWindow();
}

DWORD WINAPI VideoWindow::WindowThread(void* context)
{
    auto& self = *static_cast<VideoWindow*>(context);
    // A new thread can inherit its creator's wait cursor. Initialize its own
    // queue from the currently displayed cursor before attaching to the host.
    CURSORINFO cursor = {sizeof(cursor)};
    if (GetCursorInfo(&cursor))
        SetCursor((cursor.flags & CURSOR_SHOWING) ? cursor.hCursor : nullptr);
    self.prepared = self.PrepareOnThread();
    SetEvent(self.ready);
    if (SUCCEEDED(self.prepared))
    {
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    self.CBaseWindow::DoneWithWindow();
    return 0;
}

HRESULT VideoWindow::PrepareWindow()
{
    if (!m_hwndOwner || !IsWindow(m_hwndOwner))
        return VFW_E_WRONG_STATE;
    if (WindowExists())
        return S_OK;
    DoneWithWindow();
    ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready)
        return HRESULT_FROM_WIN32(GetLastError());
    thread = CreateThread(nullptr, 0, WindowThread, this, 0, &threadId);
    if (!thread)
    {
        const auto error = GetLastError();
        CloseHandle(ready);
        ready = nullptr;
        return HRESULT_FROM_WIN32(error);
    }
    const DWORD waited = WaitDispatchingMessages(ready, INFINITE);
    CloseHandle(ready);
    ready = nullptr;
    if (waited != WAIT_OBJECT_0)
        return E_FAIL;
    const HRESULT result = prepared;
    if (FAILED(result))
        DoneWithWindow();
    return result;
}

HRESULT VideoWindow::PrepareOnThread()
{
    // A player can destroy its parent before releasing this COM instance.
    // Clear the dead HWND/DC state before creating a new child for a new owner.
    if (GetWindowHWND() && !WindowExists())
        CBaseWindow::DoneWithWindow();
    const bool creating = !WindowExists();
    HRESULT hr = CBaseWindow::PrepareWindow(m_hwndOwner);
    if (FAILED(hr) || !creating)
        return hr;
    // Pause may show the child before the player supplies its first placement.
    // Replace the base class's 320x240 default while the child is still hidden.
    RECT client = {};
    if (!GetClientRect(m_hwndOwner, &client) ||
        !SetWindowPos(GetWindowHWND(), nullptr, 0, 0, client.right, client.bottom, SWP_NOACTIVATE | SWP_NOZORDER))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HRESULT VideoWindow::DoneWithWindow()
{
    if (!thread)
        return CBaseWindow::DoneWithWindow();
    if (GetCurrentThreadId() == threadId)
    {
        const auto result = CBaseWindow::DoneWithWindow();
        PostQuitMessage(0);
        return result;
    }
    PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    const DWORD waited = WaitDispatchingMessages(thread, INFINITE);
    CloseHandle(thread);
    thread = nullptr;
    threadId = 0;
    return waited == WAIT_OBJECT_0 ? S_OK : E_FAIL;
}

HRESULT VideoWindow::UninitialiseWindow()
{
    const HWND video = GetWindowHWND();
    if (GetParent(video))
    {
        // Destroying an attached cross-thread child restores the input queues'
        // old cursor. Detach the hidden child first; never give it popup styles.
        ShowWindow(video, SW_HIDE);
        SetParent(video, nullptr);
    }
    return CBaseWindow::UninitialiseWindow();
}

BOOL VideoWindow::OnSize(LONG width, LONG height)
{
    CBaseWindow::OnSize(width, height);
    if (ResizeGesture::Resizing(GetWindowHWND()))
    {
        owner->backend.HoldForResize(true);
        SetTimer(GetWindowHWND(), ResizeGesture::Timer, ResizeGesture::QuietMilliseconds, nullptr);
    }
    if (GetWindowLongPtrW(GetWindowHWND(), GWL_STYLE) & WS_CHILD)
        VideoBackground::Resize(GetParent(GetWindowHWND()));
    owner->Repaint("window size");
    return TRUE;
}

HRESULT VideoWindow::SetHiddenStyle(long style, int index)
{
    // The legacy base implementation moves hidden windows offscreen, briefly
    // shows them, then hides them again. Frame changes need no visibility or
    // position change on our D3D11 child window.
    HWND hwnd = GetWindowHWND();
    if (index == GWL_STYLE)
        style &= ~WS_VISIBLE;
    if (static_cast<DWORD>(GetWindowLongPtrW(hwnd, index)) == static_cast<DWORD>(style))
        return S_OK;
    SetLastError(ERROR_SUCCESS);
    auto previous = SetWindowLongPtrW(hwnd, index, static_cast<LONG_PTR>(static_cast<DWORD>(style)));
    const DWORD error = GetLastError();
    if (!previous && error != ERROR_SUCCESS &&
        static_cast<DWORD>(GetWindowLongPtrW(hwnd, index)) != static_cast<DWORD>(style))
        return HRESULT_FROM_WIN32(error);
    if (!SetWindowPos(
            hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

STDMETHODIMP VideoWindow::put_WindowStyle(long style)
{
    if (!m_pPin || !m_pPin->IsConnected())
        return VFW_E_NOT_CONNECTED;
    style = (style | WS_CHILD) & ~WS_POPUP;
    if ((style & (WS_DISABLED | WS_ICONIC | WS_MAXIMIZE | WS_MINIMIZE | WS_HSCROLL | WS_VSCROLL)) ||
        IsWindowVisible(GetWindowHWND()))
        return CBaseControlWindow::put_WindowStyle(style);
    return SetHiddenStyle(style, GWL_STYLE);
}

STDMETHODIMP VideoWindow::put_WindowStyleEx(long style)
{
    if (!m_pPin || !m_pPin->IsConnected())
        return VFW_E_NOT_CONNECTED;
    if (IsWindowVisible(GetWindowHWND()))
        return CBaseControlWindow::put_WindowStyleEx(style);
    return SetHiddenStyle(style, GWL_EXSTYLE);
}

STDMETHODIMP VideoWindow::SetWindowPosition(long left, long top, long width, long height)
{
    if (!m_pPin || !m_pPin->IsConnected())
        return VFW_E_NOT_CONNECTED;
    // Prepare newly exposed parent space before the GPU child grows over it.
    if (GetOwnerWindow())
        VideoBackground::Resize(GetOwnerWindow());
    if (owner->windowState.load() == State_Running)
        Trace("Host SetWindowPosition while running %ld,%ld %ldx%ld", left, top, width, height);
    return CBaseControlWindow::SetWindowPosition(left, top, width, height);
}

STDMETHODIMP VideoWindow::put_Owner(OAHWND next)
{
    if (!m_pPin)
        return E_UNEXPECTED;
    const HWND parent = reinterpret_cast<HWND>(next);
    if (parent && !IsWindow(parent))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    VideoBackground::Ensure(parent);
    if (!WindowExists())
    {
        // Both supported players normally provide the parent before connection.
        // A later assignment uses exactly the same child-window creation path.
        m_hwndOwner = parent;
        if (!parent || !m_pPin->IsConnected())
            return S_OK;
        const auto hr = owner->PrepareWindow();
        return FAILED(hr) ? hr : owner->UpdateDisplayProfile();
    }
    if (parent == m_hwndOwner)
        return S_OK;
    owner->backend.HoldForResize(false);
    const HWND video = GetWindowHWND();
    const bool visible = IsWindowVisible(video);
    DoShowWindow(SW_HIDE);
    if (!parent)
    {
        // Release the output before destroying its child. Never turn it into
        // an unowned popup, including during player shutdown or graph changes.
        const auto hr = owner->backend.SetWindow(nullptr);
        if (FAILED(hr))
        {
            if (visible)
                DoShowWindow(SW_SHOWNOACTIVATE);
            return hr;
        }
        StatisticsKey::Detach(video);
        const auto result = DoneWithWindow();
        if (SUCCEEDED(result))
            m_hwndOwner = nullptr;
        return result;
    }
    SetLastError(ERROR_SUCCESS);
    const HWND previous = SetParent(video, parent);
    const DWORD error = GetLastError();
    const HRESULT hr = !previous && error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : S_OK;
    if (SUCCEEDED(hr))
    {
        m_hwndOwner = parent;
        StatisticsKey::Attach(video);
        PaintWindow(TRUE);
    }
    if (visible)
        DoShowWindow(SW_SHOWNOACTIVATE);
    return FAILED(hr) ? hr : owner->UpdateDisplayProfile();
}

INT_PTR VideoWindow::OnReceiveMessage(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    constexpr UINT_PTR repaintTimer = 0x46525950;
    if (msg == WM_TIMER && w == ResizeGesture::Timer)
    {
        // A queued timer can predate the most recent size change. Check the
        // deadline before resuming, even while the mouse button remains down.
        const UINT remaining = ResizeGesture::Remaining(hwnd);
        if (remaining)
            SetTimer(hwnd, ResizeGesture::Timer, remaining, nullptr);
        else
        {
            KillTimer(hwnd, ResizeGesture::Timer);
            owner->backend.HoldForResize(false);
            owner->Repaint("resize timer");
        }
        return 0;
    }
    if (msg == WM_TIMER && w == repaintTimer)
    {
        KillTimer(hwnd, repaintTimer);
        owner->Repaint("retry timer");
        return 0;
    }
    if (msg == WM_CREATE)
    {
        MonitorChange::Attach(hwnd);
        ResizeGesture::Attach(hwnd);
    }
    if (msg == WM_NCDESTROY)
    {
        StatisticsKey::Detach(hwnd);
        MonitorChange::Detach(hwnd);
        ResizeGesture::Detach(hwnd);
        owner->backend.HoldForResize(false);
    }
    if (msg == ResizeGesture::EndMessage)
    {
        KillTimer(hwnd, ResizeGesture::Timer);
        owner->backend.HoldForResize(false);
        owner->Repaint("resize end");
        return 0;
    }
    if (msg == MonitorChange::Message)
    {
        Trace("Monitor-change repaint received monitor=%p", MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
        owner->refreshDirty = true;
        owner->Repaint("monitor change");
        return 0;
    }
    if (msg == WM_KEYDOWN && w == 'J' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) &&
        !(GetKeyState(VK_SHIFT) & 0x8000))
    {
        if (!(l & (1LL << 30)))
            owner->ToggleStatistics();
        return 0;
    }
    if (msg == WM_TIMER && w == ImageAnalysis::Timer)
    {
        KillTimer(hwnd, ImageAnalysis::Timer);
        const auto state = owner->windowState.load();
        if (state == State_Paused && owner->settings.imageAnalysis)
            owner->Repaint();
        return 0;
    }
    if (msg == StatisticsKey::ToggleMessage)
    {
        owner->ToggleStatistics();
        return 0;
    }
    if (msg == StatisticsRasterizer::ReadyMessage || msg == ImageAnalysis::ReadyMessage)
    {
        const auto state = owner->windowState.load();
        // Running playback consumes the bitmap on its next scheduled frame.
        // Only a paused image needs an additional repaint after text is ready.
        if (state == State_Paused && (owner->backend.StatisticsVisible() || owner->settings.imageAnalysis))
            owner->Repaint();
        return 0;
    }
    if (msg == WM_ERASEBKGND)
    {
        owner->backend.PaintLoadingBackground(reinterpret_cast<HDC>(w));
        return TRUE;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        owner->backend.PaintLoadingBackground(ps.hdc);
        EndPaint(hwnd, &ps);
        owner->Repaint("WM_PAINT");
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE)
    {
        owner->UpdateDisplayProfile(true);
        owner->refreshDirty = true;
        owner->Repaint("display change");
        return 0;
    }
    return CBaseControlWindow::OnReceiveMessage(hwnd, msg, w, l);
}

VideoControl::VideoControl(Renderer* renderer, CCritSec* lock, IUnknown* outer, HRESULT* hr)
    : CBaseControlVideo(renderer, lock, NAME("FrankyRenderer video control"), outer, hr), owner(renderer)
{
}

HRESULT VideoControl::IsDefaultTargetRect()
{
    return IsRectEmpty(&owner->target) ? S_OK : S_FALSE;
}

HRESULT VideoControl::SetDefaultTargetRect()
{
    owner->target = {};
    owner->backend.SetGeometry(owner->source, owner->target);
    return S_OK;
}

HRESULT VideoControl::SetTargetRect(RECT* r)
{
    if (!r)
        return E_POINTER;
    owner->target = *r;
    owner->backend.SetGeometry(owner->source, owner->target);
    return S_OK;
}

HRESULT VideoControl::GetTargetRect(RECT* r)
{
    if (!r)
        return E_POINTER;
    *r = owner->target;
    if (IsRectEmpty(r) && owner->window)
        GetClientRect(owner->window->GetWindowHWND(), r);
    return S_OK;
}

HRESULT VideoControl::IsDefaultSourceRect()
{
    return EqualRect(&owner->source, &owner->format.source) ? S_OK : S_FALSE;
}

HRESULT VideoControl::SetDefaultSourceRect()
{
    owner->source = owner->format.source;
    owner->backend.SetGeometry(owner->source, owner->target);
    return S_OK;
}

HRESULT VideoControl::SetSourceRect(RECT* r)
{
    if (!r)
        return E_POINTER;
    owner->source = *r;
    owner->backend.SetGeometry(owner->source, owner->target);
    return S_OK;
}

HRESULT VideoControl::GetSourceRect(RECT* r)
{
    if (!r)
        return E_POINTER;
    *r = owner->source;
    return S_OK;
}

VIDEOINFOHEADER* VideoControl::GetVideoFormat()
{
    return &owner->format.basic;
}

STDMETHODIMP VideoControl::GetPreferredAspectRatio(long* width, long* height)
{
    if (!width || !height)
        return E_POINTER;
    CAutoLock guard(m_pInterfaceLock);
    if (!m_pPin || !m_pPin->IsConnected())
        return VFW_E_NOT_CONNECTED;
    // ParseFormat preserves the display aspect ratio, including anamorphic
    // input, and supplies the visible source dimensions when it is absent.
    *width = owner->format.frame.aspectX;
    *height = owner->format.frame.aspectY;
    return *width > 0 && *height > 0 ? S_OK : E_UNEXPECTED;
}

STDMETHODIMP VideoControl::GetCurrentImage(long* size, long* image)
{
    if (!size)
        return E_POINTER;
    CAutoLock guard(m_pInterfaceLock);
    if (!m_pPin || !m_pPin->IsConnected())
        return VFW_E_NOT_CONNECTED;
    // Both calls describe our converted BGRA DIB, not the decoder's YUV
    // format. MPC-HC also requests images without pausing playback first.
    return GetStaticImage(size, image);
}

HRESULT VideoControl::GetStaticImage(long* size, long* image)
{
    if (!size)
        return E_POINTER;
    const int width = owner->format.frame.width, height = owner->format.frame.height;
    if (width <= 0 || height <= 0)
        return VFW_E_NOT_CONNECTED;
    const uint64_t bytes = sizeof(BITMAPINFOHEADER) + uint64_t(width) * height * 4;
    if (bytes > LONG_MAX)
        return E_OUTOFMEMORY;
    const long required = static_cast<long>(bytes);
    if (!image)
    {
        *size = required;
        return S_OK;
    }
    if (*size < required)
    {
        *size = required;
        return E_OUTOFMEMORY;
    }
    std::vector<uint8_t> dib;
    HRESULT hr = CaptureDib(owner->backend, width, height, false, dib);
    if (FAILED(hr))
        return hr;
    memcpy(image, dib.data(), dib.size());
    *size = required;
    return S_OK;
}

// Restore settings a crashed player left in uncommitted live preview first.
static PlaceboSettings InitialSettings()
{
    SettingsJournal::Recover();
    return LoadSettings();
}

Renderer::Renderer(IUnknown* outer, HRESULT* hr)
    : CBaseVideoRenderer(CLSID_RegisteredRenderer, NAME("FrankyRenderer"), outer, hr), settings(InitialSettings())
{
    smoothEnabled = !!settings.smoothMotion;
    motionRadius = BackendSettings(settings).motionRadius;
    motionLookahead = std::max(2, static_cast<int>(std::ceil(motionRadius.load())) + 1);
    Trace("Renderer created; public DirectShow/libplacebo implementation");
    *hr = backend.SetSettings(BackendSettings(settings));
    if (FAILED(*hr) && (settings.iccEnabled || settings.lutEnabled))
    {
        Trace("Color files unavailable at startup: %08lX; using uncalibrated output", static_cast<unsigned long>(*hr));
        settings.iccEnabled = settings.lutEnabled = FALSE;
        *hr = backend.SetSettings(BackendSettings(settings));
    }
    if (FAILED(*hr))
        return;
    window = std::make_unique<VideoWindow>(this, &m_InterfaceLock, GetOwner(), hr);
    if (FAILED(*hr))
        return;
    control = std::make_unique<VideoControl>(this, &m_InterfaceLock, GetOwner(), hr);
    if (FAILED(*hr))
        return;
    CBasePin* pin = GetPin(0);
    if (!pin)
    {
        *hr = E_OUTOFMEMORY;
        return;
    }
    window->SetControlWindowPin(pin);
    control->SetControlVideoPin(pin);
}

Renderer::~Renderer()
{
    SetPreviewed(nullptr);
    Trace("Renderer destruction begin; rendered=%llu hardware=%llu last=%08lX",
          status.rendered,
          status.hardwareFrames,
          static_cast<unsigned long>(status.lastResult));
    backend.SetOverlayPainter({});
    Trace("Renderer subtitle bridge released");
    const HWND video = window ? window->GetWindowHWND() : nullptr;
    const HWND videoParent = video ? GetParent(video) : nullptr;
    if (video)
        StatisticsKey::Detach(video);
    // Do not release the swapchain while its visible child can still expose
    // the host's GDI backing contents. Hide first, then restore the background.
    if (video)
        ShowWindow(video, SW_HIDE);
    VideoBackground::Repaint(videoParent);
    backend.SetWindow(nullptr);
    if (window)
        window->DoneWithWindow();
    VideoBackground::Repaint(videoParent);
    Trace("Renderer window released; owner background refreshed");
}

CBasePin* Renderer::GetPin(int index)
{
    if (index != 0)
        return nullptr;
    CAutoLock guard(&m_InterfaceLock);
    if (!m_pInputPin)
    {
        HRESULT hr = S_OK;
        m_pInputPin = new (std::nothrow) DecoderPin(this, &hr);
        if (FAILED(hr))
        {
            delete m_pInputPin;
            m_pInputPin = nullptr;
        }
    }
    return m_pInputPin;
}

STDMETHODIMP Renderer::NonDelegatingQueryInterface(REFIID id, void** out)
{
    TraceInterface("renderer", id);
    if (id == IID_IAMFilterMiscFlags)
        return GetInterface(static_cast<IAMFilterMiscFlags*>(this), out);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (id == IID_IVideoWindow && window)
        return GetInterface(static_cast<IVideoWindow*>(window.get()), out);
    if (id == IID_IBasicVideo && control)
        return GetInterface(static_cast<IBasicVideo*>(control.get()), out);
    if (id == IID_IBasicVideo2 && control)
        return GetInterface(static_cast<IBasicVideo2*>(control.get()), out);
    if (id == IID_ISpecifyPropertyPages)
        return GetInterface(static_cast<ISpecifyPropertyPages*>(this), out);
    if (id == __uuidof(ISubRender11))
        return GetInterface(static_cast<ISubRender11*>(this), out);
    if (id == __uuidof(IPlaceboSettings))
        return GetInterface(static_cast<IPlaceboSettings*>(this), out);
    if (id == __uuidof(IPlaceboDiagnostics))
        return GetInterface(static_cast<IPlaceboDiagnostics*>(this), out);
    if (id == __uuidof(IExFilterConfig))
        return GetInterface(static_cast<IExFilterConfig*>(this), out);
    // Some hosts negotiate against the filter object instead of its input pin.
    // Both return the same decoder configuration and share its device lifetime.
    if (id == __uuidof(ID3D11DecoderConfiguration) || id == __uuidof(ID3D11DecoderTextureConfiguration))
    {
        Trace("Decoder interface requested on filter object");
        auto pin = GetPin(0);
        return pin ? pin->QueryInterface(id, out) : E_OUTOFMEMORY;
    }
    return CBaseVideoRenderer::NonDelegatingQueryInterface(id, out);
}

HRESULT Renderer::CheckMediaType(const CMediaType* mt)
{
    if (!mt)
        return E_POINTER;
    MediaFormat candidate;
    auto hr = ParseFormat(*mt, candidate);
    if (FAILED(hr))
        return hr;
    ComPtr<IPin> peer;
    if (m_pInputPin)
        m_pInputPin->ConnectedTo(&peer);
    return dv.CheckFormat(m_pGraph, peer.Get(), candidate);
}

HRESULT Renderer::SetMediaType(const CMediaType* mt)
{
    if (!mt)
        return E_POINTER;
    MediaFormat candidate;
    HRESULT hr = ParseFormat(*mt, candidate);
    if (FAILED(hr))
        return hr;
    CAutoLock guard(&m_RendererLock);
    ResetFields();
    format = candidate;
    frameDuration = format.basic.AvgTimePerFrame;
    source = format.source;
    backend.SetGeometry(source, target);
    {
        std::lock_guard stats(statusLock);
        status.width = format.frame.width;
        status.height = format.frame.height;
        status.hdrSource = pl_color_space_is_hdr(&format.frame.color);
    }
    Trace("Input %s %dx%d stride=%d primaries=%d transfer=%d matrix=%d range=%d interlaced=%d",
          Layout(format.frame.format).name,
          format.frame.width,
          format.frame.height,
          format.frame.stride,
          format.frame.color.primaries,
          format.frame.color.transfer,
          format.frame.matrix,
          format.frame.levels,
          format.interlaced);
    return CBaseVideoRenderer::SetMediaType(mt);
}

HRESULT Renderer::PrepareWindow()
{
    if (!window)
        return E_UNEXPECTED;
    HRESULT hr = S_OK;
    if (!window->WindowExists())
        hr = window->PrepareWindow();
    if (FAILED(hr))
        return hr;
    StatisticsKey::Attach(window->GetWindowHWND());
    return backend.SetWindow(window->GetWindowHWND());
}

HRESULT Renderer::CompleteConnect(IPin* pin)
{
    HRESULT hr = CBaseVideoRenderer::CompleteConnect(pin);
    if (FAILED(hr))
        return hr;
    PIN_INFO info = {};
    if (SUCCEEDED(pin->QueryPinInfo(&info)) && info.pFilter)
    {
        FILTER_INFO filterInfo = {};
        CLSID clsid = {};
        info.pFilter->GetClassID(&clsid);
        if (SUCCEEDED(info.pFilter->QueryFilterInfo(&filterInfo)))
        {
            Trace("Connected upstream filter=%ls pin=%ls CLSID=%08lX-%04X-%04X",
                  filterInfo.achName,
                  info.achName,
                  clsid.Data1,
                  clsid.Data2,
                  clsid.Data3);
            if (filterInfo.pGraph)
                filterInfo.pGraph->Release();
        }
        info.pFilter->Release();
    }
    // Hosts may supply the video owner before or after connection. AutoShow
    // belongs to Pause/Run; the video child starts hidden inside the player.
    backend.SetSourceCodec(SourceCodecFromOutput(pin));
    // Connection alone must not create a desktop window. If the player has
    // not supplied a parent yet, put_Owner creates the child when it arrives.
    if (!window->GetOwnerWindow())
        return S_OK;
    hr = PrepareWindow();
    return FAILED(hr) ? hr : UpdateDisplayProfile();
}

void Renderer::ResetFields(bool preserveBlackBars, bool preserveVideo)
{
    pending = {};
    previous = {};
    followingQueued = {};
    motionQueue.clear();
    motionPast.clear();
    bufferedLookahead = motionLookahead;
    buffering = false;
    smoothBuffering = false;
    deinterlacingActive = false;
    // A live mode change needs new temporal samples, but the last displayed
    // image must remain valid while buffering them (including an immediate pause).
    if (!preserveVideo)
    {
        backend.Flush(preserveBlackBars);
        std::lock_guard stats(statusLock);
        diagnostics = {};
    }
}

HRESULT Renderer::BreakConnect()
{
    dv.Cancel();
    dv.Disconnect();
    SetPreviewed(nullptr);
    backend.SetSourceCodec({});
    ResetFields(false);
    // Disconnect, unlike a temporary graph Stop, really retires this image
    // and releases native HDR output before another stream takes its place.
    const auto redraw = backend.TryPresent(true);
    if (FAILED(redraw))
        Trace("Disconnected background redraw failed result=%08lX", static_cast<unsigned long>(redraw));
    return CBaseVideoRenderer::BreakConnect();
}

HRESULT Renderer::DeliverFields(const QueuedVideoFrame& frame, uint64_t past, uint64_t future)
{
    if (smoothEnabled)
        return DeliverMotion(frame, past, future);
    for (int parity = 0; parity < 2; parity++)
    {
        ComPtr<IMediaSample> field;
        field.Attach(new (std::nothrow) FieldSample(frame, past, future, parity));
        if (!field)
            return E_OUTOFMEMORY;
        HRESULT hr = CBaseVideoRenderer::Receive(field.Get());
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

HRESULT Renderer::DeliverMotion(const QueuedVideoFrame& frame,
                                uint64_t past,
                                uint64_t future,
                                uint64_t afterNext,
                                std::shared_ptr<const std::vector<TemporalFrame>> temporal)
{
    HWND hwnd = window ? window->GetWindowHWND() : nullptr;
    auto monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (refreshDirty.exchange(false) || monitor != motionMonitor)
    {
        motionMonitor = monitor;
        motionPeriod = MonitorPeriod(hwnd);
        Trace("Smooth Motion display cadence %.6f Hz", motionPeriod > 0 ? 1e7 / motionPeriod.load() : 0);
    }
    const bool fields = frame.first != PL_FIELD_NONE;
    const int count = fields ? 2 : 1;
    for (int parity = 0; parity < count; parity++)
    {
        const auto middle = frame.start + (frame.stop - frame.start) / 2;
        auto start = parity ? middle : frame.start, stop = fields && !parity ? middle : frame.stop;
        const auto which = fields ? (parity ? pl_field_other(frame.first) : frame.first) : PL_FIELD_NONE;
        const auto after = fields ? (parity ? frame.first : pl_field_other(frame.first)) : PL_FIELD_NONE;
        const double period = motionPeriod.load();
        const bool scheduled =
            MotionCadenceSupported(frame.nominalDuration / count, period) && MotionIntervalValid(start, stop, period);
        const auto ticks = scheduled ? MotionTicks(start, stop, period) : std::vector<MotionTick>{};
        if (!scheduled)
        {
            // Unknown cadence, faster source, or invalid timestamps: ordinary delivery.
            ComPtr<IMediaSample> output;
            if (fields)
                output.Attach(new (std::nothrow) FieldSample(frame, past, future, parity));
            else
            {
                auto* fallback = new (std::nothrow) FieldSample(frame, past, future, which, after, start, stop, 0, 1);
                if (fallback)
                    fallback->motion = false;
                output.Attach(fallback);
            }
            if (!output)
                return E_OUTOFMEMORY;
            HRESULT hr = CBaseVideoRenderer::Receive(output.Get());
            if (FAILED(hr))
                return hr;
        }
        else
            for (const auto& tick : ticks)
            {
                ComPtr<IMediaSample> output;
                output.Attach(new (std::nothrow) FieldSample(
                    frame, past, future, which, after, tick.start, tick.stop, tick.position, tick.duration, afterNext));
                if (!output)
                    return E_OUTOFMEMORY;
                static_cast<FieldSample*>(output.Get())->temporal = temporal;
                HRESULT hr = CBaseVideoRenderer::Receive(output.Get());
                if (FAILED(hr))
                    return hr;
            }
    }
    return S_OK;
}
HRESULT Renderer::DeliverQueuedMotion()
try
{
    QueuedVideoFrame frame;
    uint64_t past = 0, future = 0, later = 0;
    auto temporal = std::make_shared<std::vector<TemporalFrame>>();
    {
        CAutoLock lock(&m_RendererLock);
        if (motionQueue.empty())
            return S_OK;
        frame = motionQueue.front();
        past = motionPast.empty() ? 0 : motionPast.back().id;
        future = motionQueue.size() > 1 ? motionQueue[1].id : 0;
        later = motionQueue.size() > 2 ? motionQueue[2].id : 0;
        if (motionQueue.size() > 1)
            frame.stop = MotionIntervalEnd(frame.start, frame.stop, motionQueue[1].start);
        for (const auto& f : motionPast)
            temporal->push_back({f.id, f.start, f.stop, f.first});
        for (const auto& f : motionQueue)
            temporal->push_back({f.id, f.start, f.stop, f.first});
        for (size_t i = 0; i + 1 < temporal->size(); i++)
        {
            auto& f = (*temporal)[i];
            f.stop = MotionIntervalEnd(f.start, f.stop, (*temporal)[i + 1].start);
        }
    }
    HRESULT hr = DeliverMotion(frame, past, future, later, temporal);
    if (SUCCEEDED(hr))
    {
        CAutoLock lock(&m_RendererLock);
        if (motionQueue.empty() || motionQueue.front().id != frame.id)
            return S_FALSE;
        motionQueue.erase(motionQueue.begin());
        motionPast.push_back(frame);
        while (motionPast.size() > static_cast<size_t>(bufferedLookahead))
            motionPast.erase(motionPast.begin());
        previous = frame;
    }
    return hr;
}
catch (const std::bad_alloc&)
{
    return E_OUTOFMEMORY;
}

HRESULT Renderer::DrainFields()
{
    for (;;)
    {
        {
            CAutoLock lock(&m_RendererLock);
            if (motionQueue.empty())
                break;
        }
        HRESULT hr = DeliverQueuedMotion();
        if (FAILED(hr))
            return hr;
    }
    QueuedVideoFrame frame, last;
    uint64_t past = 0;
    {
        CAutoLock lock(&m_RendererLock);
        frame = pending;
        last = followingQueued;
        past = previous.id;
        pending = {};
        followingQueued = {};
    }
    HRESULT hr = S_OK;
    if (frame.id)
        hr = smoothEnabled || frame.first == PL_FIELD_NONE ? DeliverMotion(frame, past, last.id)
                                                           : DeliverFields(frame, past, last.id);
    if (SUCCEEDED(hr) && last.id)
        hr = smoothEnabled || last.first == PL_FIELD_NONE ? DeliverMotion(last, frame.id, 0)
                                                          : DeliverFields(last, frame.id, 0);
    return hr;
}

HRESULT Renderer::Receive(IMediaSample* sample)
{
    if (!sample)
        return E_POINTER;
    CAutoLock receive(&receiveLock);
    const auto profileResult = UpdateDisplayProfile();
    if (FAILED(profileResult))
        return profileResult;
    AM_MEDIA_TYPE* changed = nullptr;
    if (sample->GetMediaType(&changed) == S_OK && changed)
    {
        CMediaType type(*changed);
        DeleteMediaType(changed);
        HRESULT hr = SetMediaType(&type);
        if (FAILED(hr))
            return hr;
    }
    MediaFormat current;
    int algorithm, fieldOverride;
    {
        CAutoLock state(&m_InterfaceLock);
        if (m_State == State_Stopped || m_pInputPin->IsFlushing())
            return VFW_E_WRONG_STATE;
        algorithm = settings.deinterlacing;
        fieldOverride = settings.fieldOverride;
        CAutoLock render(&m_RendererLock);
        current = format;
    }
    ComPtr<IMediaSample> reconstructed;
    if (dv.ShouldProcess())
    {
        ComPtr<IPin> upstream;
        m_pInputPin->ConnectedTo(&upstream);
        const auto supplemented =
            dv.Prepare(m_pGraph, upstream.Get(), sample, current, segment, backend.Device(), reconstructed);
        if (FAILED(supplemented))
            return supplemented;
        if (supplemented == S_OK && !reconstructed)
            return S_OK;
        if (reconstructed)
            sample = reconstructed.Get();
    }
    bool interlaced = !reconstructed && current.interlaced;
    bool top = current.topFieldFirst;
    DWORD fieldFlags = 0xffffffff;
    ComPtr<IMediaSample2> propsSample;
    if (interlaced && SUCCEEDED(sample->QueryInterface(IID_PPV_ARGS(&propsSample))))
    {
        AM_SAMPLE2_PROPERTIES props = {};
        if (SUCCEEDED(propsSample->GetProperties(sizeof(props), reinterpret_cast<BYTE*>(&props))))
        {
            fieldFlags = props.dwTypeSpecificFlags;
            interlaced = (fieldFlags & AM_VIDEO_FLAG_WEAVE) == 0;
            top = (fieldFlags & AM_VIDEO_FLAG_FIELD1FIRST) != 0;
        }
    }
    REFERENCE_TIME start = 0, stop = 0;
    const bool timed = SUCCEEDED(sample->GetTime(&start, &stop)) && stop > start;
    if (!reconstructed && current.interlaced && fieldOverride)
    {
        interlaced = true;
        top = fieldOverride == 1;
    }
    if (!fieldDecisionLogged)
    {
        fieldDecisionLogged = true;
        Trace("Deinterlacing input decision: algorithm=%d media_interlaced=%d sample_flags=%08lX interlaced=%d top=%d "
              "timed=%d start=%lld stop=%lld",
              algorithm,
              current.interlaced,
              fieldFlags,
              interlaced,
              top,
              timed,
              start,
              stop);
    }
    const bool fields = algorithm && interlaced && timed;
    const bool smooth = smoothEnabled && timed && sample->IsPreroll() != S_OK;
    if (buffering && (smoothBuffering != smooth || bufferedLookahead != motionLookahead))
    {
        HRESULT hr = DrainFields();
        if (FAILED(hr))
            return hr;
        CAutoLock render(&m_RendererLock);
        ResetFields(true, true);
    }
    if (!fields && !smooth)
    {
        HRESULT hr = DrainFields();
        if (FAILED(hr))
            return hr;
        {
            CAutoLock render(&m_RendererLock);
            if (buffering || previous.id)
                ResetFields(true, true);
        }
        return CBaseVideoRenderer::Receive(sample);
    }
    QueuedVideoFrame output, input, past;
    uint64_t future = 0;
    {
        CAutoLock render(&m_RendererLock);
        const auto expected = !motionQueue.empty() ? motionQueue.back().stop
                              : followingQueued.id ? followingQueued.stop
                                                   : pending.stop;
        const auto bufferedFirst = !motionQueue.empty() ? motionQueue.back().first : pending.first;
        const bool timelineReset =
            sample->IsDiscontinuity() == S_OK ||
            ((!motionQueue.empty() || followingQueued.id || pending.id) && std::abs(start - expected) > 20000);
        if (timelineReset || !buffering || bool(deinterlacingActive) != fields ||
            (fields && (!motionQueue.empty() || pending.id) && bufferedFirst != (top ? PL_FIELD_TOP : PL_FIELD_BOTTOM)))
            ResetFields(true, !timelineReset);
        input.frame = current.frame;
        ComPtr<IDvLayerSample> layered;
        sample->QueryInterface(IID_PPV_ARGS(&layered));
        if (layered)
        {
            HRESULT hr = DescribeDolbyVision(layered.Get(), input.frame);
            if (FAILED(hr))
                return hr;
        }
        else
            ReadHdrSideData(sample, input.frame);
        const auto source = dv.SourceInfo();
        if (source.present)
            input.frame.dvSource = source;
        input.start = start;
        input.stop = stop;
        input.nominalDuration = current.averageDuration;
        input.first = fields ? (top ? PL_FIELD_TOP : PL_FIELD_BOTTOM) : PL_FIELD_NONE;
        HRESULT hr;
        ComPtr<IMediaSampleD3D11> gpu;
        if (layered)
        {
            hr = backend.CaptureDolbyVision(input.frame, layered.Get(), input.id);
            input.hardware = SUCCEEDED(hr);
        }
        else if (SUCCEEDED(sample->QueryInterface(IID_PPV_ARGS(&gpu))))
        {
            ComPtr<ID3D11Texture2D> texture;
            UINT slice = 0;
            hr = gpu->GetD3D11Texture(0, &texture, &slice);
            if (SUCCEEDED(hr))
                hr = backend.CaptureImport(input.frame, texture.Get(), slice, input.id);
            input.hardware = SUCCEEDED(hr);
        }
        else
        {
            BYTE* bytes = nullptr;
            hr = sample->GetPointer(&bytes);
            if (SUCCEEDED(hr))
                hr = backend.CaptureUpload(input.frame,
                                           bytes,
                                           sample->GetActualDataLength() > 0 ? sample->GetActualDataLength()
                                                                             : sample->GetSize(),
                                           input.id);
        }
        if (FAILED(hr))
            return hr;
        buffering = true;
        smoothBuffering = smooth;
        deinterlacingActive = fields;
        if (smooth)
        {
            try
            {
                motionQueue.push_back(input);
            }
            catch (const std::bad_alloc&)
            {
                return E_OUTOFMEMORY;
            }
            if (motionQueue.size() <= static_cast<size_t>(bufferedLookahead))
                return S_OK;
        }
        else
        {
            output = pending;
            past = previous;
            previous = pending;
            pending = input;
            future = input.id;
        }
    }
    if (smooth)
        return DeliverQueuedMotion();
    return output.id ? DeliverFields(output, past.id, future) : S_OK;
}

HRESULT Renderer::ShouldDrawSampleNow(IMediaSample* sample, REFERENCE_TIME* start, REFERENCE_TIME* stop)
{
    // The base heuristic times DoRenderSample, which includes waiting for the
    // DXGI queue, as render cost. It then drops samples half a duration late:
    // about 4 ms for Smooth Motion's display-rate outputs, which ordinary clock
    // jitter reaches. Present such samples; drop only when clearly behind.
    const HRESULT hr = CBaseVideoRenderer::ShouldDrawSampleNow(sample, start, stop);
    REFERENCE_TIME now = 0;
    if (hr != E_FAIL || !m_pClock || FAILED(m_pClock->GetTime(&now)))
        return hr;
    const REFERENCE_TIME late = now - m_tStart - *start;
    const REFERENCE_TIME limit = std::max<REFERENCE_TIME>(2 * (*stop - *start), 250000);
    if (late > limit)
        return hr;
    // The base class marked a drop; keep its next decision on the normal path.
    m_nNormal = 0;
    return S_OK;
}

void Renderer::OnReceiveFirstSample(IMediaSample* sample)
{
    // The base renderer holds a paused sample until Run or the next flush.
    // Present its preview before that wait, including after a paused seek.
    SetPreviewed(nullptr);
    const HRESULT hr = DoRenderSample(sample);
    if (FAILED(hr))
        Trace("Paused preview failed result=%08lX", static_cast<unsigned long>(hr));
    else
        SetPreviewed(sample);
}

void Renderer::SetPreviewed(IMediaSample* sample)
{
    std::lock_guard guard(previewLock);
    if (sample)
        sample->AddRef();
    if (previewed)
        previewed->Release();
    previewed = sample;
}

bool Renderer::TakePreviewed(IMediaSample* sample)
{
    std::lock_guard guard(previewLock);
    // Any later render ends the preview: allocators recycle sample objects,
    // so only the very next render of the held sample is the duplicate.
    const bool same = previewed && previewed == sample;
    if (previewed)
        previewed->Release();
    previewed = nullptr;
    return same;
}

HRESULT Renderer::DoRenderSample(IMediaSample* sample)
{
    if (!sample)
        return E_POINTER;
    // Run delivers the held paused sample again; its preview is still on screen.
    if (TakePreviewed(sample))
        return S_OK;
    HRESULT hr = PrepareWindow();
    if (FAILED(hr))
        return hr;
    ComPtr<IMediaSample> fieldInterface;
    sample->QueryInterface(IID_InternalFieldSample, reinterpret_cast<void**>(fieldInterface.GetAddressOf()));
    auto field = fieldInterface ? static_cast<FieldSample*>(fieldInterface.Get()) : nullptr;
    VideoFrame frame = field ? field->frame.frame : format.frame;
    ComPtr<IDvLayerSample> layered;
    if (SUCCEEDED(sample->QueryInterface(IID_PPV_ARGS(&layered))))
    {
        hr = DescribeDolbyVision(layered.Get(), frame);
        if (FAILED(hr))
            return hr;
    }

    auto side = (field || layered) ? HdrSideDataStatus{} : ReadHdrSideData(sample, frame);
    if (!field)
    {
        const auto source = dv.SourceInfo();
        if (source.present)
            frame.dvSource = source;
    }
    if (!field && (!(sideDataLogged & 32) || (side.dovi && !(sideDataLogged & 64))))
    {
        sideDataLogged |= 32 | (side.dovi ? 64 : 0);
        Trace("Dolby Vision side data: result=%08lX bytes=%zu accepted=%d source_max=%.2f Level1_max_pq=%.6f "
              "Level1_avg_pq=%.6f",
              static_cast<unsigned long>(side.doviResult),
              side.doviBytes,
              side.dovi,
              frame.color.hdr.max_luma,
              frame.color.hdr.max_pq_y,
              frame.color.hdr.avg_pq_y);
    }
    if (!field && (!(sideDataLogged & 8) || side.dynamic && !(sideDataLogged & 16)))
    {
        sideDataLogged |= 8 | (side.dynamic ? 16 : 0);
        Trace("HDR10+ side data: result=%08lX bytes=%zu available=%d used=%d scene_max=%.2f/%.2f/%.2f scene_avg=%.2f "
              "target=%.2f",
              static_cast<unsigned long>(side.dynamicResult),
              side.dynamicBytes,
              side.dynamic,
              side.dynamic && !frame.dovi,
              frame.color.hdr.scene_max[0],
              frame.color.hdr.scene_max[1],
              frame.color.hdr.scene_max[2],
              frame.color.hdr.scene_avg,
              frame.color.hdr.ootf.target_luma);
    }
    unsigned observed = 1 | (side.mastering ? 2 : 0) | (side.light ? 4 : 0);
    if (observed & ~sideDataLogged)
    {
        sideDataLogged |= observed;
        Trace("HDR side data: interface=%08lX mastering=%08lX bytes=%zu light=%08lX bytes=%zu max_nits=%.4f "
              "min_nits=%.6f MaxCLL=%.1f MaxFALL=%.1f",
              static_cast<unsigned long>(side.interfaceResult),
              static_cast<unsigned long>(side.masteringResult),
              side.masteringBytes,
              static_cast<unsigned long>(side.lightResult),
              side.lightBytes,
              frame.color.hdr.max_luma,
              frame.color.hdr.min_luma,
              frame.color.hdr.max_cll,
              frame.color.hdr.max_fall);
    }
    REFERENCE_TIME start = 0, stop = 0;
    if (SUCCEEDED(sample->GetTime(&start, &stop)))
    {
        frameStart = start + segment;
        frameStop = stop + segment;
        frameDuration = stop - start;
    }
    ComPtr<IMediaSampleD3D11> gpuSample;
    bool hardware = false;
    if (field)
    {
        hr = field->motion
                 ? backend.SelectMix(field->frame.id,
                                     field->previous,
                                     field->next,
                                     field->field,
                                     field->frame.first,
                                     field->following,
                                     field->position,
                                     field->duration,
                                     field->afterNext)
                 : backend.SelectField(field->frame.id, field->previous, field->next, field->field, field->frame.first);
        if (SUCCEEDED(hr) && field->motion && field->temporal && motionRadius > 0)
            hr = backend.SelectTemporalMix(*field->temporal,
                                           field->frame.id,
                                           field->previous,
                                           field->next,
                                           field->field,
                                           field->frame.first,
                                           start,
                                           field->duration);
        hardware = field->frame.hardware;
    }
    else if (layered)
    {
        hr = backend.ImportDolbyVision(frame, layered.Get());
        hardware = SUCCEEDED(hr);
    }
    else if (SUCCEEDED(sample->QueryInterface(IID_PPV_ARGS(&gpuSample))))
    {
        ComPtr<ID3D11Texture2D> texture;
        UINT slice = 0;
        hr = gpuSample->GetD3D11Texture(0, &texture, &slice);
        if (SUCCEEDED(hr))
            hr = backend.Import(frame, texture.Get(), slice);
        hardware = SUCCEEDED(hr);
    }
    else
    {
        BYTE* bytes = nullptr;
        hr = sample->GetPointer(&bytes);
        const long actual = sample->GetActualDataLength();
        if (SUCCEEDED(hr))
            hr = backend.Upload(frame, bytes, actual > 0 ? actual : sample->GetSize());
    }
    int64_t dueQpc = 0;
    if (m_State == State_Running && m_pClock && stop > start)
    {
        REFERENCE_TIME clock = 0;
        LARGE_INTEGER before = {}, after = {}, frequency = {};
        QueryPerformanceCounter(&before);
        if (SUCCEEDED(m_pClock->GetTime(&clock)))
        {
            QueryPerformanceCounter(&after);
            QueryPerformanceFrequency(&frequency);
            const auto delta = static_cast<REFERENCE_TIME>(m_tStart) + start - clock;
            if (std::abs(static_cast<double>(delta)) < 60e7)
                dueQpc = (before.QuadPart + after.QuadPart) / 2 +
                         static_cast<int64_t>(double(delta) * frequency.QuadPart / 1e7);
        }
    }
    if (sample->IsDiscontinuity() == S_OK)
        backend.ResetPresentationMeasurement();
    backend.SetPresentationTiming(dueQpc, m_State == State_Running, start);
    int dropped = 0;
    get_FramesDroppedInRenderer(&dropped);
    backend.SetStatisticsInput(format.averageDuration > 0 ? 1e7 / format.averageDuration : 0, hardware, dropped);
    if (SUCCEEDED(hr))
        hr = backend.Present();
    if (hr == S_OK && m_State == State_Running)
        lastTimedPresent = GetTickCount64();
    if (TraceEnabled())
    {
        // Opt-in diagnostics: name the moment a drop, repeat or glitch appears.
        const auto measured = backend.Measurements();
        const uint64_t drops = measured.drops, repeats = measured.repeats, glitches = measured.glitches;
        if (drops != traceDrops || repeats != traceRepeats || glitches != traceGlitches)
            Trace("Counters drop %llu->%llu rep %llu->%llu glitch %llu->%llu at stream %lld ms",
                  static_cast<unsigned long long>(traceDrops),
                  static_cast<unsigned long long>(drops),
                  static_cast<unsigned long long>(traceRepeats),
                  static_cast<unsigned long long>(repeats),
                  static_cast<unsigned long long>(traceGlitches),
                  static_cast<unsigned long long>(glitches),
                  static_cast<long long>(start / 10000));
        traceDrops = drops;
        traceRepeats = repeats;
        traceGlitches = glitches;
    }
    PlaceboStatus snapshot;
    HRESULT previousResult;
    {
        std::lock_guard stats(statusLock);
        previousResult = status.lastResult;
        if (hardware)
            status.hardwareFrames++;
        if (hr == S_OK)
        {
            status.rendered++;
            if (field && field->field != PL_FIELD_NONE)
                status.deinterlacedFields++;
            if (field && field->motion && smoothEnabled)
                status.mixedFrames++;
        }
        status.hdrSource = pl_color_space_is_hdr(&frame.color);
        status.lastResult = hr;
        status.masteringMaxNits = frame.color.hdr.max_luma;
        status.masteringMinNits = frame.color.hdr.min_luma;
        status.maxCLL = frame.color.hdr.max_cll;
        status.maxFALL = frame.color.hdr.max_fall;
        snapshot = status;
    }
    if (hr == S_OK && snapshot.rendered == 1)
        Trace("First video frame presented; hardware=%llu HDR=%d mastering_max=%.1f MaxCLL=%.1f SmoothMotion=%d "
              "displayHz=%.6f",
              snapshot.hardwareFrames,
              pl_color_space_is_hdr(&frame.color),
              frame.color.hdr.max_luma,
              frame.color.hdr.max_cll,
              field && field->motion && smoothEnabled,
              motionPeriod > 0 ? 1e7 / motionPeriod.load() : 0);
    {
        std::lock_guard stats(statusLock);
        diagnostics.result = hr;
        if (hr == S_OK)
        {
            diagnostics.haveVideo = TRUE;
            diagnostics.outputTextureFormat = backend.OutputTextureFormat();
            diagnostics.outputLevels = backend.OutputLevels();
            diagnostics.ditherDepth = backend.OutputDitherDepth();
            diagnostics.sharpenState = backend.SharpenState();
            diagnostics.directGpu = hardware;
            diagnostics.width = frame.width;
            diagnostics.height = frame.height;
            diagnostics.bitDepth = Layout(frame.format).depth;
            strcpy_s(diagnostics.pixelFormat, Layout(frame.format).name);
            diagnostics.signal = frame.dovi ? SourceSignal::DolbyVision
                                 : pl_hdr_metadata_contains(&frame.color.hdr, PL_HDR_METADATA_HDR10PLUS)
                                     ? SourceSignal::HDR10Plus
                                 : frame.color.transfer == PL_COLOR_TRC_HLG ? SourceSignal::HLG
                                 : pl_color_space_is_hdr(&frame.color)      ? SourceSignal::HDR10
                                                                            : SourceSignal::SDR;
            diagnostics.smoothMotion = field && field->motion && smoothEnabled;
            diagnostics.displayHz = motionPeriod > 0 ? static_cast<float>(1e7 / motionPeriod.load()) : 0;
            diagnostics.deinterlacing = field && field->field != PL_FIELD_NONE ? settings.deinterlacing : 0;
            diagnostics.sourcePeak = frame.color.hdr.max_luma;
            diagnostics.scenePeak =
                frame.dovi && frame.color.hdr.max_pq_y > 0
                    ? pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, frame.color.hdr.max_pq_y)
                    : std::max(
                          {frame.color.hdr.scene_max[0], frame.color.hdr.scene_max[1], frame.color.hdr.scene_max[2]});
            diagnostics.frames = snapshot.rendered;
        }
    }
    if (hr == S_OK && snapshot.deinterlacedFields == 120 && field)
        Trace("Deinterlacing verified: 120 field outputs; source=%dx%d first=%s algorithm=%s",
              frame.width,
              frame.height,
              field->frame.first == PL_FIELD_TOP ? "TFF" : "BFF",
              settings.deinterlacing == 2 ? "Yadif" : "Bwdif");
    if (hr == S_OK && snapshot.rendered == 120)
        Trace("Playback verified at 120 presented frames; hardware=%llu CPU=%llu HDR=%d DolbyVision=%d "
              "SmoothMotion=%llu Sharpen=%d",
              snapshot.hardwareFrames,
              snapshot.rendered - snapshot.hardwareFrames,
              pl_color_space_is_hdr(&frame.color),
              !!frame.dovi,
              snapshot.mixedFrames,
              backend.SharpenState());
    if (FAILED(hr) && hr != previousResult)
        Trace(
            "Video frame failed result=%08lX details=%s", static_cast<unsigned long>(hr), backend.LastError().c_str());
    return hr;
}

void Renderer::ToggleStatistics()
{
    backend.ShowStatistics(!backend.StatisticsVisible());
    // Running playback shows or hides the overlay with its next scheduled
    // frame. An extra untimed present here would repeat the current frame,
    // delay the next one and count as repeats, glitches and drops.
    if (windowState.load() != State_Running && window && window->WindowExists())
        PostMessageW(window->GetWindowHWND(), WM_PAINT, 0, 0);
}

void Renderer::Repaint(const char* source)
{
    // During playback the next scheduled frame follows within one display
    // period and already renders at the new size, monitor and settings. An
    // extra untimed present here repeats the current frame, takes a vsync from
    // the schedule and makes the following frames late (repeats and drops).
    // After the end of the stream nothing follows, so keep repainting there.
    if (windowState.load() == State_Running && GetTickCount64() - lastTimedPresent.load() < 250)
    {
        Trace("Repaint during playback left to the next frame: %s", source);
        return;
    }
    // The host can synchronously resize the video while holding this lock.
    // Never block its window thread behind that call; retry the repaint later.
    if (!m_InterfaceLock.TryLock())
    {
        if (window && window->WindowExists())
            SetTimer(window->GetWindowHWND(), 0x46525950, 16, nullptr);
        return;
    }

    struct Unlock
    {
        CCritSec& lock;

        ~Unlock()
        {
            lock.Unlock();
        }
    } unlock{m_InterfaceLock};

    if (FAILED(UpdateDisplayProfile()))
        return;
    const auto result = backend.TryPresent();
    // A resize can race the scheduled GPU presentation. Keep the redraw
    // pending even while paused, when no new video sample will repaint it.
    if (result == HRESULT_FROM_WIN32(ERROR_BUSY) && window && window->WindowExists())
        SetTimer(window->GetWindowHWND(), 0x46525950, 16, nullptr);
    if (result != S_OK)
        Trace("Repaint not presented result=%08lX", static_cast<unsigned long>(result));
}

void Renderer::NewSegment(REFERENCE_TIME start)
{
    dv.ResetTimeline();
    dv.Resume();
    CAutoLock guard(&m_RendererLock);
    segment = start;
    ResetFields();
}

HRESULT Renderer::BeginFlush()
{
    SetPreviewed(nullptr);
    return CBaseVideoRenderer::BeginFlush();
}

HRESULT Renderer::EndFlush()
{
    dv.Resume();
    ResetFields();
    return CBaseVideoRenderer::EndFlush();
}

STDMETHODIMP Renderer::Stop()
{
    Trace("Renderer stop begin");
    dv.Cancel();
    SetPreviewed(nullptr);
    HRESULT hr = CBaseVideoRenderer::Stop();
    if (SUCCEEDED(hr))
        windowState = State_Stopped;
    {
        CAutoLock receive(&receiveLock);
        dv.ResetTimeline();
        CAutoLock lock(&m_RendererLock);
        ResetFields();
    }
    // Stop also occurs during an audio-track rebuild. It ends scheduling,
    // not the video connection: discard temporal inputs but leave the last
    // presented swapchain surface alone until a new picture arrives.
    Trace("Renderer stop completed result=%08lX", static_cast<unsigned long>(hr));
    return hr;
}

STDMETHODIMP Renderer::Run(REFERENCE_TIME start)
{
    Trace("Renderer run begin start=%lld", start);
    backend.ResetPresentationMeasurement();
    HRESULT hr = CBaseVideoRenderer::Run(start);
    Trace("Renderer base run returned result=%08lX", static_cast<unsigned long>(hr));
    if (SUCCEEDED(hr))
        windowState = State_Running;
    if (SUCCEEDED(hr) && window && window->WindowExists() && window->IsAutoShowEnabled())
        window->DoShowWindow(SW_SHOWNOACTIVATE);
    return hr;
}

STDMETHODIMP Renderer::Pause()
{
    Trace("Renderer pause begin");
    backend.ResetPresentationMeasurement();
    // Stop and disconnect cancel waiting DV work. A new streaming run starts
    // here, not at Run: the decoder already delivers the preroll while paused,
    // and a still-cancelled processor would silently swallow those samples.
    {
        CAutoLock state(&m_InterfaceLock);
        if (m_State == State_Stopped)
            dv.Resume();
    }
    HRESULT hr = CBaseVideoRenderer::Pause();
    Trace("Renderer base pause returned result=%08lX", static_cast<unsigned long>(hr));
    if (SUCCEEDED(hr))
        windowState = State_Paused;
    if (SUCCEEDED(hr) && window && window->WindowExists() && window->IsAutoShowEnabled())
        window->DoShowWindow(SW_SHOWNOACTIVATE);
    return hr;
}

HRESULT Renderer::OnStartStreaming()
{
    ComPtr<IPin> peer;
    auto input = GetPin(0);
    if (input && SUCCEEDED(input->ConnectedTo(&peer)))
        backend.SetSourceCodec(SourceCodecFromOutput(peer.Get()));
    dv.RefreshSource(m_pGraph, peer.Get());
    dv.Resume();
    int rawDrops = 0;
    get_FramesDroppedInRenderer(&rawDrops);
    backend.RestartDropMeasurement(rawDrops);
    return CBaseVideoRenderer::OnStartStreaming();
}

STDMETHODIMP Renderer::GetPages(CAUUID* pages)
{
    if (!pages)
        return E_POINTER;
    pages->cElems = 0;
    pages->pElems = static_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID)));
    if (!pages->pElems)
        return E_OUTOFMEMORY;
    pages->pElems[0] = CLSID_FrankyDirect2DPage;
    pages->cElems = 1;
    return S_OK;
}

STDMETHODIMP Renderer::SetCallback11(ISubRender11Callback* next)
{
    Trace("Subtitle callback %s", next ? "attached" : "detached");

    struct Bridge
    {
        ISubRender11Callback* callback = nullptr;
        ComPtr<ISubRender11Callback> retained;
        ComPtr<ID3D11Device> device;

        ~Bridge()
        {
            Trace("Subtitle bridge destruction begin; device=%d", !!device);
            if (device)
            {
                callback->SetDevice11(nullptr);
                Trace("Subtitle device detached");
            }
            retained.Reset();
            Trace("Subtitle callback reference released");
        }
    };

    if (!next)
    {
        backend.SetOverlayPainter({});
        return S_OK;
    }
    ComPtr<IUnknown> callbackIdentity, ownerIdentity;
    HRESULT hr = next->QueryInterface(IID_PPV_ARGS(&callbackIdentity));
    if (FAILED(hr))
        return hr;
    hr = GetOwner()->QueryInterface(IID_PPV_ARGS(&ownerIdentity));
    if (FAILED(hr))
        return hr;
    auto bridge = std::make_shared<Bridge>();
    bridge->callback = next;
    // An aggregate's controlling unknown already owns the renderer. Retaining
    // that same object as our callback would keep both alive after file change.
    // Independent callback objects still need their own lifetime reference.
    if (callbackIdentity.Get() != ownerIdentity.Get())
        bridge->retained = next;
    Trace("Subtitle callback ownership: aggregate=%d", !bridge->retained);
    backend.SetOverlayPainter(
        [this, bridge](ID3D11Device* device, ID3D11DeviceContext*, int width, int height, const RECT& video) -> HRESULT
        {
            if (bridge->device.Get() != device)
            {
                if (bridge->device)
                {
                    bridge->callback->SetDevice11(nullptr);
                    bridge->device.Reset();
                }
                HRESULT hr = bridge->callback->SetDevice11(device);
                if (FAILED(hr))
                    return hr;
                bridge->device = device;
            }
            const RECT viewport = {0, 0, width, height};
            HRESULT hr = bridge->callback->Render11(
                frameStart.load(), frameStop.load(), frameDuration.load(), video, video, viewport, 1., 0, 0);
            // MPC-BE uses E_FAIL for an empty subtitle queue. The overlay was
            // cleared before the callback; keep the video and its empty overlay.
            // Device initialization failures above remain actual errors.
            return hr == E_FAIL ? S_FALSE : hr;
        },
        true);
    return S_OK;
}

STDMETHODIMP Renderer::GetSettings(PlaceboSettings* out)
{
    if (!out)
        return E_POINTER;
    const auto profileResult = UpdateDisplayProfile();
    if (FAILED(profileResult))
        return profileResult;
    CAutoLock guard(&m_InterfaceLock);
    *out = settings;
    return S_OK;
}

STDMETHODIMP Renderer::SetSettings(const PlaceboSettings* next)
{
    if (!next)
        return E_POINTER;
    HRESULT hr = ValidateSettings(*next);
    if (FAILED(hr))
        return hr;
    CAutoLock guard(&m_InterfaceLock);
    PlaceboSettings selected = *next;
    if (!selected.displayProfile[0])
        wcscpy_s(selected.displayProfile, settings.displayProfile);
    next = &selected;
    Trace("Profile edit selected=%ls active=%ls HDRMode=%d brightness=%.4f monitor=%p",
          next->displayProfile,
          settings.displayProfile,
          next->hdrMode,
          next->brightness,
          profileMonitor.load());
    // Editing another device must not change the currently playing display.
    if (wcscmp(next->displayProfile, settings.displayProfile))
        return SaveSettings(*next);
    Trace("Settings requested: HDR=%d AutoHDR=%d", next->hdrPassthrough, next->autoHdr);
    hr = backend.SetSettings(BackendSettings(*next));
    if (FAILED(hr))
        return hr;
    hr = SaveSettings(*next);
    if (FAILED(hr))
    {
        backend.SetSettings(BackendSettings(settings));
        return hr;
    }
    settings = *next;
    smoothEnabled = !!settings.smoothMotion;
    motionRadius = BackendSettings(settings).motionRadius;
    motionLookahead = std::max(2, static_cast<int>(std::ceil(motionRadius.load())) + 1);
    // Never render or change the display mode synchronously from the page.
    if (window && window->WindowExists())
        PostMessageW(window->GetWindowHWND(), WM_PAINT, 0, 0);
    Trace("Settings queued without waiting for renderer");
    return S_OK;
}

HRESULT Renderer::UpdateDisplayProfile(bool refresh)
{
    if (!window || !window->WindowExists())
        return S_OK;
    const auto monitor = MonitorFromWindow(window->GetWindowHWND(), MONITOR_DEFAULTTONEAREST);
    if (!refresh && monitor == profileMonitor.load())
        return S_OK;
    CAutoLock guard(&m_InterfaceLock);
    if (!refresh && monitor == profileMonitor)
        return S_OK;
    const auto devices = DisplayProfiles::Connected();
    const auto persisted = EnsureDisplayProfiles(devices, LoadSettings());
    if (FAILED(persisted))
        return persisted;
    const auto found =
        std::find_if(devices.begin(), devices.end(), [&](const auto& d) { return d.monitor == monitor; });
    if (found == devices.end())
        return S_OK;
    if (found->id == settings.displayProfile)
    {
        profileMonitor = monitor;
        return S_OK;
    }
    auto selected = LoadSettings(found->id);
    auto hr = backend.SetSettings(BackendSettings(selected));
    if (FAILED(hr) && (selected.iccEnabled || selected.lutEnabled))
    {
        selected.iccEnabled = selected.lutEnabled = FALSE;
        hr = backend.SetSettings(BackendSettings(selected));
    }
    if (FAILED(hr))
        return hr;
    settings = selected;
    Trace("Profile activated id=%ls monitor=%p HDRMode=%d brightness=%.4f",
          settings.displayProfile,
          monitor,
          settings.hdrMode,
          settings.brightness);
    profileMonitor = monitor;
    smoothEnabled = !!settings.smoothMotion;
    motionRadius = BackendSettings(settings).motionRadius;
    motionLookahead = std::max(2, static_cast<int>(std::ceil(motionRadius.load())) + 1);
    refreshDirty = true;
    return S_OK;
}

STDMETHODIMP Renderer::GetStatus(PlaceboStatus* out)
{
    if (!out)
        return E_POINTER;
    std::lock_guard stats(statusLock);
    *out = status;
    return S_OK;
}

STDMETHODIMP Renderer::GetDiagnostics(PlaceboDiagnostics* out)
{
    if (!out)
        return E_POINTER;
    if (out->size != sizeof(PlaceboDiagnostics))
        return E_INVALIDARG;
    std::lock_guard stats(statusLock);
    *out = diagnostics;
    return S_OK;
}

STDMETHODIMP Renderer::ReadPixels(int width, int height, BYTE* rgba, SIZE_T bytes)
{
    if (!rgba || width <= 0 || height <= 0 || static_cast<uint64_t>(width) * height * 4 > bytes)
        return E_INVALIDARG;
    std::vector<uint8_t> pixels;
    HRESULT hr = backend.RenderOffscreen(width, height, pixels);
    if (SUCCEEDED(hr))
    {
        if (pixels.size() > bytes)
            return E_OUTOFMEMORY;
        memcpy(rgba, pixels.data(), pixels.size());
    }
    return hr;
}

STDMETHODIMP Renderer::Flt_GetBin(LPCSTR field, LPVOID* value, unsigned* size)
{
    if (!field || !value || !size)
        return E_POINTER;
    *value = nullptr;
    *size = 0;
    if (strcmp(field, "displayedImage"))
        return E_INVALIDARG;
    CAutoLock guard(&m_InterfaceLock);
    RECT viewport = {};
    if (!window || !window->WindowExists() || !GetClientRect(window->GetWindowHWND(), &viewport))
        return VFW_E_NOT_CONNECTED;
    std::vector<uint8_t> dib;
    const HRESULT hr = CaptureDib(backend, viewport.right, viewport.bottom, true, dib);
    if (FAILED(hr))
        return hr;
    // MPC-BE's allocator presenter releases this DIB with LocalFree.
    const auto result = LocalAlloc(LMEM_FIXED, dib.size());
    if (!result)
        return E_OUTOFMEMORY;
    memcpy(result, dib.data(), dib.size());
    *value = result;
    *size = static_cast<unsigned>(dib.size());
    return S_OK;
}

STDMETHODIMP Renderer::Flt_GetBool(LPCSTR field, bool* value)
{
    if (!field || !value)
        return E_POINTER;
    Trace("Host Flt_GetBool %s", field);
    if (!strcmp(field, "doubleRate"))
    {
        *value = deinterlacingActive;
        return S_OK;
    }
    if (!strcmp(field, "statsEnable"))
    {
        *value = backend.StatisticsVisible();
        return S_OK;
    }
    if (!strcmp(field, "flip"))
    {
        *value = backend.Flip();
        return S_OK;
    }
    return E_INVALIDARG;
}

STDMETHODIMP Renderer::Flt_GetInt(LPCSTR field, int* value)
{
    if (!field || !value)
        return E_POINTER;
    Trace("Host Flt_GetInt %s", field);
    if (!strcmp(field, "renderType"))
    {
        *value = 11;
        return S_OK;
    }
    if (!strcmp(field, "rotation"))
    {
        *value = backend.Rotation();
        return S_OK;
    }
    if (!strcmp(field, "playbackState"))
    {
        *value = m_State;
        return S_OK;
    }
    return E_INVALIDARG;
}

STDMETHODIMP Renderer::Flt_GetInt64(LPCSTR field, __int64* value)
{
    if (!field || !value)
        return E_POINTER;
    if (!strcmp(field, "placeboStatsRtxFrames"))
    {
        *value = backend.RtxFrames();
        return S_OK;
    }
    if (!strcmp(field, "placeboShaderCacheLoaded"))
    {
        *value = backend.CacheLoadedObjects();
        return S_OK;
    }
    if (!strcmp(field, "placeboShaderCacheObjects"))
    {
        *value = backend.CacheObjects();
        return S_OK;
    }
    Trace("Host Flt_GetInt64 %s", field);
    if (!strncmp(field, "placeboStats", 12))
    {
        const auto stats = backend.Measurements();
        if (!strcmp(field, "placeboStatsSubmissions"))
            *value = stats.submissions;
        else if (!strcmp(field, "placeboStatsConfirmed"))
            *value = stats.confirmed;
        else if (!strcmp(field, "placeboStatsRepeats"))
        {
            if (!stats.countersAvailable)
                return E_ABORT;
            *value = stats.repeats;
        }
        else if (!strcmp(field, "placeboStatsGlitches"))
        {
            if (!stats.countersAvailable)
                return E_ABORT;
            *value = stats.glitches;
        }
        else if (!strcmp(field, "placeboStatsDrops"))
            *value = stats.drops;
        else if (!strcmp(field, "placeboStatsTimingActive"))
            *value = stats.timing;
        else if (!strcmp(field, "placeboStatsPending"))
        {
            if (!stats.feedback)
                return E_ABORT;
            *value = stats.pending;
        }
        else if (!strcmp(field, "placeboStatsRenderGpuSamples"))
            *value = stats.renderGpuSamples;
        else
            return E_INVALIDARG;
        return S_OK;
    }
    if (!strcmp(field, "originalVideoSize"))
    {
        PlaceboStatus current;
        GetStatus(&current);
        SIZE dimensions = {current.width, current.height};
        memcpy(value, &dimensions, sizeof(dimensions));
        return S_OK;
    }
    // Do not claim the version/features of the original MPC implementation.
    return E_INVALIDARG;
}

STDMETHODIMP Renderer::Flt_GetDouble(LPCSTR field, double* value)
{
    if (!field || !value)
        return E_POINTER;
    if (strncmp(field, "placeboStats", 12))
        return E_INVALIDARG;
    const auto stats = backend.Measurements();
    if (!strcmp(field, "placeboStatsRenderCpuMs"))
    {
        if (!stats.renderCpuSamples)
            return E_ABORT;
        *value = stats.renderCpuAverage;
    }
    else if (!strcmp(field, "placeboStatsRenderGpuMs"))
    {
        if (!stats.renderGpuSamples)
            return E_ABORT;
        *value = stats.renderGpuAverage;
    }
    else if (!strcmp(field, "placeboStatsPresentCpuMs"))
    {
        if (!stats.presentCpuSamples)
            return E_ABORT;
        *value = stats.presentCpuAverage;
    }
    else
        return E_INVALIDARG;
    return S_OK;
}

STDMETHODIMP Renderer::Flt_SetBool(LPCSTR field, bool value)
{
    if (!field)
        return E_POINTER;
    Trace("Host Flt_SetBool %s=%d", field, value);
    if (!strcmp(field, "flip"))
    {
        backend.SetFlip(value);
        if (window && window->WindowExists())
            PostMessageW(window->GetWindowHWND(), WM_PAINT, 0, 0);
        return S_OK;
    }
    if (!strcmp(field, "statsEnable"))
    {
        backend.ShowStatistics(value);
        // As with Ctrl+J: running playback picks up the change on its next frame.
        if (windowState.load() != State_Running && window && window->WindowExists())
            PostMessageW(window->GetWindowHWND(), WM_PAINT, 0, 0);
        return S_OK;
    }
    if (!strcmp(field, "cmd_redraw"))
    {
        if (value && window && window->WindowExists())
            PostMessageW(window->GetWindowHWND(), WM_PAINT, 0, 0);
        return S_OK;
    }
    return E_INVALIDARG;
}

STDMETHODIMP Renderer::Flt_SetInt(LPCSTR field, int value)
{
    if (!field)
        return E_POINTER;
    if (strcmp(field, "rotation"))
        return E_INVALIDARG;
    const HRESULT hr = backend.SetRotation(value);
    if (SUCCEEDED(hr) && window && window->WindowExists())
        PostMessageW(window->GetWindowHWND(), WM_PAINT, 0, 0);
    return hr;
}
