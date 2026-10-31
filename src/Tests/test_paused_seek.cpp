#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include "Settings/Settings.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;

static void Check(HRESULT hr, const char* text)
{
    if (FAILED(hr))
    {
        printf("FAIL %s %08lX\n", text, static_cast<unsigned long>(hr));
        throw std::runtime_error(text);
    }
}

static ComPtr<IBaseFilter> Load(const std::wstring& path, const CLSID& id)
{
    auto dll =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll)
        Check(HRESULT_FROM_WIN32(GetLastError()), "load filter");
    auto getter =
        reinterpret_cast<HRESULT(WINAPI*)(REFCLSID, REFIID, void**)>(GetProcAddress(dll, "DllGetClassObject"));
    if (!getter)
        Check(E_NOINTERFACE, "filter factory export");
    ComPtr<IClassFactory> factory;
    Check(getter(id, IID_PPV_ARGS(&factory)), "filter factory");
    ComPtr<IBaseFilter> filter;
    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&filter)), "filter instance");
    return filter;
}

static ComPtr<IPin> Pin(IBaseFilter* filter, PIN_DIRECTION direction)
{
    ComPtr<IEnumPins> pins;
    Check(filter->EnumPins(&pins), "enumerate pins");
    ComPtr<IPin> pin;
    while (pins->Next(1, &pin, nullptr) == S_OK)
    {
        PIN_DIRECTION current;
        Check(pin->QueryDirection(&current), "pin direction");
        if (current == direction)
            return pin;
        pin.Reset();
    }
    Check(E_NOINTERFACE, "requested pin");
    return {};
}

static void Pump()
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Sleep(10);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4 || argc > 6)
    {
        puts("Usage: test_paused_seek renderer.ax LAVFilters64 video [--motion] [--potplayer]");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", L"Software\\FrankyRendererTest\\PausedSeek");
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_DEFER_APPLY", L"1");
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM");
    try
    {
        const CLSID splitterId = {0xb98d13e7, 0x55db, 0x4385, {0xa3, 0x3d, 0x09, 0xfd, 0x1b, 0xa2, 0x63, 0x38}};
        const CLSID decoderId = {0xee30215d, 0x164f, 0x4a92, {0xa4, 0xeb, 0x9d, 0x4c, 0x13, 0x39, 0x0f, 0x9f}};
        auto splitter = Load(std::wstring(argv[2]) + L"\\LAVSplitter.ax", splitterId);
        auto decoder = Load(std::wstring(argv[2]) + L"\\LAVVideo.ax", decoderId);
        bool potplayer = false, motion = false;
        for (int i = 4; i < argc; ++i)
        {
            potplayer |= wcscmp(argv[i], L"--potplayer") == 0;
            motion |= wcscmp(argv[i], L"--motion") == 0;
        }
        // Both players use the same native class since the packages were unified.
        (void)potplayer;
        auto renderer = Load(argv[1], CLSID_PlaceboRenderer);
        ComPtr<IGraphBuilder> graph;
        Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), "graph");
        Check(graph->AddFilter(splitter.Get(), L"Existing LAV splitter"), "add splitter");
        Check(graph->AddFilter(decoder.Get(), L"Existing LAV decoder"), "add decoder");
        Check(graph->AddFilter(renderer.Get(), L"FrankyRenderer"), "add renderer");
        ComPtr<IFileSourceFilter> source;
        Check(splitter.As(&source), "file source");
        Check(source->Load(argv[3], nullptr), "load video");
        ComPtr<IVideoWindow> window;
        Check(renderer.As(&window), "video window");
        HWND owner = CreateWindowExW(0,
                                     L"STATIC",
                                     L"Paused seek test",
                                     WS_OVERLAPPEDWINDOW,
                                     0,
                                     0,
                                     400,
                                     260,
                                     nullptr,
                                     nullptr,
                                     GetModuleHandleW(nullptr),
                                     nullptr);
        if (!owner)
            Check(HRESULT_FROM_WIN32(GetLastError()), "test owner");
        ShowWindow(owner, SW_SHOWNOACTIVATE);
        Check(window->put_Owner(reinterpret_cast<OAHWND>(owner)), "video owner");
        Check(graph->ConnectDirect(
                  Pin(splitter.Get(), PINDIR_OUTPUT).Get(), Pin(decoder.Get(), PINDIR_INPUT).Get(), nullptr),
              "splitter to decoder");
        Check(graph->ConnectDirect(
                  Pin(decoder.Get(), PINDIR_OUTPUT).Get(), Pin(renderer.Get(), PINDIR_INPUT).Get(), nullptr),
              "decoder to renderer");
        Check(window->SetWindowPosition(0, 0, 320, 180), "video size");
        ComPtr<IPlaceboSettings> config;
        Check(renderer.As(&config), "renderer settings");
        PlaceboSettings settings;
        Check(config->GetSettings(&settings), "read settings");
        settings.hdrMode = 2;
        settings.hdrPassthrough = settings.autoHdr = FALSE;
        settings.smoothMotion = motion;
        settings.rtxSuperResolution = FALSE;
        Check(config->SetSettings(&settings), "isolated test settings");
        ComPtr<IMediaControl> control;
        ComPtr<IMediaSeeking> seeking;
        Check(graph.As(&control), "graph control");
        Check(graph.As(&seeking), "graph seeking");
        auto waitPreview = [&](uint64_t previous)
        {
            PlaceboStatus status;
            DWORD started = GetTickCount();
            do
            {
                Pump();
                Check(config->GetStatus(&status), "preview status");
                Check(status.lastResult, "preview render");
            } while (status.rendered <= previous && GetTickCount() - started < 5000);
            OAFilterState state = State_Stopped;
            Check(control->GetState(1000, &state), "paused graph state");
            if (state != State_Paused || status.rendered <= previous)
                Check(E_FAIL, "paused preview did not refresh without Run");
            return status.rendered;
        };
        PlaceboStatus baseline;
        Check(config->GetStatus(&baseline), "initial status");
        Check(control->Pause(), "initial pause");
        uint64_t rendered = waitPreview(baseline.rendered);
        std::vector<BYTE> pixels(320 * 180 * 4), next(pixels.size());
        Check(config->ReadPixels(320, 180, pixels.data(), pixels.size()), "initial preview pixels");
        for (int seconds : {5, 12, 2})
        {
            REFERENCE_TIME target = seconds * 10000000LL;
            Check(seeking->SetPositions(&target, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning),
                  "seek while paused");
            rendered = waitPreview(rendered);
            Check(config->ReadPixels(320, 180, next.data(), next.size()), "new paused pixels");
            if (pixels == next)
                Check(E_FAIL, "paused seek retained old pixels");
            pixels.swap(next);
            printf("PASS paused seek to %d seconds updates actual pixels without Run\n", seconds);
        }
        Check(control->Run(), "resume after paused seeks");
        DWORD started = GetTickCount();
        PlaceboStatus resumed;
        do
        {
            Pump();
            Check(config->GetStatus(&resumed), "resumed status");
        } while (resumed.rendered < rendered + 5 && GetTickCount() - started < 5000);
        if (resumed.rendered < rendered + 5)
            Check(E_FAIL, "resume after previews");
        Check(control->Stop(), "stop graph");
        DestroyWindow(owner);
        printf("PASS paused preview, forward/backward seeking and resume; smooth motion=%d\n", settings.smoothMotion);
    }
    catch (const std::exception&)
    {
        return 1;
    }
    return 0;
}
