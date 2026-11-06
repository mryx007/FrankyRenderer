#include "DolbyVision/Processor.h"
#include "DirectShow/external/BaseClasses/streams.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>
extern "C"
{
#include <libavutil/log.h>
}

using Microsoft::WRL::ComPtr;
HINSTANCE g_hInst = nullptr;
static bool scriptExecuted = false;

static void CaptureLog(void*, int level, const char* format, va_list args)
{
    if (level > av_log_get_level())
        return;
    char text[4096] = {};
    vsnprintf(text, sizeof(text), format, args);
    if (strstr(text, "FRANKY_PROBE_EXECUTED_SCRIPT"))
        scriptExecuted = true;
    fputs(text, stderr);
}

static void Check(HRESULT result, const char* message)
{
    if (FAILED(result))
    {
        printf("FAIL %s: %08lx\n", message, result);
        throw std::runtime_error(message);
    }
}

class FileSource final : public CBaseFilter, public IFileSourceFilter
{
    CCritSec lock;
    std::wstring path;

  public:
    FileSource(const wchar_t* file) : CBaseFilter(NAME("DV probe test source"), nullptr, &lock, CLSID_NULL), path(file)
    {
    }

    DECLARE_IUNKNOWN
    STDMETHODIMP NonDelegatingQueryInterface(REFIID id, void** out) override
    {
        if (id == IID_IFileSourceFilter)
            return GetInterface(static_cast<IFileSourceFilter*>(this), out);
        return CBaseFilter::NonDelegatingQueryInterface(id, out);
    }

    int GetPinCount() override
    {
        return 0;
    }

    CBasePin* GetPin(int) override
    {
        return nullptr;
    }

    STDMETHODIMP Load(LPCOLESTR, const AM_MEDIA_TYPE*) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP GetCurFile(LPOLESTR* out, AM_MEDIA_TYPE* type) override
    {
        if (!out)
            return E_POINTER;
        *out = static_cast<LPOLESTR>(CoTaskMemAlloc((path.size() + 1) * sizeof(wchar_t)));
        if (!*out)
            return E_OUTOFMEMORY;
        wcscpy_s(*out, path.size() + 1, path.c_str());
        if (type)
            *type = {};
        return S_OK;
    }
};

static ComPtr<IFilterGraph> Graph(const std::filesystem::path& path)
{
    ComPtr<IFilterGraph> graph;
    Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), "test graph");
    ComPtr<IBaseFilter> source;
    auto file = new FileSource(path.c_str());
    Check(file->QueryInterface(IID_PPV_ARGS(&source)), "source interface");
    Check(graph->AddFilter(source.Get(), L"Source"), "add file source");
    return graph;
}

// 10-bit pictures make the processor read the source file.
static void Probe(const std::filesystem::path& path, bool profile7 = false)
{
    const auto graph = Graph(path);
    MediaFormat format;
    format.frame.width = profile7 ? 3840 : 1280;
    format.frame.height = profile7 ? 2160 : 720;
    format.frame.format = PixelFormat::P010;
    DvProcessor processor;
    Check(processor.CheckFormat(graph.Get(), nullptr, format), "optional probe must not reject decoded input");
    if (processor.Active() != profile7 || processor.ShouldProcess() != profile7)
        throw std::runtime_error("Incorrect DV mode after probe");
    // Repeated negotiation must use the cached result rather than retrying a script.
    Check(processor.CheckFormat(graph.Get(), nullptr, format), "cached format check");
    printf("PASS source probe: %ls, profile7=%d\n", path.filename().c_str(), profile7);
}

// 8-bit SDR pictures carry neither Dolby Vision nor HDR: the source file stays
// untouched and the mode stays unknown until a deeper picture arrives.
static void ProbeEightBit(const std::filesystem::path& path)
{
    const auto graph = Graph(path);
    MediaFormat format;
    format.frame.width = 1280;
    format.frame.height = 720;
    format.frame.format = PixelFormat::BGR24;
    DvProcessor processor;
    Check(processor.CheckFormat(graph.Get(), nullptr, format), "8-bit format check");
    if (processor.Active() || !processor.ShouldProcess())
        throw std::runtime_error("8-bit SDR picture decided the DV mode");
    printf("PASS 8-bit SDR leaves the source alone: %ls\n", path.filename().c_str());
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM");
        av_log_set_callback(CaptureLog);
        const auto folder = std::filesystem::absolute(L"src/build/dv-source-probe");
        std::filesystem::create_directories(folder);
        const auto script = folder / L"exclusive-input.avs";
        // A renderer must consume decoded frames from the existing source,
        // never evaluate this script as another AviSynth source.
        std::ofstream(script) << "Assert(false, \"FRANKY_PROBE_EXECUTED_SCRIPT\")\n"
                                 "BlankClip(width=1280, height=720, fps=60, pixel_type=\"RGB24\")\n";
        ProbeEightBit(script);
        Probe(script);
        const auto unsupported = folder / L"player-supported-source.bin";
        std::ofstream(unsupported) << "Source handled by the player, not a DV container";
        Probe(unsupported);
        if (argc > 1)
            Probe(argv[1]);
        if (argc > 2)
            Probe(argv[2], true);
        if (scriptExecuted)
            throw std::runtime_error("DV probe executed the AviSynth source script");
        av_log_set_callback(av_log_default_callback);
        CoUninitialize();
        return 0;
    }
    catch (const std::exception& error)
    {
        puts(error.what());
        return 1;
    }
}
