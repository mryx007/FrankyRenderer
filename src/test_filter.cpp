#include "Diagnostics.h"
#define NOMINMAX
#include <windows.h>
#include "DirectShow/external/BaseClasses/streams.h"
#include <dvdmedia.h>
#include <wrl/client.h>
#include "Settings.h"
#include "SettingsChoices.h"
#include "LegacyPropertyPages.h"
#include "StatisticsKey.h"
#include <commctrl.h>
#include "DirectShow/Include/FilterInterfaces.h"
#include "DirectShow/Include/ISubRender11.h"
#include <atomic>
#include <thread>
#include <d3d11.h>
#include "DirectShow/Include/ID3DVideoMemoryConfiguration.h"
#include <initguid.h>
#include "DirectShow/Include/IMediaSideData.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
using Microsoft::WRL::ComPtr;
HINSTANCE g_hInst = nullptr;

static void Check(HRESULT hr, const char* text)
{
    if (FAILED(hr))
    {
        printf("FAIL %s %08lX\n", text, static_cast<unsigned long>(hr));
        exit(1);
    }
}

static bool hardware = false;
static bool measureTest = false;
static bool startupTest = false;
static bool mpcbeOwnerTest = false;

// MPC-BE's allocator presenter aggregates the renderer and forwards interface
// requests to its non-delegating IUnknown. Exercise that ownership as well as
// its early put_Owner call, rather than testing only an unaggregated filter.
class TestPresenter final : public IUnknown
{
    std::atomic<ULONG> refs = 1;
    ComPtr<IUnknown> inner;

  public:
    HRESULT Initialize(IClassFactory* factory)
    {
        return factory->CreateInstance(this, IID_PPV_ARGS(&inner));
    }

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown)
        {
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        return inner ? inner->QueryInterface(id, out) : E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++refs;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG remaining = --refs;
        if (!remaining)
            delete this;
        return remaining;
    }
};

static bool interlacedTest = false, bottomFirst = false, weaveTest = false, eosTest = false, yadifTest = false,
            forceBffTest = false, motionTest = false, sharpenTest = false;
static bool flushRaceTest = false, rtxTest = false;
static std::atomic<bool> blockSampleTime = false;
static HANDLE sampleTimeEntered = nullptr, sampleTimeRelease = nullptr;
static DXGI_FORMAT textureFormat = DXGI_FORMAT_P010;
static UINT textureBind = D3D11_BIND_DECODER;
static std::atomic<HRESULT> sourceFailure = S_OK;

static DWORD FourCC()
{
    return textureFormat == DXGI_FORMAT_NV12   ? MAKEFOURCC('N', 'V', '1', '2')
           : textureFormat == DXGI_FORMAT_P016 ? MAKEFOURCC('P', '0', '1', '6')
                                               : MAKEFOURCC('P', '0', '1', '0');
}

static long SampleBytes()
{
    return 64 * 64 * 3 / (textureFormat == DXGI_FORMAT_NV12 ? 2 : 1);
}

class TextureSample final : public IMediaSample, public IMediaSampleD3D11
{
    std::atomic<ULONG> refs = 1;
    ComPtr<IMediaSample> sample;
    ComPtr<ID3D11Texture2D> texture;

  public:
    TextureSample(IMediaSample* source, ID3D11Texture2D* tex) : sample(source), texture(tex) {}

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IMediaSample)
            *out = static_cast<IMediaSample*>(this);
        else if (id == __uuidof(IMediaSampleD3D11))
            *out = static_cast<IMediaSampleD3D11*>(this);
        else if (id == __uuidof(IMediaSideData) || id == IID_IMediaSample2)
            return sample->QueryInterface(id, out);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++refs;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }

    STDMETHODIMP GetD3D11Texture(int view, ID3D11Texture2D** out, UINT* slice) override
    {
        if (!out || !slice)
            return E_POINTER;
        if (view)
            return E_INVALIDARG;
        *out = texture.Get();
        (*out)->AddRef();
        *slice = 1;
        return S_OK;
    }

    STDMETHODIMP GetPointer(BYTE** p) override
    {
        return sample->GetPointer(p);
    }

    STDMETHODIMP_(long) GetSize() override
    {
        return sample->GetSize();
    }

    STDMETHODIMP GetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override
    {
        if (blockSampleTime.exchange(false))
        {
            SetEvent(sampleTimeEntered);
            if (WaitForSingleObject(sampleTimeRelease, 5000) != WAIT_OBJECT_0)
                return E_FAIL;
        }
        return sample->GetTime(a, b);
    }

    STDMETHODIMP SetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override
    {
        return sample->SetTime(a, b);
    }

    STDMETHODIMP IsSyncPoint() override
    {
        return sample->IsSyncPoint();
    }

    STDMETHODIMP SetSyncPoint(BOOL b) override
    {
        return sample->SetSyncPoint(b);
    }

    STDMETHODIMP IsPreroll() override
    {
        return sample->IsPreroll();
    }

    STDMETHODIMP SetPreroll(BOOL b) override
    {
        return sample->SetPreroll(b);
    }

    STDMETHODIMP_(long) GetActualDataLength() override
    {
        return sample->GetActualDataLength();
    }

    STDMETHODIMP SetActualDataLength(long n) override
    {
        return sample->SetActualDataLength(n);
    }

    STDMETHODIMP GetMediaType(AM_MEDIA_TYPE** mt) override
    {
        return sample->GetMediaType(mt);
    }

    STDMETHODIMP SetMediaType(AM_MEDIA_TYPE* mt) override
    {
        return sample->SetMediaType(mt);
    }

    STDMETHODIMP IsDiscontinuity() override
    {
        return sample->IsDiscontinuity();
    }

    STDMETHODIMP SetDiscontinuity(BOOL b) override
    {
        return sample->SetDiscontinuity(b);
    }

    STDMETHODIMP GetMediaTime(LONGLONG* a, LONGLONG* b) override
    {
        return sample->GetMediaTime(a, b);
    }

    STDMETHODIMP SetMediaTime(LONGLONG* a, LONGLONG* b) override
    {
        return sample->SetMediaTime(a, b);
    }
};

class SubtitleProbe final : public ISubRender11Callback
{
    std::atomic<ULONG> refs = 1;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11ShaderResourceView> bitmap;
    ComPtr<ID3D11SamplerState> sampler;

  public:
    unsigned attached = 0, detached = 0, drawn = 0;
    bool empty = false;

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != __uuidof(ISubRender11Callback))
            return E_NOINTERFACE;
        *out = static_cast<ISubRender11Callback*>(this);
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++refs;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }

    STDMETHODIMP SetDevice11(ID3D11Device* next) override
    {
        vertices.Reset();
        bitmap.Reset();
        sampler.Reset();
        device = next;
        if (!next)
        {
            detached++;
            return S_OK;
        }
        attached++;
        const float quad[] = {-1, 1, 0, 0, 0, 1, 1, 0, 1, 0, -1, -1, 0, 0, 1, 1, -1, 0, 1, 1};
        D3D11_BUFFER_DESC buffer = {};
        buffer.ByteWidth = sizeof(quad);
        buffer.Usage = D3D11_USAGE_IMMUTABLE;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = quad;
        HRESULT hr = device->CreateBuffer(&buffer, &initial, &vertices);
        if (FAILED(hr))
            return hr;
        const BYTE green[] = {0, 255, 0, 0};
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        initial.pSysMem = green;
        initial.SysMemPitch = 4;
        ComPtr<ID3D11Texture2D> texture;
        hr = device->CreateTexture2D(&desc, &initial, &texture);
        if (FAILED(hr))
            return hr;
        hr = device->CreateShaderResourceView(texture.Get(), nullptr, &bitmap);
        if (FAILED(hr))
            return hr;
        D3D11_SAMPLER_DESC sample = {};
        sample.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.MaxLOD = D3D11_FLOAT32_MAX;
        return device->CreateSamplerState(&sample, &sampler);
    }

    STDMETHODIMP Render11(REFERENCE_TIME, REFERENCE_TIME, REFERENCE_TIME, RECT, RECT, RECT, double, int, DWORD) override
    {
        if (!device)
            return E_UNEXPECTED;
        if (empty)
            return E_FAIL; // MPC-BE returns this when no subtitle is active.
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        ComPtr<ID3D11RenderTargetView> view;
        context->OMGetRenderTargets(1, &view, nullptr);
        if (!view)
            return E_FAIL;
        // Like the host callback: provide bitmap/vertices, use renderer shaders.
        auto resource = bitmap.Get();
        auto sampling = sampler.Get();
        auto buffer = vertices.Get();
        UINT stride = 5 * sizeof(float), offset = 0;
        context->PSSetShaderResources(0, 1, &resource);
        context->PSSetSamplers(0, 1, &sampling);
        context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->Draw(4, 0);
        drawn++;
        return S_OK;
    }
};

class PatternStream final : public CSourceStream
{
    unsigned index = 0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HANDLE mutex = nullptr;

  public:
    PatternStream(CSource* owner, HRESULT* hr) : CSourceStream(NAME("P010 pattern"), hr, owner, L"Output") {}

    ~PatternStream()
    {
        if (mutex)
            CloseHandle(mutex);
    }

    HRESULT CompleteConnect(IPin* pin) override
    {
        HRESULT hr = CSourceStream::CompleteConnect(pin);
        if (FAILED(hr) || !hardware)
            return hr;
        ComPtr<ID3D11DecoderConfiguration> config;
        hr = pin->QueryInterface(IID_PPV_ARGS(&config));
        if (FAILED(hr))
            return hr;
        hr = D3D11CreateDevice(nullptr,
                               D3D_DRIVER_TYPE_HARDWARE,
                               nullptr,
                               D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                               nullptr,
                               0,
                               D3D11_SDK_VERSION,
                               &device,
                               nullptr,
                               &context);
        if (FAILED(hr))
            return hr;
        if (mutex)
            CloseHandle(mutex);
        mutex = CreateMutexW(nullptr, FALSE, nullptr);
        if (!mutex)
            return HRESULT_FROM_WIN32(GetLastError());
        hr = config->ActivateD3D11Decoding(device.Get(), context.Get(), mutex, 0);
        if (FAILED(hr))
            return hr;
        ComPtr<IMemInputPin> input;
        hr = pin->QueryInterface(IID_PPV_ARGS(&input));
        if (FAILED(hr))
            return hr;
        ComPtr<IMemAllocator> proposed;
        if (input->GetAllocator(&proposed) != E_NOTIMPL || proposed)
        {
            printf("FAIL renderer offered CPU allocator after native decoder activation\n");
            return E_FAIL;
        }
        printf("PASS native decoder owns allocator after device activation\n");
        ALLOCATOR_PROPERTIES nativeProperties = {};
        Check(input->GetAllocatorRequirements(&nativeProperties), "native allocator requirements");
        // Match MPC-BE's native DecideBufferSize: it supplies only the surface
        // count and retains the renderer's byte size and alignment requirements.
        nativeProperties.cBuffers = 22;
        ComPtr<IMemAllocator> nativeAllocator;
        Check(CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nativeAllocator)),
              "native allocator base-class requirements probe");
        ALLOCATOR_PROPERTIES nativeActual = {};
        Check(nativeAllocator->SetProperties(&nativeProperties, &nativeActual), "native allocator set properties");
        Check(nativeAllocator->Commit(), "native allocator commit with MPC-BE negotiation");
        Check(nativeAllocator->Decommit(), "native allocator decommit");
        puts("PASS MPC-BE native buffer negotiation commits without VFW_E_SIZENOTSET");
        return S_OK;
    }

    HRESULT GetDeliveryBuffer(IMediaSample** out, REFERENCE_TIME* start, REFERENCE_TIME* stop, DWORD flags) override
    {
        ComPtr<IMediaSample> base;
        HRESULT hr = CSourceStream::GetDeliveryBuffer(&base, start, stop, flags);
        if (FAILED(hr))
            return hr;
        if (!hardware)
        {
            *out = base.Detach();
            return S_OK;
        }
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = 64;
        desc.MipLevels = 1;
        desc.ArraySize = 2;
        desc.Format = textureFormat;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = textureBind;
        ComPtr<ID3D11Texture2D> texture;
        hr = device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(hr))
        {
            if (sourceFailure.exchange(hr) == S_OK)
                printf("SOURCE CreateTexture2D DXGI format %u, bind %u, HRESULT %08lX\n",
                       static_cast<unsigned>(desc.Format),
                       textureBind,
                       static_cast<unsigned long>(hr));
            return hr;
        }
        *out = new TextureSample(base.Get(), texture.Get());
        return S_OK;
    }

    HRESULT GetMediaType(CMediaType* mt) override
    {
        mt->InitMediaType();
        mt->SetType(&MEDIATYPE_Video);
        GUID p010 = {FourCC(), 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
        mt->SetSubtype(&p010);
        mt->SetFormatType(&FORMAT_VideoInfo2);
        auto info = reinterpret_cast<VIDEOINFOHEADER2*>(mt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER2)));
        if (!info)
            return E_OUTOFMEMORY;
        *info = {};
        info->AvgTimePerFrame = 333333;
        info->dwInterlaceFlags =
            interlacedTest ? (AMINTERLACE_IsInterlaced | (bottomFirst ? 0 : AMINTERLACE_Field1First)) : 0;
        info->dwPictAspectRatioX = 1;
        info->dwPictAspectRatioY = 1;
        info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->bmiHeader.biWidth = info->bmiHeader.biHeight = 64;
        info->bmiHeader.biPlanes = 1;
        info->bmiHeader.biBitCount = textureFormat == DXGI_FORMAT_NV12 ? 12 : 24;
        info->bmiHeader.biCompression = p010.Data1;
        info->bmiHeader.biSizeImage = SampleBytes();
        mt->SetSampleSize(SampleBytes());
        return S_OK;
    }

    HRESULT DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* props) override
    {
        props->cBuffers = 2;
        props->cbBuffer = SampleBytes();
        props->cbAlign = 1;
        ALLOCATOR_PROPERTIES actual = {};
        return allocator->SetProperties(props, &actual);
    }

    HRESULT OnThreadCreate() override
    {
        index = 0;
        return S_OK;
    }

    HRESULT FillBuffer(IMediaSample* sample) override
    {
        if (index >= (eosTest ? 6u : 90u))
            return S_FALSE;
        BYTE* bytes = nullptr;
        HRESULT hr = sample->GetPointer(&bytes);
        if (FAILED(hr))
            return hr;
        ComPtr<IMediaSample2> propsSample;
        if (SUCCEEDED(sample->QueryInterface(IID_PPV_ARGS(&propsSample))))
        {
            AM_SAMPLE2_PROPERTIES props = {};
            Check(propsSample->GetProperties(sizeof(props), reinterpret_cast<BYTE*>(&props)), "sample properties");
            props.dwTypeSpecificFlags = weaveTest ? AM_VIDEO_FLAG_WEAVE : bottomFirst ? 0 : AM_VIDEO_FLAG_FIELD1FIRST;
            Check(propsSample->SetProperties(sizeof(props), reinterpret_cast<const BYTE*>(&props)),
                  "sample field flags");
        }
        ComPtr<IMediaSideData> side;
        hr = sample->QueryInterface(IID_PPV_ARGS(&side));
        if (FAILED(hr))
            return hr;
        const BYTE* old = nullptr;
        size_t oldBytes = 0;
        if (SUCCEEDED(side->GetSideData(IID_MediaSideDataHDR, &old, &oldBytes)))
        {
            printf("FAIL reused allocator sample retained old HDR metadata\n");
            return E_FAIL;
        }
        MediaSideDataHDR hdr = {};
        hdr.display_primaries_x[0] = .265;
        hdr.display_primaries_y[0] = .69;
        hdr.display_primaries_x[1] = .15;
        hdr.display_primaries_y[1] = .06;
        hdr.display_primaries_x[2] = .68;
        hdr.display_primaries_y[2] = .32;
        hdr.white_point_x = .3127;
        hdr.white_point_y = .329;
        hdr.max_display_mastering_luminance = 1000;
        hdr.min_display_mastering_luminance = .0001;
        hr = side->SetSideData(IID_MediaSideDataHDR, reinterpret_cast<const BYTE*>(&hdr), sizeof(hdr));
        if (FAILED(hr))
            return hr;
        MediaSideDataHDRContentLightLevel light = {1200, 400};
        hr = side->SetSideData(
            IID_MediaSideDataHDRContentLightLevel, reinterpret_cast<const BYTE*>(&light), sizeof(light));
        if (FAILED(hr))
            return hr;
        if (textureFormat == DXGI_FORMAT_NV12)
        {
            std::fill_n(bytes, 64 * 64, static_cast<BYTE>(90));
            for (int i = 64 * 64; i < 64 * 64 * 3 / 2; i += 2)
            {
                bytes[i] = 48;
                bytes[i + 1] = 210;
            }
        }
        else
        {
            const unsigned factor = textureFormat == DXGI_FORMAT_P016 ? 257 : 256;
            auto data = reinterpret_cast<uint16_t*>(bytes);
            std::fill_n(data, 64 * 64, static_cast<uint16_t>(90 * factor));
            for (int i = 64 * 64; i < 64 * 64 * 3 / 2; i += 2)
            {
                data[i] = static_cast<uint16_t>(48 * factor);
                data[i + 1] = static_cast<uint16_t>(210 * factor);
            }
        }
        if (hardware)
        {
            ComPtr<IMediaSampleD3D11> gpu;
            hr = sample->QueryInterface(IID_PPV_ARGS(&gpu));
            if (FAILED(hr))
                return hr;
            ComPtr<ID3D11Texture2D> tex;
            UINT slice = 0;
            hr = gpu->GetD3D11Texture(0, &tex, &slice);
            if (FAILED(hr))
                return hr;
            if (WaitForSingleObject(mutex, 1000) != WAIT_OBJECT_0)
                return E_FAIL;
            context->UpdateSubresource(
                tex.Get(), slice, nullptr, bytes, textureFormat == DXGI_FORMAT_NV12 ? 64 : 128, 0);
            context->Flush();
            ReleaseMutex(mutex);
        }
        REFERENCE_TIME start = index * 333333LL, stop = start + 333333;
        sample->SetTime(&start, &stop);
        sample->SetSyncPoint(TRUE);
        sample->SetActualDataLength(SampleBytes());
        index++;
        return S_OK;
    }
};

class PatternSource final : public CSource
{
  public:
    PatternSource(HRESULT* hr) : CSource(NAME("P010 test source"), nullptr, CLSID_NULL)
    {
        new PatternStream(this, hr);
    }
};

static int Spread(const std::vector<BYTE>& pixels)
{
    long sum = 0;
    for (size_t i = 0; i < pixels.size(); i += 4)
        sum +=
            std::max({pixels[i], pixels[i + 1], pixels[i + 2]}) - std::min({pixels[i], pixels[i + 1], pixels[i + 2]});
    return static_cast<int>(sum / (pixels.size() / 4));
}

using FactoryGetter = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);

struct VideoCreation
{
    HWND parent;
    DWORD style;
};

static thread_local std::vector<VideoCreation> videoCreations;
static thread_local std::vector<VideoCreation> videoDestructions;

static constexpr UINT QueryOwnerCursor = WM_APP + 0x550;
static constexpr UINT HideOwnerCursor = WM_APP + 0x551;

static LRESULT CALLBACK VideoOwnerTestProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    if (message == QueryOwnerCursor)
        return reinterpret_cast<LRESULT>(GetCursor());
    if (message == HideOwnerCursor)
        return reinterpret_cast<LRESULT>(SetCursor(nullptr));
    return DefWindowProcW(hwnd, message, w, l);
}

static LRESULT CALLBACK ObserveVideoCreation(int code, WPARAM w, LPARAM l)
{
    if (code == HCBT_CREATEWND)
    {
        const auto create = reinterpret_cast<const CBT_CREATEWNDW*>(l)->lpcs;
        if (!IS_INTRESOURCE(create->lpszClass) && !wcscmp(create->lpszClass, L"PlaceboRendererVideoWindow"))
            videoCreations.push_back({create->hwndParent, static_cast<DWORD>(create->style)});
    }
    if (code == HCBT_DESTROYWND)
    {
        wchar_t name[64] = {};
        const HWND video = reinterpret_cast<HWND>(w);
        GetClassNameW(video, name, 64);
        if (!wcscmp(name, L"PlaceboRendererVideoWindow"))
            videoDestructions.push_back({GetParent(video), static_cast<DWORD>(GetWindowLongPtrW(video, GWL_STYLE))});
    }
    return CallNextHookEx(nullptr, code, w, l);
}

static int TestVideoWindowFocus(FactoryGetter getter)
{
    WNDCLASSW cls = {};
    cls.lpfnWndProc = VideoOwnerTestProcedure;
    cls.hInstance = g_hInst;
    cls.lpszClassName = L"VideoOwnerFocusTest";
    if (!RegisterClassW(&cls))
        return 263;
    HWND host = CreateWindowW(cls.lpszClassName,
                              L"Player focus test",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              0,
                              0,
                              160,
                              120,
                              nullptr,
                              nullptr,
                              g_hInst,
                              nullptr);
    HWND other = CreateWindowW(cls.lpszClassName,
                               L"Other application",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               180,
                               0,
                               160,
                               120,
                               nullptr,
                               nullptr,
                               g_hInst,
                               nullptr);
    if (!host || !other)
        return 260;
    const DWORD playerThread = GetCurrentThreadId();
    std::atomic<bool> passed = true;
    for (int episode = 0; episode < 4; ++episode)
    {
        const HWND expected = episode < 2 ? host : other;
        SetActiveWindow(expected);
        SetFocus(expected);
        std::atomic<bool> done = false, closeHost = false, hostClosed = false;
        std::thread graphThread(
            [&]()
            {
                Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "focus worker COM");
                videoCreations.clear();
                videoDestructions.clear();
                // Joining input queues through a cross-thread video child must
                // not restore this old wait cursor when the child is removed.
                SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                SendMessageW(host, HideOwnerCursor, 0, 0);
                const HHOOK creationHook =
                    SetWindowsHookExW(WH_CBT, ObserveVideoCreation, nullptr, GetCurrentThreadId());
                if (!creationHook)
                    exit(262);
                {
                    ComPtr<IClassFactory> factory;
                    Check(getter(CLSID_PlaceboRenderer, IID_PPV_ARGS(&factory)), "focus renderer factory");
                    ComPtr<IBaseFilter> renderer;
                    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&renderer)), "focus renderer");
                    ComPtr<IVideoWindow> window;
                    Check(renderer.As(&window), "focus video window");
                    const bool pendingOwner = episode % 2 == 0;
                    if (pendingOwner)
                        Check(window->put_Owner(reinterpret_cast<OAHWND>(host)), "focus pending owner");
                    ComPtr<IGraphBuilder> graph;
                    Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)),
                          "focus graph");
                    Check(graph->AddFilter(renderer.Get(), L"renderer"), "focus add renderer");
                    HRESULT hr = S_OK;
                    auto rawSource = new PatternSource(&hr);
                    Check(hr, "focus source");
                    ComPtr<IBaseFilter> source;
                    Check(rawSource->NonDelegatingQueryInterface(IID_PPV_ARGS(&source)), "focus source interface");
                    Check(graph->AddFilter(source.Get(), L"source"), "focus add source");
                    ComPtr<IEnumPins> pins;
                    ComPtr<IPin> input;
                    Check(renderer->EnumPins(&pins), "focus pins");
                    Check(pins->Next(1, &input, nullptr), "focus input");
                    Check(graph->ConnectDirect(rawSource->GetPin(0), input.Get(), nullptr), "focus connect");
                    if (!pendingOwner)
                    {
                        if (!videoCreations.empty())
                            passed = false;
                        Check(window->put_Owner(reinterpret_cast<OAHWND>(host)), "focus owner after connection");
                    }
                    if (videoCreations.size() != 1 || videoCreations.back().parent != host ||
                        !(videoCreations.back().style & WS_CHILD) || (videoCreations.back().style & WS_POPUP))
                        passed = false;
                    const auto verify = [&](const char* stage)
                    {
                        GUITHREADINFO gui = {sizeof(gui)};
                        if (!GetGUIThreadInfo(playerThread, &gui) || gui.hwndActive != expected ||
                            gui.hwndFocus != expected)
                        {
                            printf("FAIL episode %d %s: active=%p focus=%p expected=%p\n",
                                   episode,
                                   stage,
                                   gui.hwndActive,
                                   gui.hwndFocus,
                                   expected);
                            passed = false;
                        }
                    };
                    verify("embedded");
                    Check(window->put_Owner(reinterpret_cast<OAHWND>(host)), "focus repeated owner");
                    verify("repeated owner");
                    Check(window->put_Visible(OATRUE), "focus show video");
                    verify("shown");
                    const HWND originalVideo = FindWindowExW(host, nullptr, L"PlaceboRendererVideoWindow", nullptr);
                    Check(window->put_Owner(reinterpret_cast<OAHWND>(other)), "focus fullscreen parent");
                    verify("new parent");
                    if (!originalVideo || GetParent(originalVideo) != other || videoCreations.size() != 1)
                        passed = false;
                    Check(window->put_Owner(reinterpret_cast<OAHWND>(host)), "focus restore parent");
                    verify("restored parent");
                    if (GetParent(originalVideo) != host || videoCreations.size() != 1)
                        passed = false;
                    Check(window->put_Visible(OAFALSE), "focus hide video");
                    verify("hidden");
                    SendMessageW(host, HideOwnerCursor, 0, 0);
                    Check(window->put_Owner(0), "focus detach video");
                    verify("detached");
                    if (SendMessageW(host, QueryOwnerCursor, 0, 0))
                    {
                        printf("FAIL episode %d: stale cursor restored after video detach\n", episode);
                        passed = false;
                    }
                    if (videoDestructions.size() != 1 ||
                        (videoDestructions.back().parent && videoDestructions.back().parent != GetDesktopWindow()) ||
                        (videoDestructions.back().style & (WS_POPUP | WS_VISIBLE)))
                    {
                        printf("FAIL episode %d: video not detached while hidden before destruction\n", episode);
                        passed = false;
                    }
                    if (FindWindowExW(host, nullptr, L"PlaceboRendererVideoWindow", nullptr))
                        passed = false;
                    Check(window->put_Owner(reinterpret_cast<OAHWND>(host)), "focus recreate child");
                    verify("recreated");
                    if (videoCreations.size() != 2 || videoCreations.back().parent != host ||
                        !(videoCreations.back().style & WS_CHILD) || (videoCreations.back().style & WS_POPUP))
                        passed = false;
                    SendMessageW(host, HideOwnerCursor, 0, 0);
                    if (episode == 3)
                    {
                        closeHost = true;
                        while (!hostClosed)
                        {
                            MSG message;
                            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                            {
                                TranslateMessage(&message);
                                DispatchMessageW(&message);
                            }
                            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
                        }
                        Check(window->put_Owner(reinterpret_cast<OAHWND>(other)),
                              "new owner after old parent destroyed");
                        verify("old parent destroyed");
                        if (videoCreations.size() != 3 || videoCreations.back().parent != other ||
                            !(videoCreations.back().style & WS_CHILD) || (videoCreations.back().style & WS_POPUP))
                            passed = false;
                    }
                }
                if (episode != 3 && SendMessageW(host, QueryOwnerCursor, 0, 0))
                {
                    printf("FAIL episode %d: stale cursor restored after renderer release\n", episode);
                    passed = false;
                }
                UnhookWindowsHookEx(creationHook);
                CoUninitialize();
                done = true;
            });
        while (!done)
        {
            if (closeHost && !hostClosed)
            {
                DestroyWindow(host);
                hostClosed = true;
            }
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        }
        graphThread.join();
        if (GetActiveWindow() != expected || GetFocus() != expected)
            passed = false;
    }
    DestroyWindow(other);
    DestroyWindow(host);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    if (!passed)
        return 261;
    puts(
        "PASS direct child creation, hidden detach before destruction, cursor and focus preserved through early/late/repeated owner, parent switch, show/hide/detach/recreate and parent-first destruction");
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    for (int i = 1; i < argc; i++)
        if (wcscmp(argv[i], L"--mpcbe-owner") == 0)
            mpcbeOwnerTest = true;
    for (int i = 1; i < argc; i++)
        if (wcscmp(argv[i], L"--startup") == 0)
            startupTest = true;
    for (int i = 1; i < argc; i++)
        if (wcscmp(argv[i], L"--measure") == 0)
            measureTest = true;
    hardware = argc > 1 && wcscmp(argv[1], L"--gpu") == 0;
    for (int i = 1; i < argc; i++)
    {
        if (wcscmp(argv[i], L"--nv12") == 0)
            textureFormat = DXGI_FORMAT_NV12;
        if (wcscmp(argv[i], L"--p016") == 0)
            textureFormat = DXGI_FORMAT_P016;
    }
    for (int i = 1; i < argc; i++)
        if (wcscmp(argv[i], L"--srv") == 0)
            textureBind = D3D11_BIND_SHADER_RESOURCE;
    for (int i = 1; i < argc; i++)
    {
        if (wcscmp(argv[i], L"--flush-race") == 0)
            flushRaceTest = true;
        if (wcscmp(argv[i], L"--sharpen") == 0)
            sharpenTest = true;
        if (wcscmp(argv[i], L"--motion") == 0)
            motionTest = true;
        if (wcscmp(argv[i], L"--rtx") == 0)
            rtxTest = true;
        if (wcscmp(argv[i], L"--interlaced") == 0)
            interlacedTest = true;
        if (wcscmp(argv[i], L"--bff") == 0)
            bottomFirst = true;
        if (wcscmp(argv[i], L"--weave") == 0)
            weaveTest = true;
        if (wcscmp(argv[i], L"--eos") == 0)
            eosTest = true;
        if (wcscmp(argv[i], L"--yadif") == 0)
            yadifTest = true;
        if (wcscmp(argv[i], L"--force-bff") == 0)
            forceBffTest = true;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    g_hInst = GetModuleHandleW(nullptr);
    Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM");
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    std::wstring directory = executable;
    directory.resize(directory.find_last_of(L"\\") + 1);
    std::wstring dllPath = directory + L"PlaceboRenderer.ax";
    for (int i = 1; i + 1 < argc; i++)
        if (wcscmp(argv[i], L"--dll") == 0)
            dllPath = argv[++i];
    HMODULE dll =
        LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll)
    {
        printf("FAIL load %lu\n", GetLastError());
        return 1;
    }
    using Getter = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto getter = reinterpret_cast<Getter>(GetProcAddress(dll, "DllGetClassObject"));
    for (int i = 1; i < argc; ++i)
        if (wcscmp(argv[i], L"--focus") == 0)
            return TestVideoWindowFocus(getter);
    ComPtr<IClassFactory> factory;
    Check(getter(CLSID_PlaceboRenderer, IID_PPV_ARGS(&factory)), "factory");
    {
        HWND keyWindow = CreateWindowExW(
            0, L"STATIC", L"Internal shortcut test", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, g_hInst, nullptr);
        StatisticsKey::Attach(keyWindow);
        StatisticsKey::Attach(keyWindow);
        if (StatisticsKey::windows.size() != 1 || StatisticsKey::hooks.size() != 1)
            exit(99);
        MSG key = {};
        key.message = WM_KEYDOWN;
        key.wParam = 'J';
        if (StatisticsKey::Handle(key, false, false, false, keyWindow) ||
            StatisticsKey::Handle(key, true, false, false, nullptr))
            exit(100);
        if (!StatisticsKey::Handle(key, true, false, false, keyWindow) || key.message != WM_NULL)
            exit(101);
        MSG posted = {};
        if (!PeekMessageW(&posted, keyWindow, StatisticsKey::ToggleMessage, StatisticsKey::ToggleMessage, PM_REMOVE))
            exit(102);
        key.message = WM_KEYDOWN;
        key.wParam = 'J';
        key.lParam = 1LL << 30;
        if (!StatisticsKey::Handle(key, true, false, false, keyWindow) ||
            PeekMessageW(&posted, keyWindow, StatisticsKey::ToggleMessage, StatisticsKey::ToggleMessage, PM_REMOVE))
            exit(103);
        key.message = WM_SYSKEYDOWN;
        key.wParam = 'A';
        key.lParam = 0;
        if (StatisticsKey::Handle(key, true, true, false, keyWindow) || key.message != WM_SYSKEYDOWN)
            exit(264);
        StatisticsKey::Detach(keyWindow);
        if (!StatisticsKey::windows.empty() || !StatisticsKey::hooks.empty())
            exit(104);
        DestroyWindow(keyWindow);
        puts("PASS Ctrl+J routing, focus restriction, repeat suppression and hook cleanup");
    }

    struct OwnerWindow
    {
        HWND handle = nullptr;

        ~OwnerWindow()
        {
            if (handle)
                DestroyWindow(handle);
        }
    } ownerWindow;

    ComPtr<TestPresenter> presenter;
    ComPtr<IBaseFilter> renderer;
    if (mpcbeOwnerTest)
    {
        presenter.Attach(new TestPresenter());
        Check(presenter->Initialize(factory.Get()), "MPC-BE aggregated renderer");
        Check(presenter.As(&renderer), "MPC-BE presenter filter interface");
    }
    else
        Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&renderer)), "renderer");
    ComPtr<IPlaceboSettings> config;
    Check(renderer.As(&config), "settings interface");
    PlaceboSettings settings;
    Check(config->GetSettings(&settings), "read settings");
    if (GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0))
    {
        auto inactive = settings;
        const auto id = DisplayProfiles::Hash(L"filter:test:inactive");
        wcscpy_s(inactive.displayProfile, id.c_str());
        inactive.brightness = .17f;
        inactive.smoothMotion = !settings.smoothMotion;
        Check(config->SetSettings(&inactive), "edit inactive display profile");
        PlaceboSettings unchanged;
        Check(config->GetSettings(&unchanged), "active display after inactive edit");
        float savedBrightness = 0;
        DWORD bytes = sizeof(savedBrightness);
        const auto read = RegGetValueW(HKEY_CURRENT_USER,
                                       DisplayProfiles::Key(id).c_str(),
                                       L"Brightness",
                                       RRF_RT_REG_BINARY,
                                       nullptr,
                                       &savedBrightness,
                                       &bytes);
        if (unchanged.brightness != settings.brightness || unchanged.smoothMotion != settings.smoothMotion ||
            wcscmp(unchanged.displayProfile, settings.displayProfile) || read != ERROR_SUCCESS ||
            savedBrightness != .17f)
            return 245;
        puts("PASS editing an inactive display persists all settings without changing active playback");
    }
    if (const char* mixer = getenv("PLACEBO_TEST_MIXER"))
        strcpy_s(settings.scaling.filters[4].filter, mixer);
    if (getenv("PLACEBO_TEST_DEBAND"))
    {
        settings.deband = TRUE;
        settings.debandGrain = 0;
    }
    settings.adaptiveSharpen = sharpenTest;
    settings.smoothMotion = motionTest;
    settings.rtxSuperResolution = rtxTest;
    settings.autoHdr = FALSE;
    settings.deinterlacing = yadifTest ? 2 : 1;
    settings.fieldOverride = forceBffTest ? 2 : 0;
    settings.saturation = 1.f;
    Check(config->SetSettings(&settings), "set color without display switching");
    ComPtr<ID3D11DecoderConfiguration> filterDecoder;
    Check(renderer.As(&filterDecoder), "decoder configuration on filter");
    ComPtr<ID3D11DecoderTextureConfiguration> filterTextures;
    Check(renderer.As(&filterTextures), "decoder texture configuration on filter");
    ComPtr<IExFilterConfig> hostConfig;
    Check(renderer.As(&hostConfig), "public host configuration");
    int renderType = 0;
    Check(hostConfig->Flt_GetInt("renderType", &renderType), "D3D11 render type");
    if (renderType != 11)
        return 13;
    if (hostConfig->Flt_GetInt("unknown", &renderType) != E_INVALIDARG)
        return 14;
    ComPtr<IQualProp> startupQuality;
    if (startupTest)
    {
        Check(renderer->QueryInterface(IID_IQualProp, reinterpret_cast<void**>(startupQuality.GetAddressOf())),
              "DirectShow raw quality counter");
        int raw = 0;
        __int64 sum = 0;
        Check(startupQuality->get_FramesDroppedInRenderer(&raw), "initial raw drops");
        Check(hostConfig->Flt_GetInt64("placeboStatsDrops", &sum), "initial accumulated drops");
        printf("STARTUP fresh renderer raw=%d accumulated=%lld\n", raw, sum);
        if (raw || sum)
            exit(108);
    }
    ComPtr<IClassFactory> pageFactory;
    Check(getter(CLSID_FrankyDirect2DPage, IID_PPV_ARGS(&pageFactory)), "settings factory");
    {
        IID oldId = {0x9a1392c7, 0xa9ea, 0x4f3c, {0x8e, 0x24, 0x77, 0x33, 0x3c, 0x9a, 0x81, 0xbf}};
        void* old = nullptr;
        if (renderer->QueryInterface(oldId, &old) != E_NOINTERFACE || old)
            exit(65);
    }
    {
        IID oldId = {0x37628880, 0x632f, 0x46e9, {0x9a, 0xa9, 0xf7, 0x8d, 0xd0, 0x44, 0x21, 0xe9}};
        void* old = nullptr;
        if (renderer->QueryInterface(oldId, &old) != E_NOINTERFACE || old)
            exit(95);
    }
    {
        IID previousId = {0xfae47621, 0xc763, 0x4fae, {0x99, 0xa0, 0xc8, 0x45, 0x9f, 0x6b, 0x51, 0xe8}};
        void* previous = nullptr;
        if (renderer->QueryInterface(previousId, &previous) != E_NOINTERFACE || previous)
            exit(136);
    }
    {
        IID previousId = {0x772c1bf7, 0xe7ea, 0x498c, {0x84, 0xe7, 0xb7, 0xa4, 0x75, 0x3c, 0x76, 0xe9}};
        void* previous = nullptr;
        if (renderer->QueryInterface(previousId, &previous) != E_NOINTERFACE || previous)
            exit(133);
    }
    {
        IID previousId = {0xb7214b57, 0x2da7, 0x444a, {0x98, 0x05, 0xa8, 0xcb, 0x80, 0x2d, 0xf5, 0x1e}};
        void* previous = nullptr;
        if (renderer->QueryInterface(previousId, &previous) != E_NOINTERFACE || previous)
            exit(125);
    }
    {
        IID oldId = {0x082bccb7, 0x181b, 0x4d07, {0x9c, 0x18, 0x50, 0x31, 0x47, 0xc4, 0x59, 0x21}};
        void* old = nullptr;
        if (renderer->QueryInterface(oldId, &old) != E_NOINTERFACE || old)
            exit(112);
    }
    {
        IID oldId = {0x1726072a, 0xdf7c, 0x4be9, {0xb2, 0xd7, 0xe4, 0x49, 0xfb, 0xb0, 0x42, 0xab}};
        void* old = nullptr;
        if (renderer->QueryInterface(oldId, &old) != E_NOINTERFACE || old)
            exit(170);
    }
    ComPtr<ISpecifyPropertyPages> listedPages;
    Check(renderer.As(&listedPages), "listed property pages");
    CAUUID listed = {};
    Check(listedPages->GetPages(&listed), "page list");
    if (listed.cElems != 1 || listed.pElems[0] != CLSID_FrankyDirect2DPage)
        exit(55);
    CoTaskMemFree(listed.pElems);
    for (const auto& id : LegacyPropertyPages::Ids)
    {
        ComPtr<IClassFactory> obsolete;
        if (getter(id, IID_PPV_ARGS(&obsolete)) != CLASS_E_CLASSNOTAVAILABLE || obsolete)
            exit(150);
    }
    puts("PASS only current settings page is exposed; retired dialog factories are unavailable");
    ComPtr<IVideoWindow> window;
    Check(renderer.As(&window), "video window");
    {
        ownerWindow.handle = CreateWindowW(L"STATIC",
                                           L"MPC-BE owner test",
                                           WS_POPUP,
                                           0,
                                           0,
                                           128,
                                           128,
                                           nullptr,
                                           nullptr,
                                           GetModuleHandleW(nullptr),
                                           nullptr);
        if (!ownerWindow.handle)
            exit(252);
        // MPC-BE provides its parent before connection; the ordinary host
        // path below deliberately supplies it afterwards.
        if (mpcbeOwnerTest)
            Check(window->put_Owner(reinterpret_cast<OAHWND>(ownerWindow.handle)), "MPC-BE owner before connection");
    }
    ComPtr<IGraphBuilder> graph;
    Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), "graph");
    Check(graph->AddFilter(renderer.Get(), L"libplacebo"), "add renderer");
    HRESULT hr = S_OK;
    auto rawSource = new PatternSource(&hr);
    Check(hr, "source create");
    ComPtr<IBaseFilter> source;
    Check(rawSource->NonDelegatingQueryInterface(IID_PPV_ARGS(&source)), "source interface");
    Check(graph->AddFilter(source.Get(), L"P010"), "add source");
    ComPtr<IEnumPins> list;
    ComPtr<IPin> input;
    Check(renderer->EnumPins(&list), "input list");
    Check(list->Next(1, &input, nullptr), "input");
    Check(graph->ConnectDirect(rawSource->GetPin(0), input.Get(), nullptr), "connect P010");
    if (!mpcbeOwnerTest)
        Check(window->put_Owner(reinterpret_cast<OAHWND>(ownerWindow.handle)), "host owner after connection");
    if (mpcbeOwnerTest)
    {
        ComPtr<IBasicVideo2> videoInfo;
        Check(renderer.As(&videoInfo), "MPC-BE IBasicVideo2 interface");
        long aspectX = 0, aspectY = 0;
        Check(videoInfo->GetPreferredAspectRatio(&aspectX, &aspectY), "MPC-BE video aspect ratio");
        if (aspectX <= 0 || aspectY <= 0 || aspectX != aspectY)
            exit(249); // The pattern source uses square 64x64 video.
        OAHWND owner = 0;
        Check(window->get_Owner(&owner), "MPC-BE retained owner");
        const HWND video = FindWindowExW(ownerWindow.handle, nullptr, L"PlaceboRendererVideoWindow", nullptr);
        if (owner != reinterpret_cast<OAHWND>(ownerWindow.handle) || !video ||
            (GetWindowLongPtrW(video, GWL_STYLE) & WS_VISIBLE))
            exit(253);
        for (const SIZE size : {SIZE{320, 240}, SIZE{960, 540}, SIZE{128, 128}})
        {
            Check(window->SetWindowPosition(0, 0, size.cx, size.cy), "MPC-BE video window resize");
            RECT client = {};
            if (!GetClientRect(video, &client) || client.right != size.cx || client.bottom != size.cy)
                exit(250);
        }
        puts("PASS MPC-BE video identification, aspect ratio and repeated window resize");
        puts("PASS MPC-BE pre-connection owner retained; video embedded without showing a popup");
    }
    Check(window->put_AutoShow(OAFALSE), "disable autoshow");
    Check(window->put_Visible(OAFALSE), "hide test window");
    Check(window->SetWindowPosition(0, 0, 128, 128), "video size");
    if (measureTest)
    {
        Check(window->SetWindowPosition(40, 40, 640, 360), "measurement window size");
        Check(window->put_Visible(OATRUE), "show measurement test window");
    }
    ComPtr<IMediaControl> control;
    Check(graph.As(&control), "control");
    if (eosTest)
    {
        ComPtr<IMediaFilter> media;
        Check(graph.As(&media), "clock control");
        Check(media->SetSyncSource(nullptr), "unclocked exact-count test");
    }
    Check(control->Run(), "run");
    PlaceboStatus status;
    const DWORD start = GetTickCount();
    do
    {
        Sleep(20);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Check(config->GetStatus(&status), "status");
    } while (status.rendered < 5 && SUCCEEDED(sourceFailure.load()) && GetTickCount() - start < 10000);
    if (rtxTest)
    {
        __int64 processed = 0;
        Check(hostConfig->Flt_GetInt64("placeboStatsRtxFrames", &processed), "RTX processing counter");
        if (processed <= 0)
        {
            printf("FAIL RTX driver processing counter=%lld\n", processed);
            ExitProcess(1);
        }
        printf("PASS RTX DirectShow driver path: submitted=%lld\n", processed);
    }
    if (startupTest)
    {
        __int64 cached = 0, objects = 0;
        Check(hostConfig->Flt_GetInt64("placeboShaderCacheLoaded", &cached), "cache loaded");
        Check(hostConfig->Flt_GetInt64("placeboShaderCacheObjects", &objects), "cache objects");
        printf("STARTUP shader cache loaded=%lld objects=%lld\n", cached, objects);
        int raw = 0;
        __int64 sum = 0, confirmed = 0;
        Check(startupQuality->get_FramesDroppedInRenderer(&raw), "startup raw drops");
        Check(hostConfig->Flt_GetInt64("placeboStatsDrops", &sum), "startup accumulated drops");
        Check(hostConfig->Flt_GetInt64("placeboStatsConfirmed", &confirmed), "startup display feedback");
        printf(
            "STARTUP after %lu ms, video window intentionally hidden=%d: renderer outputs=%llu DXGI confirmed=%lld raw drops=%d accumulated=%lld\n",
            GetTickCount() - start,
            !measureTest,
            status.rendered,
            confirmed,
            raw,
            sum);
        // Pause freezes the scheduler, making raw and accumulated snapshots
        // comparable without racing another output on the source thread.
        Check(control->Pause(), "startup freeze");
        OAFilterState frozen;
        Check(control->GetState(3000, &frozen), "startup frozen state");
        Check(startupQuality->get_FramesDroppedInRenderer(&raw), "frozen raw drops");
        Check(hostConfig->Flt_GetInt64("placeboStatsDrops", &sum), "frozen accumulated drops");
        double cpuMs = 0, gpuMs = 0;
        Check(hostConfig->Flt_GetDouble("placeboStatsRenderCpuMs", &cpuMs), "startup CPU render time");
        auto gpuResult = hostConfig->Flt_GetDouble("placeboStatsRenderGpuMs", &gpuMs);
        printf(
            "STARTUP frozen raw=%d accumulated=%lld difference=%lld render CPU average=%.3fms GPU average=%.3fms GPU result=%08lX\n",
            raw,
            sum,
            sum - raw,
            cpuMs,
            gpuMs,
            static_cast<unsigned long>(gpuResult));
        if (sum > raw)
            exit(109);
        Check(control->Run(), "startup resume and capture final raw epoch");
        Check(hostConfig->Flt_GetInt64("placeboStatsDrops", &sum), "retained startup total");
        if (sum < raw)
            exit(110);
        printf("STARTUP prior raw=%d retained after restart=%lld\n", raw, sum);
    }
    if (status.rendered < 5)
    {
        ComPtr<IMediaEvent> events;
        if (SUCCEEDED(graph.As(&events)))
        {
            long code = 0;
            LONG_PTR a = 0, b = 0;
            while (events->GetEvent(&code, &a, &b, 0) == S_OK)
            {
                printf("Graph event %ld, parameter %08llX\n", code, static_cast<unsigned long long>(a));
                events->FreeEventParams(code, a, b);
            }
        }
        printf("FAIL no rendered frames, count=%llu fields=%llu result %08lX\n",
               status.rendered,
               status.deinterlacedFields,
               static_cast<unsigned long>(status.lastResult));
        control->Stop();
        return 2;
    }
    if (hardware && status.hardwareFrames < 5)
    {
        printf("FAIL D3D11 sample branch not used\n");
        control->Stop();
        return 8;
    }
    if (!eosTest && interlacedTest && (!weaveTest || forceBffTest) && status.deinterlacedFields < 5)
    {
        printf("FAIL timed stream bypassed deinterlacing\n");
        return 19;
    }
    if ((!interlacedTest || (weaveTest && !forceBffTest)) && status.deinterlacedFields)
    {
        printf("FAIL progressive samples deinterlaced\n");
        return 20;
    }
    if (eosTest)
    {
        ComPtr<IMediaEvent> events;
        Check(graph.As(&events), "EOS events");
        long event = 0;
        Check(events->WaitForCompletion(5000, &event), "complete deinterlaced stream");
        Check(config->GetStatus(&status), "EOS final count");
        PlaceboDiagnostics motion;
        Check(renderer.As(&filterDecoder), "renderer still valid");
        ComPtr<IPlaceboDiagnostics> motionInfo;
        Check(renderer.As(&motionInfo), "motion diagnostics");
        Check(motionInfo->GetDiagnostics(&motion), "final motion diagnostics");
        const uint64_t expected = motionTest && motion.displayHz > 0
                                      ? static_cast<uint64_t>(std::ceil(1999998. * motion.displayHz / 1e7 - 1e-5))
                                  : interlacedTest && (!weaveTest || forceBffTest) ? 12
                                                                                   : 6;
        if (status.rendered != expected ||
            status.deinterlacedFields != ((interlacedTest && (!weaveTest || forceBffTest)) ? expected : 0) ||
            (motionTest && !status.mixedFrames))
        {
            printf("FAIL final fields lost/duplicated: frames=%llu fields=%llu expected=%llu\n",
                   status.rendered,
                   status.deinterlacedFields,
                   expected);
            return 18;
        }
        if (motionTest)
            printf("PASS Smooth Motion %llu mixed outputs, display %.6f Hz, final timeline and EOS\n",
                   status.mixedFrames,
                   motion.displayHz);
        Check(control->Stop(), "EOS stop");
        printf("PASS exact EOS drain: %llu outputs, fields=%llu, %s %s\n",
               status.rendered,
               status.deinterlacedFields,
               bottomFirst ? "BFF" : "TFF",
               yadifTest ? "Yadif" : "Bwdif");
        return 0;
    }
    if (status.masteringMaxNits != 1000 || abs(status.masteringMinNits - .0001f) > .000001f || status.maxCLL != 1200 ||
        status.maxFALL != 400)
    {
        printf("FAIL allocated HDR side data did not reach renderer: %.1f %.6f %.1f %.1f\n",
               status.masteringMaxNits,
               status.masteringMinNits,
               status.maxCLL,
               status.maxFALL);
        control->Stop();
        return 15;
    }
    ComPtr<IPlaceboDiagnostics> diagnosticConfig;
    Check(renderer.As(&diagnosticConfig), "diagnostics interface");
    PlaceboDiagnostics actual;
    Check(diagnosticConfig->GetDiagnostics(&actual), "diagnostics snapshot");
    if (sharpenTest && actual.sharpenState != 1)
        exit(53);
    if (!actual.haveVideo || actual.width != 64 || actual.height != 64 || !!actual.directGpu != hardware ||
        actual.frames == 0 || actual.sourcePeak != 1000)
        exit(30);
    actual.size = 0;
    if (SUCCEEDED(diagnosticConfig->GetDiagnostics(&actual)))
        exit(31);
    printf(
        "PASS diagnostics: actual input size, GPU/CPU path, source metadata, frame count and buffer size validation\n");
    printf(
        "PASS allocator HDR metadata write/read, recycled samples cleared, mastering 1000 nits and MaxCLL/MaxFALL\n");
    if (hardware)
        printf("PASS decoder device negotiation, array texture slice 1 (bind %u), GPU sample branch (%llu frames)\n",
               textureBind,
               status.hardwareFrames);
    if (sharpenTest)
    {
        for (bool enabled : {false, true})
        {
            settings.adaptiveSharpen = enabled;
            Check(config->SetSettings(&settings), "live sharpen toggle");
            const DWORD deadline = GetTickCount();
            bool matched = false;
            do
            {
                Sleep(10);
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                actual = {};
                Check(diagnosticConfig->GetDiagnostics(&actual), "live sharpen diagnostics");
                matched = actual.sharpenState == (enabled ? 1 : 0) && actual.haveVideo;
            } while (!matched && GetTickCount() - deadline < 5000);
            if (!matched)
            {
                puts("FAIL live shader toggle stalled or not applied");
                return 54;
            }
        }
        puts("PASS live Adaptive Sharpen off/on without stopping playback");
    }
    if (motionTest)
    {
        for (bool enabled : {false, true})
        {
            settings.smoothMotion = enabled;
            Check(config->SetSettings(&settings), "live smooth motion toggle");
            const DWORD deadline = GetTickCount();
            bool matched = false;
            do
            {
                Sleep(10);
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                actual = {};
                Check(diagnosticConfig->GetDiagnostics(&actual), "live motion diagnostics");
                matched = !!actual.smoothMotion == enabled && actual.haveVideo;
            } while (!matched && GetTickCount() - deadline < 5000);
            if (!matched)
            {
                puts("FAIL live smooth motion toggle stalled or not applied");
                return 40;
            }
        }
        puts("PASS live Smooth Motion off/on without stopping playback");
    }
    if (measureTest)
    {
        const auto deadline = GetTickCount64() + 5000;
        __int64 confirmed = 0;
        do
        {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(10);
            Check(hostConfig->Flt_GetInt64("placeboStatsConfirmed", &confirmed), "confirmed presentation feedback");
        } while (confirmed < 20 && GetTickCount64() < deadline);
        __int64 glitches = 0, repeats = 0;
        double gpuMs = 0;
        Check(hostConfig->Flt_GetInt64("placeboStatsGlitches", &glitches), "clocked DXGI glitch counter");
        Check(hostConfig->Flt_GetInt64("placeboStatsRepeats", &repeats), "clocked DXGI repeat counter");
        Check(hostConfig->Flt_GetDouble("placeboStatsRenderGpuMs", &gpuMs), "asynchronous render GPU measurement");
        if (confirmed < 20 || gpuMs <= 0)
            exit(107);
        printf(
            "PASS actual DirectShow reference clock -> DXGI: %lld confirmed, repeats=%lld glitches=%lld GPU=%.3fms\n",
            confirmed,
            repeats,
            glitches,
            gpuMs);
    }
    double renderMs = 0, presentMs = 0;
    Check(hostConfig->Flt_GetDouble("placeboStatsRenderCpuMs", &renderMs), "render CPU measurement");
    Check(hostConfig->Flt_GetDouble("placeboStatsPresentCpuMs", &presentMs), "present CPU measurement");
    if (renderMs < 0 || presentMs < 0)
        exit(105);
    Check(control->Pause(), "pause");
    OAFilterState paused;
    Check(control->GetState(3000, &paused), "paused state");
    __int64 timingCount = 0;
    Check(hostConfig->Flt_GetInt64("placeboStatsTimingActive", &timingCount), "paused timing state");
    if (timingCount)
        exit(106);
    if (measureTest)
        Check(hostConfig->Flt_GetInt64("placeboStatsGlitches", &timingCount), "paused counters remain readable");
    puts("PASS pause resets correlation while retaining measured error totals");
    Check(renderer.As(&hostConfig), "host statistics interface");
    bool statsEnabled = true;
    Check(hostConfig->Flt_GetBool("statsEnable", &statsEnabled), "stats default");
    if (statsEnabled)
        exit(96);
    Check(hostConfig->Flt_SetBool("statsEnable", true), "enable statistics");
    Check(hostConfig->Flt_GetBool("statsEnable", &statsEnabled), "stats enabled readback");
    if (!statsEnabled)
        exit(97);
    std::vector<BYTE> statsPixels(1280 * 1080 * 4);
    Check(config->ReadPixels(1280, 1080, statsPixels.data(), statsPixels.size()), "statistics with host subtitles");
    Check(hostConfig->Flt_SetBool("statsEnable", false), "disable statistics");
    Check(hostConfig->Flt_GetBool("statsEnable", &statsEnabled), "stats disabled readback");
    if (statsEnabled)
        exit(98);
    puts("PASS host statsEnable toggle and statistics GPU composition with subtitle callback");
    std::vector<BYTE> color(64 * 64 * 4), gray(color.size());
    Check(config->ReadPixels(64, 64, color.data(), color.size()), "filter color pixels");
    {
        auto shaderSettings = settings;
        shaderSettings.userShaders.shaders[0].enabled = 1;
        Check(config->SetSettings(&shaderSettings), "packaged AMD CAS enable");
        std::vector<BYTE> shaderPixels(color.size());
        Check(config->ReadPixels(64, 64, shaderPixels.data(), shaderPixels.size()), "packaged custom shader pixels");
        long long delta = 0;
        for (size_t i = 0; i < color.size(); i++)
            if (i % 4 != 3)
                delta += std::abs(int(color[i]) - int(shaderPixels[i]));
        if (delta < 10)
            exit(211);
        Check(config->SetSettings(&settings), "packaged custom shader disable");
        Check(config->ReadPixels(64, 64, shaderPixels.data(), shaderPixels.size()), "packaged all off pixels");
        long long restored = 0;
        for (size_t i = 0; i < color.size(); i++)
            restored += std::abs(int(color[i]) - int(shaderPixels[i]));
        if (restored > 10)
            exit(212);
        printf("PASS packaged custom shader GPU effect %lld and disable restores pixels\n", delta);
    }
    settings.saturation = 0.f;
    Check(config->SetSettings(&settings), "live gray setting");
    Check(config->ReadPixels(64, 64, gray.data(), gray.size()), "filter gray pixels");
    printf("Filter GPU output: color spread %d, gray spread %d, frames %llu\n",
           Spread(color),
           Spread(gray),
           status.rendered);
    if (Spread(color) < 40 || Spread(gray) > 3)
    {
        control->Stop();
        return 3;
    }
    ComPtr<ISubRender11> sub;
    Check(renderer.As(&sub), "subtitle interface");
    ComPtr<SubtitleProbe> probe;
    probe.Attach(new SubtitleProbe);
    probe->empty = true;
    Check(sub->SetCallback11(probe.Get()), "attach subtitle callback");
    Check(config->ReadPixels(64, 64, color.data(), color.size()), "MPC-BE empty subtitle keeps video");
    if (color != gray)
    {
        puts("FAIL empty subtitle changed video pixels");
        return 254;
    }
    probe->empty = false;
    Check(config->ReadPixels(64, 64, color.data(), color.size()), "subtitle pixels");
    size_t center = (32 * 64 + 32) * 4;
    if (color[center] > 2 || color[center + 1] < 253 || color[center + 2] > 2 || !probe->attached || !probe->drawn)
    {
        printf("FAIL subtitle callback/output: RGB=%u/%u/%u attached=%u drawn=%u\n",
               color[center],
               color[center + 1],
               color[center + 2],
               probe->attached,
               probe->drawn);
        return 5;
    }
    probe->empty = true;
    Check(config->ReadPixels(64, 64, color.data(), color.size()), "MPC-BE expired subtitle keeps video");
    if (color != gray)
    {
        puts("FAIL expired subtitle remained over video");
        return 255;
    }
    puts("PASS MPC-BE empty/expired subtitle E_FAIL preserves video and clears previous subtitle");
    Check(sub->SetCallback11(nullptr), "detach subtitles");
    if (!probe->detached)
    {
        printf("FAIL subtitle device not released\n");
        return 6;
    }
    Check(config->ReadPixels(64, 64, color.data(), color.size()), "detached subtitle pixels");
    if (Spread(color) > 3)
    {
        printf("FAIL detached subtitles persisted\n");
        return 7;
    }
    printf("PASS DirectShow subtitle callback device, composed pixels and detach\n");
    {
        PlaceboSettings original;
        Check(config->GetSettings(&original), "analysis baseline settings");
        bool statsBefore = false;
        Check(hostConfig->Flt_GetBool("statsEnable", &statsBefore), "statistics before image analysis");
        std::vector<BYTE> baseline(640 * 360 * 4), analyzed(baseline.size()), restored(baseline.size());
        Check(config->ReadPixels(640, 360, baseline.data(), baseline.size()), "analysis baseline pixels");
        auto enabled = original;
        enabled.imageAnalysis = TRUE;
        Check(config->SetSettings(&enabled), "enable complete image analysis");
        for (int i = 0; i < 35; ++i)
        {
            Check(config->ReadPixels(640, 360, analyzed.data(), analyzed.size()), "live image scopes");
            Sleep(20);
        }
        size_t changed = 0;
        for (int y = 16; y < 260; ++y)
            for (int x = 270; x < 624; ++x)
            {
                const size_t offset = (y * 640 + x) * 4;
                changed += memcmp(analyzed.data() + offset, baseline.data() + offset, 3) != 0;
            }
        Check(changed > 500 ? S_OK : E_FAIL, "all-scopes panel appears in GPU output");
        bool shades[256] = {};
        for (int x = 0; x < 640; ++x)
            shades[analyzed[(345 * 640 + x) * 4]] = true;
        Check(std::count(std::begin(shades), std::end(shades), true) > 40 ? S_OK : E_FAIL,
              "test strip is an actual output gradient inside the fitted video");
        bool statsAfter = false;
        Check(hostConfig->Flt_GetBool("statsEnable", &statsAfter), "statistics during image analysis");
        Check(statsBefore == statsAfter ? S_OK : E_FAIL, "Ctrl+J state remains independent");
        Check(config->SetSettings(&original), "disable image analysis");
        Check(config->ReadPixels(640, 360, restored.data(), restored.size()), "original video after analysis");
        Check(restored == baseline ? S_OK : E_FAIL, "disabling scopes and ramp restores exact video pixels");
        puts("PASS four image scopes, output gray ramp, independent Ctrl+J and exact restoration when disabled");
    }
    if (flushRaceTest)
    {
        if (!hardware)
        {
            puts("FAIL flush race requires GPU sample wrapper");
            return 120;
        }
        sampleTimeEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        sampleTimeRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE flushStarted = CreateEventW(nullptr, TRUE, FALSE, nullptr),
               flushDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Check(control->Run(), "race resume");
        // Arm after Run: the base scheduler may read the paused sample on the
        // control thread during Run; we need the next streaming Receive.
        blockSampleTime = true;
        if (WaitForSingleObject(sampleTimeEntered, 3000) != WAIT_OBJECT_0)
        {
            puts("FAIL no sample entered race gate");
            return 121;
        }
        HRESULT flushed = E_PENDING;
        std::thread flush(
            [&]
            {
                SetEvent(flushStarted);
                flushed = input->BeginFlush();
                SetEvent(flushDone);
            });
        WaitForSingleObject(flushStarted, 1000);
        const bool returnedEarly = WaitForSingleObject(flushDone, 150) == WAIT_OBJECT_0;
        SetEvent(sampleTimeRelease);
        if (WaitForSingleObject(flushDone, 5000) != WAIT_OBJECT_0)
        {
            puts("FAIL flush deadlocked with active input");
            ExitProcess(122);
        }
        flush.join();
        Check(flushed, "concurrent begin flush");
        actual = {};
        Check(diagnosticConfig->GetDiagnostics(&actual), "race flushed diagnostics");
        if (returnedEarly || actual.haveVideo)
        {
            printf("FAIL flush returned before input completed=%d stale video=%d\n", returnedEarly, actual.haveVideo);
            return 123;
        }
        Check(input->EndFlush(), "race end flush");
        Check(input->NewSegment(0, 30000000, 1.), "race new segment");
        Check(control->Stop(), "race stop");
        for (HANDLE event : {sampleTimeEntered, sampleTimeRelease, flushStarted, flushDone})
            CloseHandle(event);
        sampleTimeEntered = sampleTimeRelease = nullptr;
        puts("PASS concurrent flush waits for temporal input, clears stale frames and completes without deadlock");
    }
    Check(control->Stop(), "stop");
    Check(input->BeginFlush(), "begin flush");
    Check(input->NewSegment(420000000, 450000000, 1.), "new segment");
    Check(input->EndFlush(), "end flush");
    Check(control->Run(), "restart");
    Sleep(250);
    Check(control->Stop(), "final stop");
    actual = {};
    Check(diagnosticConfig->GetDiagnostics(&actual), "diagnostics after stop");
    if (actual.haveVideo)
        exit(32);
    // Reuse the same renderer, replacing decoder format and device rather than
    // constructing a fresh filter that could hide stale allocator/texture state.
    for (int phase = 0; phase < 3; phase++)
    {
        Check(graph->Disconnect(rawSource->GetPin(0)), "disconnect decoder");
        Check(graph->Disconnect(input.Get()), "disconnect renderer");
        ComPtr<IMemInputPin> mem;
        Check(input.As(&mem), "input allocator interface");
        ComPtr<IMemAllocator> available;
        Check(mem->GetAllocator(&available), "CPU allocator restored after disconnect");
        available.Reset();
        hardware = phase != 0;
        textureFormat = phase == 1 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010;
        Check(graph->ConnectDirect(rawSource->GetPin(0), input.Get(), nullptr), "reconnect changed decoder");
        Check(config->GetStatus(&status), "reconnect baseline");
        const auto beforeFrames = status.rendered, beforeHardware = status.hardwareFrames;
        Check(control->Run(), "run reconnected decoder");
        const DWORD deadline = GetTickCount();
        do
        {
            Sleep(10);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Check(config->GetStatus(&status), "reconnect status");
        } while (status.rendered < beforeFrames + 5 && GetTickCount() - deadline < 5000);
        Check(control->Pause(), "pause reconnected decoder");
        Check(control->GetState(3000, &paused), "reconnected paused state");
        if (status.rendered < beforeFrames + 5 || FAILED(status.lastResult) ||
            (hardware ? status.hardwareFrames < beforeHardware + 5 : status.hardwareFrames != beforeHardware))
        {
            printf("FAIL reconnect did not use requested CPU/GPU path\n");
            return 16;
        }
        Check(config->ReadPixels(64, 64, gray.data(), gray.size()), "reconnected image pixels");
        if (Spread(gray) > 3 || gray[0] < 30)
        {
            printf("FAIL stale/empty image after decoder change\n");
            return 17;
        }
        actual = {};
        Check(diagnosticConfig->GetDiagnostics(&actual), "diagnostics after decoder change");
        if (!actual.haveVideo || !!actual.directGpu != hardware ||
            strcmp(actual.pixelFormat, phase == 1 ? "NV12" : "P010"))
            exit(33);
        Check(control->Stop(), "stop reconnected decoder");
        printf("PASS same renderer reconnect: %s %s, new frames and pixels verified\n",
               hardware ? "GPU" : "CPU",
               textureFormat == DXGI_FORMAT_NV12 ? "NV12" : "P010");
    }
    PlaceboSettings persist;
    Check(config->GetSettings(&persist), "settings before persistence check");
    persist.allowDelayedPeak = TRUE;
    persist.kneeAdaptation = .7f;
    persist.splineContrast = .8f;
    persist.kneeMinimum = .2f;
    persist.kneeMaximum = .9f;
    persist.blackCutoff = 2;
    persist.perceptualStrength = .6f;
    persist.softclipKnee = .65f;
    persist.showClipping = TRUE;
    persist.imageAnalysis = TRUE;
    persist.maximumFramesInFlight = 7;
    persist.toneMapping = 2;
    persist.hdrNits = 600;
    persist.antiringing = .6f;
    persist.upscalerBlur = .9f;
    persist.downscalerBlur = 1.2f;
    persist.debandIterations = 3;
    persist.debandThreshold = 5;
    persist.debandRadius = 24;
    persist.debandGrain = 1;
    persist.ditherMethod = 2;
    persist.ditherTemporal = TRUE;
    persist.chromaScaler = 4;
    persist.adaptiveSharpen = TRUE;
    persist.sharpenStrength = .75f;
    persist.peakDetect = FALSE;
    persist.peakSmoothing = 35;
    persist.sceneLow = .5f;
    persist.sceneHigh = 4;
    persist.peakPercentile = 99.995f;
    persist.contrastRecovery = .4f;
    persist.contrastSmoothness = 5;
    persist.gamutMapping = 3;
    persist.outputLevels = 2;
    persist.displayDepth = 12;
    persist.outputFormat = 3;
    Check(config->SetSettings(&persist), "save advanced settings");
    ComPtr<IBaseFilter> second;
    ComPtr<IPlaceboSettings> secondConfig;
    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&second)), "second renderer");
    Check(second.As(&secondConfig), "second settings");
    // An unconnected renderer reads the legacy root. Connect it so it selects
    // the output display profile, just as a player does before playback.
    ComPtr<IGraphBuilder> secondGraph;
    Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&secondGraph)),
          "second graph");
    Check(secondGraph->AddFilter(second.Get(), L"readback renderer"), "add second renderer");
    ComPtr<IVideoWindow> secondWindow;
    Check(second.As(&secondWindow), "second video window");
    Check(secondWindow->put_Owner(reinterpret_cast<OAHWND>(ownerWindow.handle)), "second owner");
    auto secondRawSource = new PatternSource(&hr);
    Check(hr, "second source create");
    ComPtr<IBaseFilter> secondSource;
    Check(secondRawSource->NonDelegatingQueryInterface(IID_PPV_ARGS(&secondSource)), "second source interface");
    Check(secondGraph->AddFilter(secondSource.Get(), L"readback source"), "add second source");
    ComPtr<IEnumPins> secondPins;
    ComPtr<IPin> secondInput;
    Check(second->EnumPins(&secondPins), "second input list");
    Check(secondPins->Next(1, &secondInput, nullptr), "second input");
    Check(secondGraph->ConnectDirect(secondRawSource->GetPin(0), secondInput.Get(), nullptr),
          "connect saved display profile");
    PlaceboSettings readback;
    Check(secondConfig->GetSettings(&readback), "saved readback");
    if (wcscmp(readback.displayProfile, persist.displayProfile) || !readback.allowDelayedPeak ||
        readback.maximumFramesInFlight != 7 || readback.kneeAdaptation != .7f || readback.splineContrast != .8f ||
        readback.kneeMinimum != .2f || readback.kneeMaximum != .9f || readback.blackCutoff != 2 ||
        readback.perceptualStrength != .6f || readback.softclipKnee != .65f || !readback.showClipping ||
        !readback.imageAnalysis || readback.adaptiveSharpen != TRUE || readback.sharpenStrength != .75f ||
        readback.smoothMotion != persist.smoothMotion || readback.saturation != 0.f || readback.toneMapping != 2 ||
        readback.hdrNits != 600 || readback.antiringing != .6f || readback.upscalerBlur != .9f ||
        readback.downscalerBlur != 1.2f || readback.debandIterations != 3 || readback.debandThreshold != 5 ||
        readback.debandRadius != 24 || readback.debandGrain != 1 || readback.ditherMethod != 2 ||
        !readback.ditherTemporal || readback.chromaScaler != 4 || readback.outputFormat != 3 ||
        readback.outputLevels != 2 || readback.displayDepth != 12 || readback.peakDetect ||
        readback.peakSmoothing != 35 || readback.sceneLow != .5f || readback.sceneHigh != 4 ||
        readback.peakPercentile != 99.995f || readback.contrastRecovery != .4f || readback.contrastSmoothness != 5 ||
        readback.gamutMapping != 3)
    {
        printf("FAIL saved advanced settings not read\n");
        return 4;
    }
    printf(
        "PASS DirectShow %s, clocked playback, pause, measured live setting, flush/new segment, restart, stop, saved settings\n",
        textureFormat == DXGI_FORMAT_NV12   ? "NV12"
        : textureFormat == DXGI_FORMAT_P016 ? "P016"
                                            : "P010");
    return 0;
}
