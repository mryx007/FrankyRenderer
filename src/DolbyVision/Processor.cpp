#include <streams.h>
#include <atomic>
#include <mutex>
#include "Processor.h"
#include "Runtime.h"
#include "Transport.h"
#include "EnhancementReader.h"
#include "../DirectShow/Include/ID3DVideoMemoryConfiguration.h"
using Microsoft::WRL::ComPtr;
#include "Sample.h"

static void LogDv(const char* event, LONGLONG time = 0, HRESULT result = S_OK)
{
    wchar_t path[32768];
    if (!GetEnvironmentVariableW(L"FEL_SUPPLEMENT_LOG", path, 32768))
        return;
    FILE* file = nullptr;
    if (!_wfopen_s(&file, path, L"a") && file)
    {
        fprintf(file, "pid=%lu %s time=%.6f result=%08lx\n", GetCurrentProcessId(), event, time / 1e7, result);
        fclose(file);
    }
}

class DeviceMutexGuard
{
    HANDLE held = nullptr;

  public:
    explicit DeviceMutexGuard(HANDLE mutex)
    {
        if (mutex)
        {
            DWORD wait = WaitForSingleObject(mutex, 5000);
            check(wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED, "decoder GPU mutex");
            held = mutex;
        }
    }

    ~DeviceMutexGuard()
    {
        if (held)
            ReleaseMutex(held);
    }
};

// Follow only the connected video/stream path. During initial type negotiation
// the pin is not connected yet, so accept an unambiguous file source in the graph.
static std::wstring SourcePath(IFilterGraph* graph, IPin* upstream)
{
    std::vector<ComPtr<IBaseFilter>> queue;
    std::vector<ComPtr<IUnknown>> visited;
    auto append = [&](IPin* pin)
    {
        PIN_INFO info = {};
        if (pin && SUCCEEDED(pin->QueryPinInfo(&info)) && info.pFilter)
        {
            ComPtr<IBaseFilter> filter;
            filter.Attach(info.pFilter);
            queue.push_back(std::move(filter));
        }
    };
    auto file = [](IBaseFilter* filter)
    {
        ComPtr<IFileSourceFilter> source;
        std::wstring result;
        if (SUCCEEDED(filter->QueryInterface(IID_PPV_ARGS(&source))))
        {
            LPOLESTR path = nullptr;
            if (SUCCEEDED(source->GetCurFile(&path, nullptr)) && path)
                result = path;
            CoTaskMemFree(path);
        }
        return result;
    };
    append(upstream);
    for (size_t i = 0; i < queue.size() && i < 32; ++i)
    {
        ComPtr<IUnknown> id;
        queue[i].As(&id);
        if (std::any_of(visited.begin(), visited.end(), [&](const auto& p) { return p.Get() == id.Get(); }))
            continue;
        visited.push_back(id);
        auto path = file(queue[i].Get());
        if (!path.empty())
            return path;
        ComPtr<IEnumPins> pins;
        if (FAILED(queue[i]->EnumPins(&pins)))
            continue;
        ComPtr<IPin> pin;
        while (pins->Next(1, &pin, nullptr) == S_OK)
        {
            PIN_DIRECTION direction;
            ComPtr<IPin> peer;
            if (SUCCEEDED(pin->QueryDirection(&direction)) && direction == PINDIR_INPUT &&
                SUCCEEDED(pin->ConnectedTo(&peer)))
            {
                CMediaType type;
                if (SUCCEEDED(pin->ConnectionMediaType(&type)) &&
                    (type.majortype == MEDIATYPE_Video || type.majortype == MEDIATYPE_Stream))
                    append(peer.Get());
            }
            pin.Reset();
        }
    }
    if (upstream || !graph)
        return {};
    ComPtr<IEnumFilters> filters;
    if (FAILED(graph->EnumFilters(&filters)))
        return {};
    std::wstring found;
    ComPtr<IBaseFilter> filter;
    while (filters->Next(1, &filter, nullptr) == S_OK)
    {
        auto path = file(filter.Get());
        filter.Reset();
        if (!path.empty())
        {
            if (!found.empty() && found != path)
                return {};
            found = path;
        }
    }
    return found;
}

struct DvProcessor::State
{
    std::mutex lock;
    std::atomic<bool> cancelled{false}, layered{false};
    std::atomic<bool> modeKnown{false};
    std::unique_ptr<DvGpu> gpu;
    std::unique_ptr<EnhancementReader> reader;
    std::wstring filename;
    ComPtr<ID3D11Device> decoderDevice;
    HANDLE decoderMutex = nullptr;
    bool nativeActive = false;
    DolbyVisionSource source;
    REFERENCE_TIME segment = 0;

    ~State()
    {
        if (decoderMutex)
            CloseHandle(decoderMutex);
    }

    HRESULT SelectMode(IFilterGraph* graph, IPin* upstream)
    {
        if (modeKnown || !graph)
            return S_OK;
        try
        {
            const auto path = SourcePath(graph, upstream);
            bool profile7 = false;
            if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                int count = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string utf8(count, '\0');
                WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), count, nullptr, nullptr);
                AVFormatContext* context = nullptr;
                const int opened = avformat_open_input(&context, utf8.c_str(), nullptr, nullptr);
                std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)> guard(
                    context, [](AVFormatContext* value) { avformat_close_input(&value); });
                avcheck(opened, "probe source container");
                // MKV/MP4 expose DV configuration in their headers. Transport
                // streams need stream inspection before that configuration exists.
                if (strstr(context->iformat->name, "mpegts"))
                    avcheck(avformat_find_stream_info(context, nullptr), "probe transport stream metadata");
                for (unsigned i = 0; i < context->nb_streams; ++i)
                {
                    auto parameters = context->streams[i]->codecpar;
                    auto data = av_packet_side_data_get(
                        parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
                    if (parameters->codec_id != AV_CODEC_ID_HEVC || !data ||
                        data->size < sizeof(AVDOVIDecoderConfigurationRecord))
                        continue;
                    const auto record = reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(data->data);
                    source.present = record->rpu_present_flag != 0;
                    source.profile = record->dv_profile;
                    source.hdr10Base = record->dv_profile == 7 || record->dv_bl_signal_compatibility_id == 1;
                    profile7 |= record->dv_profile == 7 && record->el_present_flag && record->rpu_present_flag;
                }
            }
            if (path.empty())
            {
                if (upstream)
                {
                    modeKnown = true;
                    layered = false;
                }
                return S_OK;
            }
            layered = profile7;
            filename = path;
            modeKnown = true;
            LogDv(layered ? "Profile 7 processing selected" : "standard renderer input selected");
            return S_OK;
        }
        catch (const std::exception& error)
        {
            LogDv(error.what(), 0, E_FAIL);
            return E_FAIL;
        }
    }

    void Open(ID3D11Device* fallback)
    {
        if (reader)
            return;
        const auto& path = filename;
        check(layered && !path.empty(), "local Profile 7 filename required");
        int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), size, nullptr, nullptr);
        gpu = std::make_unique<DvGpu>(decoderDevice ? decoderDevice.Get() : fallback);
        reader = std::make_unique<EnhancementReader>(utf8.c_str(), gpu->d3d->device, &cancelled);
        LogDv("opened enhancement only; existing base decoder retained");
    }

    void Reset()
    {
        reader.reset();
        gpu.reset();
    }
};

DvProcessor::DvProcessor() : state(std::make_unique<State>()) {}

DvProcessor::~DvProcessor() = default;

void DvProcessor::Cancel()
{
    state->cancelled = true;
}

void DvProcessor::Resume()
{
    state->cancelled = false;
}

bool DvProcessor::Active() const
{
    return state->layered;
}

bool DvProcessor::ShouldProcess() const
{
    return !state->modeKnown || state->layered;
}

DolbyVisionSource DvProcessor::SourceInfo() const
{
    std::lock_guard guard(state->lock);
    return state->source;
}

void DvProcessor::ResetTimeline()
{
    std::lock_guard guard(state->lock);
    state->Reset();
}

void DvProcessor::Disconnect()
{
    std::lock_guard guard(state->lock);
    state->Reset();
    state->modeKnown = false;
    state->layered = false;
    state->source = {};
    state->filename.clear();
    state->decoderDevice.Reset();
    state->nativeActive = false;
    if (state->decoderMutex)
    {
        CloseHandle(state->decoderMutex);
        state->decoderMutex = nullptr;
    }
}

HRESULT DvProcessor::SetDevice(ID3D11Device* device, HANDLE mutex)
{
    HANDLE copy = nullptr;
    if (mutex &&
        !DuplicateHandle(GetCurrentProcess(), mutex, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    std::lock_guard guard(state->lock);
    state->Reset();
    if (state->decoderMutex)
        CloseHandle(state->decoderMutex);
    state->decoderMutex = copy;
    state->decoderDevice = device;
    state->nativeActive = device != nullptr;
    return S_OK;
}

void DvProcessor::RefreshSource(IFilterGraph* graph, IPin* peer)
{
    std::lock_guard guard(state->lock);
    auto path = SourcePath(graph, peer);
    if (path != state->filename)
    {
        state->Reset();
        state->modeKnown = false;
        state->layered = false;
        state->source = {};
        state->filename.clear();
    }
}

HRESULT DvProcessor::CheckFormat(IFilterGraph* graph, IPin* peer, const MediaFormat& format)
{
    RefreshSource(graph, peer);
    std::lock_guard guard(state->lock);
    auto hr = state->SelectMode(graph, peer);
    if (FAILED(hr))
        return hr;
    return state->layered && (format.frame.format != PixelFormat::P010 || format.frame.width != 3840 ||
                              format.frame.height != 2160)
               ? VFW_E_TYPE_NOT_ACCEPTED
               : S_OK;
}

HRESULT DvProcessor::Prepare(IFilterGraph* graph,
                             IPin* peer,
                             IMediaSample* input,
                             const MediaFormat& format,
                             REFERENCE_TIME segment,
                             ID3D11Device* fallback,
                             ComPtr<IMediaSample>& result)
{
    result.Reset();
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (s.cancelled)
        return S_OK;
    auto hr = s.SelectMode(graph, peer);
    if (FAILED(hr))
        return hr;
    if (!s.layered)
        return S_FALSE;
    if (format.frame.format != PixelFormat::P010 || format.frame.width != 3840 || format.frame.height != 2160)
        return VFW_E_TYPE_NOT_ACCEPTED;
    // MPC's decoder advertises a field-capable media type even for progressive
    // HEVC. Only actual sample flags determine whether this picture is interlaced.
    if (format.interlaced)
    {
        ComPtr<IMediaSample2> properties;
        AM_SAMPLE2_PROPERTIES value = {};
        if (FAILED(input->QueryInterface(IID_PPV_ARGS(&properties))) ||
            FAILED(properties->GetProperties(sizeof(value), reinterpret_cast<BYTE*>(&value))) ||
            !(value.dwTypeSpecificFlags & AM_VIDEO_FLAG_WEAVE))
        {
            LogDv("P7 sample is not marked progressive", 0, VFW_E_TYPE_NOT_ACCEPTED);
            return VFW_E_TYPE_NOT_ACCEPTED;
        }
    }
    try
    {
        auto& reader = s.reader;
        auto& gpu = s.gpu;
        auto decoderMutex = s.decoderMutex;
        auto nativeActive = s.nativeActive;
        s.segment = segment;
        if (input->IsPreroll() == S_OK)
            return S_OK;
        REFERENCE_TIME start = 0, stop = 0;
        check(SUCCEEDED(input->GetTime(&start, &stop)), "decoded base timestamps required");
        LONGLONG absolute = start + segment;
        if (absolute < 0)
            return S_OK;
        auto deviceGuard = std::make_unique<DeviceMutexGuard>(decoderMutex);
        s.Open(fallback);
        std::unique_ptr<AVFrame, void (*)(AVFrame*)> el(reader->ForTime(absolute),
                                                        [](AVFrame* f) { av_frame_free(&f); });
        auto sd = av_frame_get_side_data(el.get(), AV_FRAME_DATA_DOVI_METADATA);
        check(sd != nullptr, "matched enhancement DV metadata");
        auto picture = std::make_shared<DvPicture>();
        auto& info = picture->info;
        info.version = DvTransportVersion;
        info.placeboApi = PL_API_VER;
        info.bytes = sizeof(info);
        info.pts = absolute;
        info.width[0] = 3840;
        info.height[0] = 2160;
        pl_color_repr repr = {};
        pl_map_avdovi_metadata(&info.color, &repr, &info.dovi, reinterpret_cast<const AVDOVIMetadata*>(sd->data));
        const bool residual = info.dovi.nlq_active;
        const auto header = av_dovi_get_header(reinterpret_cast<const AVDOVIMetadata*>(sd->data));
        s.source.layer = header->disable_residual_flag ? DolbyVisionSource::Layer::MEL : DolbyVisionSource::Layer::FEL;
        info.width[1] = residual ? 1920 : 0;
        info.height[1] = residual ? 1080 : 0;
        ComPtr<IMediaSampleD3D11> native;
        input->QueryInterface(IID_PPV_ARGS(&native));
        check(!nativeActive || native, "native decoder must deliver actual GPU samples");
        std::vector<uint16_t> plane;
        if (native)
        {
            ComPtr<ID3D11Texture2D> texture;
            UINT slice = 0;
            check(SUCCEEDED(native->GetD3D11Texture(0, &texture, &slice)) && texture, "native decoded texture");
            D3D11_TEXTURE2D_DESC desc = {};
            texture->GetDesc(&desc);
            ComPtr<ID3D11Device> device;
            texture->GetDevice(&device);
            check(device.Get() == gpu->d3d->device && desc.Format == DXGI_FORMAT_P010 && desc.Width >= 3840 &&
                      desc.Height >= 2160 && slice < desc.ArraySize,
                  "native base texture layout/device");
            AVFrame frame = {};
            frame.format = AV_PIX_FMT_D3D11;
            frame.width = 3840;
            frame.height = 2160;
            frame.data[0] = reinterpret_cast<uint8_t*>(texture.Get());
            frame.data[1] = reinterpret_cast<uint8_t*>(UINT_PTR(slice));
            UploadDvPlanes(&frame, gpu->splitShader.Get(), picture->planes[0]);
        }
        else
        {
            BYTE* base = nullptr;
            check(SUCCEEDED(input->GetPointer(&base)), "decoded P010 buffer");
            check(size_t(input->GetActualDataLength()) >=
                      ImageBytes(format.frame.format, format.frame.stride, format.frame.height),
                  "complete P010 decoded base buffer");
            for (int p = 0; p < 3; ++p)
            {
                unsigned w = p ? 1920 : 3840, h = p ? 1080 : 2160;
                plane.resize(size_t(w) * h);
                auto source =
                    reinterpret_cast<const uint16_t*>(base) + (p ? size_t(format.frame.stride / 2) * 2160 : 0);
                for (unsigned y = 0; y < h; ++y)
                    for (unsigned x = 0; x < w; ++x)
                        plane[size_t(y) * w + x] =
                            source[size_t(y) * (format.frame.stride / 2) + (p ? x * 2 + p - 1 : x)] >> 6;
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = w;
                desc.Height = h;
                desc.MipLevels = desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_R16_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                D3D11_SUBRESOURCE_DATA data = {plane.data(), w * 2, 0};
                check(SUCCEEDED(gpu->d3d->device->CreateTexture2D(&desc, &data, &picture->planes[0][p])),
                      "upload externally decoded base plane");
            }
        }
        if (residual)
        {
            UploadDvPlanes(el.get(), gpu->splitShader.Get(), picture->planes[1]);
        }
        pl_gpu_finish(gpu->d3d->gpu);
        deviceGuard.reset(); // Never hold the decoder mutex while the renderer schedules/presents.

        if (s.cancelled)
            return S_OK;
        result.Attach(new (std::nothrow) DvSample(input, std::move(picture)));
        if (!result)
            return E_OUTOFMEMORY;
        return S_OK;
    }
    catch (const std::exception& error)
    {
        if (s.cancelled)
            return S_OK;
        LogDv(error.what(), 0, E_FAIL);
        return E_FAIL;
    }
}
