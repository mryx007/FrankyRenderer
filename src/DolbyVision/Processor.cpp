#include <streams.h>
#include <atomic>
#include <mutex>
#include "Processor.h"
#include "Runtime.h"
#include "Transport.h"
#include "EnhancementReader.h"
#include "BaseLayerRpuReader.h"
#include "../DirectShow/Include/ID3DVideoMemoryConfiguration.h"
#include "../DirectShow/Include/IMediaSideData.h"
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
    // Profile 5/8 metadata for decoders that deliver pictures without it.
    std::unique_ptr<BaseLayerRpuReader> rpuReader;
    bool rpuFailed = false;
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
            if (!path.empty())
            {
                filename = path;
                modeKnown = true;
                layered = false;
                source = {};
            }
            if (!path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                int count = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string utf8(count, '\0');
                WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), count, nullptr, nullptr);
                AVFormatContext* context = nullptr;
                // This is passive DV-container inspection, not another source
                // filter. In particular, never execute AviSynth/VapourSynth or
                // reopen a capture device through a script demuxer.
                AVDictionary* options = nullptr;
                const int configured = av_dict_set(&options, "format_whitelist", "matroska,webm,mov,mpegts", 0);
                if (configured < 0)
                {
                    av_dict_free(&options);
                    avcheck(configured, "configure passive source probe");
                }
                const int opened = avformat_open_input(&context, utf8.c_str(), nullptr, &options);
                av_dict_free(&options);
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
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
        catch (const std::exception& error)
        {
            LogDv(error.what(), 0, E_FAIL);
            // The existing source/decoder can supply valid frames even if
            // FFmpeg cannot inspect its input. Cache the ordinary-input mode;
            // do not retry the failed optional probe on each decoded frame.
            modeKnown = true;
            layered = false;
            source = {};
            return S_OK;
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
        // Keep the open file across seeks; only reposition on the next request.
        if (rpuReader)
            rpuReader->Invalidate();
    }

    void ForgetSource()
    {
        rpuReader.reset();
        rpuFailed = false;
    }
};

// Same layout a DirectShow decoder attaches as IID_MediaSideDataDOVIMetadataV2.
static void ToSideData(const AVDOVIMetadata* metadata, std::vector<BYTE>& out)
{
    MediaSideDataDOVIMetadata data = {};
    const auto* h = av_dovi_get_header(metadata);
    data.Header.rpu_type = h->rpu_type;
    data.Header.rpu_format = h->rpu_format;
    data.Header.vdr_rpu_profile = h->vdr_rpu_profile;
    data.Header.vdr_rpu_level = h->vdr_rpu_level;
    data.Header.chroma_resampling_explicit_filter_flag = h->chroma_resampling_explicit_filter_flag;
    data.Header.coef_data_type = h->coef_data_type;
    data.Header.coef_log2_denom = h->coef_log2_denom;
    data.Header.vdr_rpu_normalized_idc = h->vdr_rpu_normalized_idc;
    data.Header.bl_video_full_range_flag = h->bl_video_full_range_flag;
    data.Header.bl_bit_depth = h->bl_bit_depth;
    data.Header.el_bit_depth = h->el_bit_depth;
    data.Header.vdr_bit_depth = h->vdr_bit_depth;
    data.Header.spatial_resampling_filter_flag = h->spatial_resampling_filter_flag;
    data.Header.el_spatial_resampling_filter_flag = h->el_spatial_resampling_filter_flag;
    data.Header.disable_residual_flag = h->disable_residual_flag;
    const auto* m = av_dovi_get_mapping(metadata);
    data.Mapping.vdr_rpu_id = m->vdr_rpu_id;
    data.Mapping.mapping_color_space = m->mapping_color_space;
    data.Mapping.mapping_chroma_format_idc = m->mapping_chroma_format_idc;
    for (int c = 0; c < 3; ++c)
    {
        const auto& in = m->curves[c];
        auto& curve = data.Mapping.curves[c];
        curve.num_pivots = in.num_pivots;
        for (int p = 0; p < LAV_DOVI_MAX_PIECES + 1; ++p)
            curve.pivots[p] = in.pivots[p];
        for (int p = 0; p < LAV_DOVI_MAX_PIECES; ++p)
        {
            curve.mapping_idc[p] = uint8_t(in.mapping_idc[p]);
            curve.poly_order[p] = in.poly_order[p];
            curve.mmr_order[p] = in.mmr_order[p];
            curve.mmr_constant[p] = in.mmr_constant[p];
            for (int k = 0; k < 3; ++k)
                curve.poly_coef[p][k] = in.poly_coef[p][k];
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 7; ++k)
                    curve.mmr_coef[p][j][k] = in.mmr_coef[p][j][k];
        }
        data.Mapping.nlq[c].nlq_offset = m->nlq[c].nlq_offset;
        data.Mapping.nlq[c].vdr_in_max = m->nlq[c].vdr_in_max;
        data.Mapping.nlq[c].linear_deadzone_slope = m->nlq[c].linear_deadzone_slope;
        data.Mapping.nlq[c].linear_deadzone_threshold = m->nlq[c].linear_deadzone_threshold;
    }
    data.Mapping.nlq_method_idc = uint8_t(m->nlq_method_idc);
    data.Mapping.num_x_partitions = m->num_x_partitions;
    data.Mapping.num_y_partitions = m->num_y_partitions;
    const auto* color = av_dovi_get_color(metadata);
    auto& target = data.ColorMetadata;
    target.dm_metadata_id = color->dm_metadata_id;
    target.scene_refresh_flag = color->scene_refresh_flag;
    for (int i = 0; i < 9; ++i)
    {
        target.ycc_to_rgb_matrix[i] = av_q2d(color->ycc_to_rgb_matrix[i]);
        target.rgb_to_lms_matrix[i] = av_q2d(color->rgb_to_lms_matrix[i]);
    }
    for (int i = 0; i < 3; ++i)
        target.ycc_to_rgb_offset[i] = av_q2d(color->ycc_to_rgb_offset[i]);
    target.signal_eotf = color->signal_eotf;
    target.signal_eotf_param0 = color->signal_eotf_param0;
    target.signal_eotf_param1 = color->signal_eotf_param1;
    target.signal_eotf_param2 = color->signal_eotf_param2;
    target.signal_bit_depth = color->signal_bit_depth;
    target.signal_color_space = color->signal_color_space;
    target.signal_chroma_format = color->signal_chroma_format;
    target.signal_full_range_flag = color->signal_full_range_flag;
    target.source_min_pq = color->source_min_pq;
    target.source_max_pq = color->source_max_pq;
    target.source_diagonal = color->source_diagonal;
    for (int i = 0, n = 0; i < metadata->num_ext_blocks && n < LAV_DOVI_MAX_EXTENSIONS; ++i)
    {
        const auto* block = av_dovi_get_ext(metadata, i);
        if (block->level != 1)
            continue;
        data.Extensions[n].level = 1;
        data.Extensions[n].Level1.min_pq = block->l1.min_pq;
        data.Extensions[n].Level1.max_pq = block->l1.max_pq;
        data.Extensions[n].Level1.avg_pq = block->l1.avg_pq;
        ++n;
    }
    out.resize(sizeof(data));
    memcpy(out.data(), &data, sizeof(data));
}

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
    state->ForgetSource();
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
        state->ForgetSource();
    }
}

HRESULT DvProcessor::SingleLayerMetadata(
    IFilterGraph* graph, IPin* peer, REFERENCE_TIME start, REFERENCE_TIME segment, std::vector<BYTE>& out)
{
    out.clear();
    auto& s = *state;
    std::lock_guard guard(s.lock);
    if (s.cancelled)
        return S_FALSE;
    HRESULT hr = s.SelectMode(graph, peer);
    if (FAILED(hr))
        return hr;
    if (s.layered || s.rpuFailed || s.filename.empty() || !s.source.present ||
        (s.source.profile != 5 && s.source.profile != 8))
        return S_FALSE;
    try
    {
        if (!s.rpuReader)
        {
            int size = WideCharToMultiByte(CP_UTF8, 0, s.filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string utf8(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, s.filename.c_str(), -1, utf8.data(), size, nullptr, nullptr);
            s.rpuReader = std::make_unique<BaseLayerRpuReader>(utf8.c_str(), &s.cancelled);
            LogDv("opened base-layer RPU reader for decoder without Dolby Vision metadata");
        }
        const LONGLONG absolute = start + segment;
        if (absolute < 0)
            return S_FALSE;
        std::unique_ptr<AVFrame, void (*)(AVFrame*)> carrier(s.rpuReader->ForTime(absolute),
                                                             [](AVFrame* f) { av_frame_free(&f); });
        const auto sd = carrier ? av_frame_get_side_data(carrier.get(), AV_FRAME_DATA_DOVI_METADATA) : nullptr;
        if (!sd)
            return S_FALSE;
        ToSideData(reinterpret_cast<const AVDOVIMetadata*>(sd->data), out);
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (const std::exception& error)
    {
        // A cancelled read (Stop/flush) only drops the reader. Other failures
        // keep this source on its HDR10 base layer instead of retrying per frame.
        s.rpuReader.reset();
        if (!s.cancelled)
        {
            LogDv(error.what(), start, E_FAIL);
            s.rpuFailed = true;
        }
        return S_FALSE;
    }
}

HRESULT DvProcessor::CheckFormat(IFilterGraph* graph, IPin* peer, const MediaFormat& format)
{
    RefreshSource(graph, peer);
    std::lock_guard guard(state->lock);
    auto hr = state->SelectMode(graph, peer);
    if (FAILED(hr))
        return hr;
    return state->layered &&
                   (format.frame.format != PixelFormat::P010 || format.frame.width <= 0 || format.frame.height <= 0)
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
    if (format.frame.format != PixelFormat::P010 || format.frame.width <= 0 || format.frame.height <= 0)
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
        info.width[0] = format.frame.width;
        info.height[0] = format.frame.height;
        pl_color_repr repr = {};
        pl_map_avdovi_metadata(&info.color, &repr, &info.dovi, reinterpret_cast<const AVDOVIMetadata*>(sd->data));
        const bool residual = info.dovi.nlq_active;
        const auto header = av_dovi_get_header(reinterpret_cast<const AVDOVIMetadata*>(sd->data));
        s.source.layer = header->disable_residual_flag ? DolbyVisionSource::Layer::MEL : DolbyVisionSource::Layer::FEL;
        check(!residual || (el->width > 0 && el->height > 0), "decoded enhancement dimensions");
        info.width[1] = residual ? el->width : 0;
        info.height[1] = residual ? el->height : 0;
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
            check(device.Get() == gpu->d3d->device && desc.Format == DXGI_FORMAT_P010 && desc.Width >= info.width[0] &&
                      desc.Height >= info.height[0] && slice < desc.ArraySize,
                  "native base texture layout/device");
            AVFrame frame = {};
            frame.format = AV_PIX_FMT_D3D11;
            frame.width = format.frame.width;
            frame.height = format.frame.height;
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
                unsigned w = p ? (info.width[0] + 1) / 2 : info.width[0];
                unsigned h = p ? (info.height[0] + 1) / 2 : info.height[0];
                plane.resize(size_t(w) * h);
                auto source = reinterpret_cast<const uint16_t*>(base) +
                              (p ? size_t(format.frame.stride / 2) * info.height[0] : 0);
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
