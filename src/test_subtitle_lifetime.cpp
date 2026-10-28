// The aggregated presenter must not be retained by its own renderer callback.
#include <windows.h>
#include <dshow.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "DirectShow/Include/ISubRender11.h"
#include <atomic>
#include <cstdio>
#include <stdexcept>
using Microsoft::WRL::ComPtr;

static void Check(HRESULT hr, const char* message)
{
    if (FAILED(hr))
        throw std::runtime_error(message);
}

class Presenter final : public ISubRender11Callback
{
    std::atomic<ULONG> refs{1};
    ComPtr<IUnknown> inner;
    unsigned* destroyed;

  public:
    explicit Presenter(unsigned& count) : destroyed(&count) {}

    ~Presenter()
    {
        inner.Reset();
        ++*destroyed;
    }

    void Initialize(IClassFactory* factory)
    {
        Check(factory->CreateInstance(this, IID_PPV_ARGS(&inner)), "create aggregated renderer");
        ULONG before = refs;
        {
            ComPtr<ISubRender11> subtitles;
            Check(inner.As(&subtitles), "renderer subtitle interface");
            Check(subtitles->SetCallback11(this), "attach aggregated presenter callback");
        }
        if (refs != before)
            throw std::runtime_error("renderer retains its own aggregated presenter");
    }

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == __uuidof(ISubRender11Callback))
        {
            *out = static_cast<ISubRender11Callback*>(this);
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
        ULONG left = --refs;
        if (!left)
            delete this;
        return left;
    }

    STDMETHODIMP SetDevice11(ID3D11Device*) override
    {
        return S_OK;
    }

    STDMETHODIMP Render11(REFERENCE_TIME, REFERENCE_TIME, REFERENCE_TIME, RECT, RECT, RECT, double, int, DWORD) override
    {
        return S_FALSE;
    }
};

class IndependentCallback final : public ISubRender11Callback
{
    std::atomic<ULONG> refs{1};
    bool* destroyed;

  public:
    explicit IndependentCallback(bool& value) : destroyed(&value) {}

    ~IndependentCallback()
    {
        *destroyed = true;
    }

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
        ULONG left = --refs;
        if (!left)
            delete this;
        return left;
    }

    STDMETHODIMP SetDevice11(ID3D11Device*) override
    {
        return S_OK;
    }

    STDMETHODIMP Render11(REFERENCE_TIME, REFERENCE_TIME, REFERENCE_TIME, RECT, RECT, RECT, double, int, DWORD) override
    {
        return S_FALSE;
    }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 && argc != 3)
        return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", L"Software\\FrankyTests\\SubtitleLifetime");
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_DEFER_APPLY", L"1");
    try
    {
        Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM");
        auto dll =
            LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll)
            throw std::runtime_error("load renderer");
        using Getter = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
        auto getter = reinterpret_cast<Getter>(GetProcAddress(dll, "DllGetClassObject"));
        if (!getter)
            throw std::runtime_error("factory export");
        const CLSID id = {0xb864e92a, 0x1bdf, 0x4da1, {0x90, 0x04, 0xcb, 0xf4, 0x67, 0xdf, 0x23, 0x9a}};
        const CLSID alias = {0x71f080aa, 0x8661, 0x4093, {0xb1, 0x5e, 0x4f, 0x69, 0x03, 0xe7, 0x7d, 0x0a}};
        ComPtr<IClassFactory> factory;
        Check(getter(argc == 3 && wcscmp(argv[2], L"--potplayer") == 0 ? alias : id, IID_PPV_ARGS(&factory)),
              "renderer factory");
        unsigned destroyed = 0;
        for (unsigned i = 0; i < 20; ++i)
        {
            ComPtr<Presenter> presenter;
            presenter.Attach(new Presenter(destroyed));
            presenter->Initialize(factory.Get());
            presenter.Reset();
            if (destroyed != i + 1)
                throw std::runtime_error("presenter was not destroyed on file change");
        }
        puts("PASS 20 aggregated presenter callbacks release without a reference cycle");
        ComPtr<ISubRender11> renderer;
        Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&renderer)), "standalone renderer");
        bool released = false;
        ComPtr<IndependentCallback> callback;
        callback.Attach(new IndependentCallback(released));
        Check(renderer->SetCallback11(callback.Get()), "independent callback attach");
        callback.Reset();
        if (released)
            throw std::runtime_error("independent callback was not retained");
        Check(renderer->SetCallback11(nullptr), "independent callback detach");
        if (!released)
            throw std::runtime_error("independent callback was not released");
        puts("PASS independent callback retained until explicit detach");
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("FAIL subtitle lifetime: %s\n", e.what());
        return 1;
    }
}
