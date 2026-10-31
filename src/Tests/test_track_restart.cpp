#include "Renderer/Filter.h"
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <thread>
using Microsoft::WRL::ComPtr;
HINSTANCE g_hInst = nullptr;

static void Check(HRESULT hr, const char* what)
{
    if (FAILED(hr))
    {
        printf("FAIL %s %08lx\n", what, hr);
        exit(1);
    }
}

static void Require(bool ok, const char* what)
{
    Check(ok ? S_OK : E_FAIL, what);
}

class Source final : public CBaseFilter
{
    CCritSec lock;

  public:
    class Output final : public CBaseOutputPin
    {
      public:
        CMediaType type;

        Output(Source* source, CCritSec* lock, HRESULT* hr)
            : CBaseOutputPin(NAME("Restart test output"), source, lock, hr, L"Output")
        {
            VIDEOINFOHEADER2 info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = info.bmiHeader.biHeight = 64;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            info.bmiHeader.biSizeImage = 64 * 64 * 4;
            info.AvgTimePerFrame = 400000;
            info.dwPictAspectRatioX = info.dwPictAspectRatioY = 1;
            type.SetType(&MEDIATYPE_Video);
            type.SetSubtype(&MEDIASUBTYPE_RGB32);
            type.SetFormatType(&FORMAT_VideoInfo2);
            type.SetFormat((BYTE*)&info, sizeof(info));
            type.SetSampleSize(info.bmiHeader.biSizeImage);
        }

        HRESULT CheckMediaType(const CMediaType* mt) override
        {
            return *mt == type ? S_OK : VFW_E_TYPE_NOT_ACCEPTED;
        }

        HRESULT GetMediaType(int index, CMediaType* mt) override
        {
            if (index < 0)
                return E_INVALIDARG;
            if (index)
                return VFW_S_NO_MORE_ITEMS;
            *mt = type;
            return S_OK;
        }

        HRESULT DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* props) override
        {
            props->cBuffers = 3;
            props->cbBuffer = 64 * 64 * 4;
            props->cbAlign = 1;
            ALLOCATOR_PROPERTIES actual{};
            return allocator->SetProperties(props, &actual);
        }

        void Send(int channel, int index)
        {
            ComPtr<IMediaSample> sample;
            Check(GetDeliveryBuffer(&sample, nullptr, nullptr, 0), "video buffer");
            BYTE* data = nullptr;
            Check(sample->GetPointer(&data), "video data");
            memset(data, 0, 64 * 64 * 4);
            for (int i = 0; i < 64 * 64; ++i)
                data[i * 4 + channel] = 180;
            REFERENCE_TIME start = index * 400000LL, stop = start + 400000;
            sample->SetTime(&start, &stop);
            sample->SetSyncPoint(TRUE);
            sample->SetActualDataLength(64 * 64 * 4);
            Check(Deliver(sample.Get()), "video sample");
        }
    } output;

    Source(HRESULT* hr) : CBaseFilter(NAME("Restart test source"), nullptr, &lock, CLSID_NULL), output(this, &lock, hr)
    {
    }

    int GetPinCount() override
    {
        return 1;
    }

    CBasePin* GetPin(int index) override
    {
        return index == 0 ? &output : nullptr;
    }
};

class RendererRestartTest
{
  public:
    static void Screenshots()
    {
        HRESULT hr = S_OK;
        auto renderer = new Renderer(nullptr, &hr);
        Check(hr, "screenshot renderer");
        ComPtr<IBaseFilter> filter;
        Check(renderer->QueryInterface(IID_PPV_ARGS(&filter)), "screenshot renderer interface");
        auto source = new Source(&hr);
        Check(hr, "screenshot source");
        ComPtr<IBaseFilter> sourceRef;
        Check(source->QueryInterface(IID_PPV_ARGS(&sourceRef)), "screenshot source interface");
        ComPtr<IGraphBuilder> graph;
        Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)),
              "screenshot graph");
        Check(graph->AddFilter(sourceRef.Get(), L"Source"), "add screenshot source");
        Check(graph->AddFilter(filter.Get(), L"Renderer"), "add screenshot renderer");
        Check(graph->ConnectDirect(&source->output, renderer->GetPin(0), &source->output.type),
              "screenshot connection");
        HWND host = CreateWindowExW(0,
                                    L"STATIC",
                                    L"Private screenshot test",
                                    WS_OVERLAPPEDWINDOW,
                                    0,
                                    0,
                                    256,
                                    256,
                                    nullptr,
                                    nullptr,
                                    GetModuleHandleW(nullptr),
                                    nullptr);
        Require(host != nullptr, "screenshot owner window");
        ComPtr<IVideoWindow> video;
        Check(filter.As(&video), "screenshot video window");
        Check(video->put_Owner(reinterpret_cast<OAHWND>(host)), "screenshot video owner");
        PlaceboSettings settings;
        Check(renderer->GetSettings(&settings), "screenshot settings");
        settings.hdrMode = 2;
        settings.smoothMotion = FALSE;
        settings.ditherMethod = 3;
        Check(renderer->SetSettings(&settings), "apply screenshot settings");
        ComPtr<IMediaControl> playback;
        Check(graph.As(&playback), "screenshot playback");
        Check(playback->Run(), "run screenshot graph");
        for (int i = 0; i < 8; ++i)
            source->output.Send(1, i);
        Check(video->SetWindowPosition(0, 0, 128, 128), "screenshot viewport");
        ComPtr<IBasicVideo> basic;
        Check(filter.As(&basic), "screenshot basic video");
        Check(basic->SetDestinationPosition(32, 32, 64, 64), "offset window destination");
        long bytes = 0;
        Check(basic->GetCurrentImage(&bytes, nullptr), "source screenshot size query");
        Require(bytes == long(sizeof(BITMAPINFOHEADER) + 64 * 64 * 4), "source screenshot buffer size");
        std::vector<BYTE> full(bytes);
        Check(basic->GetCurrentImage(&bytes, reinterpret_cast<long*>(full.data())), "source screenshot pixels");
        const auto* header = reinterpret_cast<const BITMAPINFOHEADER*>(full.data());
        Require(header->biWidth == 64 && header->biHeight == -64 && header->biBitCount == 32,
                "source screenshot DIB header");
        bool wholeImage = true;
        const auto* pixels = full.data() + sizeof(BITMAPINFOHEADER);
        for (int y : {0, 8, 32, 55, 63})
            for (int x : {0, 8, 32, 55, 63})
                wholeImage &= pixels[(y * 64 + x) * 4 + 1] > 50;
        ComPtr<IExFilterConfig> config;
        Check(filter.As(&config), "screenshot host interface");
        LPVOID displayed = nullptr;
        unsigned displayedBytes = 0;
        hr = config->Flt_GetBin("displayedImage", &displayed, &displayedBytes);
        printf("Screenshot reproduction: complete source=%d, displayed result=%08lx\n", wholeImage, hr);
        Check(hr, "MPC-BE displayedImage request");
        const auto* shown = static_cast<const BITMAPINFOHEADER*>(displayed);
        Require(shown && shown->biWidth == 128 && shown->biHeight == -128 && shown->biBitCount == 32,
                "displayed screenshot uses the viewport dimensions");
        Require(displayedBytes == sizeof(BITMAPINFOHEADER) + 128 * 128 * 4, "displayed screenshot allocation size");
        const auto* shownPixels = static_cast<const BYTE*>(displayed) + sizeof(BITMAPINFOHEADER);
        Require(shownPixels[(64 * 128 + 64) * 4 + 1] > 50 && shownPixels[(8 * 128 + 8) * 4 + 1] < 3,
                "displayed screenshot preserves window placement and black borders");
        const auto* displayedStart = static_cast<const BYTE*>(displayed);
        std::vector<BYTE> displayedReference(displayedStart, displayedStart + displayedBytes);
        LocalFree(displayed);
        Require(wholeImage, "source screenshot must ignore window destination geometry");
        Check(playback->Pause(), "pause screenshot graph");
        std::vector<BYTE> paused(full.size());
        Check(basic->GetCurrentImage(&bytes, reinterpret_cast<long*>(paused.data())), "paused source screenshot");
        Require(paused == full, "paused source screenshot preserves complete pixels");
        displayed = nullptr;
        Check(config->Flt_GetBin("displayedImage", &displayed, &displayedBytes), "paused displayed screenshot");
        Require(displayedBytes == displayedReference.size() &&
                    !memcmp(displayed, displayedReference.data(), displayedBytes),
                "paused displayed screenshot preserves viewport pixels");
        LocalFree(displayed);
        long shortBufferBytes = bytes - 1;
        Require(basic->GetCurrentImage(&shortBufferBytes, reinterpret_cast<long*>(full.data())) == E_OUTOFMEMORY &&
                    shortBufferBytes == bytes,
                "source screenshot reports an undersized buffer");
        Require(config->Flt_GetBin("displayedImage", nullptr, &displayedBytes) == E_POINTER,
                "displayed screenshot validates output pointer");
        Check(playback->Stop(), "stop screenshot graph");
        video->put_Owner(0);
        DestroyWindow(host);
        puts(
            "PASS screenshots: complete source frame, displayed viewport, live/paused pixels, offset destination, DIB sizes and LocalFree ownership");
    }

    static void Run(bool motion)
    {
        HRESULT hr = S_OK;
        auto renderer = new Renderer(nullptr, &hr);
        Check(hr, "renderer");
        ComPtr<IBaseFilter> filter;
        Check(renderer->QueryInterface(IID_PPV_ARGS(&filter)), "renderer reference");
        auto source = new Source(&hr);
        Check(hr, "source");
        ComPtr<IBaseFilter> sourceRef;
        Check(source->QueryInterface(IID_PPV_ARGS(&sourceRef)), "source reference");
        ComPtr<IGraphBuilder> graph;
        Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), "graph");
        Check(graph->AddFilter(sourceRef.Get(), L"Source"), "add source");
        Check(graph->AddFilter(filter.Get(), L"Renderer"), "add renderer");
        Check(graph->ConnectDirect(&source->output, renderer->GetPin(0), &source->output.type), "video connection");
        HWND host = CreateWindowExW(0,
                                    L"STATIC",
                                    L"Private restart test",
                                    WS_OVERLAPPEDWINDOW,
                                    0,
                                    0,
                                    128,
                                    128,
                                    nullptr,
                                    nullptr,
                                    GetModuleHandleW(nullptr),
                                    nullptr);
        Require(host != nullptr, "owner window");
        ComPtr<IVideoWindow> video;
        Check(filter.As(&video), "video window");
        Check(video->put_Owner(reinterpret_cast<OAHWND>(host)), "owner");
        Check(video->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS), "child style");
        Check(video->SetWindowPosition(0, 0, 64, 64), "video dimensions");
        PlaceboSettings settings;
        Check(renderer->GetSettings(&settings), "settings");
        settings.hdrMode = 2;
        settings.hdrPassthrough = settings.autoHdr = FALSE;
        settings.outputFormat = 2;
        settings.smoothMotion = motion;
        settings.rtxSuperResolution = false;
        Check(renderer->SetSettings(&settings), "test settings");
        std::atomic<unsigned> presented = 0, black = 0;
        double green = 0, red = 0;
        auto pixelProbe = [&](ID3D11Texture2D* texture)
        {
            ComPtr<ID3D11Device> device;
            texture->GetDevice(&device);
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            auto format = desc.Format;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> copy;
            Check(device->CreateTexture2D(&desc, nullptr, &copy), "capture");
            context->CopyResource(copy.Get(), texture);
            D3D11_MAPPED_SUBRESOURCE map{};
            Check(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &map), "map capture");
            auto pixel = static_cast<const BYTE*>(map.pData) + (desc.Height / 2) * map.RowPitch + (desc.Width / 2) * 4;
            if (format == DXGI_FORMAT_R10G10B10A2_UNORM)
            {
                auto value = *reinterpret_cast<const UINT*>(pixel);
                red = (value & 1023) / 1023.;
                green = ((value >> 10) & 1023) / 1023.;
            }
            else
            {
                green = pixel[1] / 255.;
                red = pixel[2] / 255.;
            }
            context->Unmap(copy.Get(), 0);
            ++presented;
            if (green < .001 && red < .001)
                ++black;
        };
        renderer->backend.SetPresentProbeForTest(pixelProbe);
        ComPtr<IMediaFilter> media;
        Check(graph.As(&media), "media filter");
        Check(media->SetSyncSource(nullptr), "no clock");
        ComPtr<IMediaControl> control;
        Check(graph.As(&control), "control");
        Check(control->Run(), "initial run");
        for (int i = 0; i < 8; ++i)
            source->output.Send(1, i);
        Require(presented > 0 && green > .1, "actual green picture presented");
        if (!motion)
        {
            HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            Require(entered && release, "redraw synchronization");
            std::atomic<unsigned> redraws = 0;
            renderer->backend.SetPresentProbeForTest(
                [&](ID3D11Texture2D*)
                {
                    if (++redraws == 1)
                    {
                        SetEvent(entered);
                        Require(WaitForSingleObject(release, 5000) == WAIT_OBJECT_0, "release rendering thread");
                    }
                });
            std::thread rendering([&] { Check(renderer->backend.Present(), "occupied rendering thread"); });
            Require(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0, "rendering thread entered");
            renderer->Repaint();
            SetEvent(release);
            rendering.join();
            for (int i = 0; i < 50 && redraws < 2; ++i)
                Sleep(10);
            Require(redraws >= 2, "busy paused redraw must be retried without another video sample");
            renderer->backend.SetPresentProbeForTest({});
            CloseHandle(entered);
            CloseHandle(release);
            printf("PASS busy paused redraw is retried\n");
            renderer->backend.SetPresentProbeForTest(pixelProbe);
        }
        // Real WinEvent delivery reaches the renderer's separate window thread.
        NotifyWinEvent(EVENT_SYSTEM_MOVESIZESTART, host, OBJID_WINDOW, CHILDID_SELF);
        Sleep(100);
        unsigned beforeMove = presented;
        Require(SetWindowPos(host, nullptr, 10, 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE),
                "move without resize");
        source->output.Send(1, 8);
        Require(presented > beforeMove, "moving without resizing never holds presentation");
        NotifyWinEvent(EVENT_SYSTEM_MOVESIZEEND, host, OBJID_WINDOW, CHILDID_SELF);
        Sleep(50);
        NotifyWinEvent(EVENT_SYSTEM_MOVESIZESTART, host, OBJID_WINDOW, CHILDID_SELF);
        Sleep(100);
        Check(video->SetWindowPosition(0, 0, 320, 180), "interactive resize");
        Sleep(10);
        unsigned before = presented;
        for (int i = 8; i < 16; ++i)
            source->output.Send(2, i);
        Require(presented == before, "interactive resize holds presentation while delivery continues");
        OAFilterState running = State_Stopped;
        Check(control->GetState(0, &running), "state during resize");
        Require(running == State_Running, "interactive resize never pauses the graph");
        for (int i = 0; i < 8; ++i)
        {
            Check(video->SetWindowPosition(0, 0, 321 + i, 180), "continuous interactive resize");
            Sleep(10);
            Require(presented == before, "continuous size changes keep the displayed frame held");
        }
        for (int i = 0; i < 50 && presented == before; ++i)
            Sleep(10);
        Require(presented > before && red > .1, "size settling resumes the newest picture before mouse release");
        before = presented;
        for (int i = 16; i < 24; ++i)
            source->output.Send(1, i);
        Require(presented > before && green > .1, "video continues while the resize gesture remains open");
        Check(video->SetWindowPosition(0, 0, 360, 200), "continue dragging after settling");
        Sleep(10);
        before = presented;
        for (int i = 24; i < 32; ++i)
            source->output.Send(2, i);
        Require(presented == before, "resuming size changes holds presentation again");
        NotifyWinEvent(EVENT_SYSTEM_MOVESIZEEND, host, OBJID_WINDOW, CHILDID_SELF);
        for (int i = 0; i < 50 && presented == before; ++i)
            Sleep(10);
        Require(presented > before && red > .1, "mouse release resumes immediately");
        Check(control->Pause(), "pause before resize");
        NotifyWinEvent(EVENT_SYSTEM_MOVESIZESTART, host, OBJID_WINDOW, CHILDID_SELF);
        Sleep(100);
        Check(video->SetWindowPosition(0, 0, 400, 224), "paused interactive resize");
        Sleep(10);
        before = presented;
        renderer->Repaint();
        Require(presented == before, "paused resize also holds the displayed frame");
        for (int i = 0; i < 50 && presented == before; ++i)
            Sleep(10);
        Require(presented > before, "paused size settling redraws before mouse release without another sample");
        NotifyWinEvent(EVENT_SYSTEM_MOVESIZEEND, host, OBJID_WINDOW, CHILDID_SELF);
        Sleep(50);
        Check(control->Run(), "resume after resize");
        Check(video->SetWindowPosition(0, 0, 64, 64), "programmatic resize remains live");
        Sleep(50);
        printf(
            "PASS resize holds during changes, resumes on settling before release, holds again on further changes; paused redraw\n");
        before = presented;
        Check(control->Stop(), "temporary graph stop");
        renderer->Repaint();
        Check(renderer->GetPin(0)->BeginFlush(), "flush stopped graph");
        Check(renderer->GetPin(0)->EndFlush(), "end stopped flush");
        renderer->Repaint();
        Require(black == 0 && presented == before, "audio-track restart must not present black");
        Check(control->Run(), "restart graph");
        Check(renderer->GetPin(0)->NewSegment(0, 100000000, 1.), "new segment");
        renderer->Repaint();
        Require(black == 0 && presented == before, "new segment must retain displayed surface until first video");
        for (int i = 0; i < 8; ++i)
            source->output.Send(2, i);
        Require(presented > before && red > .1 && black == 0, "new red video replaces the retained picture");
        Check(control->Stop(), "final stop");
        Check(graph->Disconnect(&source->output), "source disconnect");
        Check(graph->Disconnect(renderer->GetPin(0)), "renderer disconnect");
        Require(black == 1, "real video disconnect clears actual GPU output");
        renderer->backend.SetPresentProbeForTest({});
        video->put_Owner(0);
        DestroyWindow(host);
        printf("PASS actual GPU output: no black during Stop/flush/restart; disconnect clears; smooth motion=%d\n",
               motion);
    }
};

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    wchar_t key[128];
    swprintf_s(key, L"Software\\FrankyRestartTest\\%lu", GetCurrentProcessId());
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", key);
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_DEFER_APPLY", L"1");
    Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM");
    RendererRestartTest::Screenshots();
    RendererRestartTest::Run(false);
    RendererRestartTest::Run(true);
    RegDeleteTreeW(HKEY_CURRENT_USER, key);
    return 0;
}
