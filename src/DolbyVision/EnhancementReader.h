#pragma once
#include "MetadataParser.h"
#include <atomic>

// Reads local enhancement pictures or MEL metadata; never decodes the base layer.
class EnhancementReader
{
    friend class BaseLayerRpuReader; // Shares the NAL/RPU extraction below.
    const std::atomic<bool>* cancelled = nullptr;

    void CheckCancelled() const
    {
        check(!cancelled || !cancelled->load(), "enhancement read cancelled");
    }

    AVFormatContext *input = nullptr, *description = nullptr;
    AVPacket *packet = nullptr, *split = nullptr;
    Layer enhancement;
    int video = -1, lengthSize = 0;
    bool ended = false, needKeyframe = false, positioned = false;
    bool separateTrack = false;
    LONGLONG origin = 0, lastRequested = -1;
    DoviMetadataParser metadataParser;
    enum class Mode
    {
        Unknown,
        Metadata,
        Residual
    } mode = Mode::Unknown;

    static std::vector<uint8_t> Rpu(const uint8_t* bytes, size_t size, int lengths)
    {
        std::vector<uint8_t> result;
        auto inspect = [&](size_t begin, size_t end)
        {
            if (end >= begin + 2 && ((bytes[begin] >> 1) & 63) == 62)
            {
                check(result.empty(), "one RPU per enhancement access unit");
                result.assign(bytes + begin, bytes + end);
            }
        };
        if (lengths)
        {
            size_t at = 0;
            while (at < size)
            {
                check(at + lengths <= size, "RPU NAL length");
                uint32_t count = 0;
                for (int i = 0; i < lengths; ++i)
                    count = (count << 8) | bytes[at++];
                check(count >= 2 && count <= size - at, "RPU NAL bounds");
                inspect(at, at + count);
                at += count;
            }
        }
        else
        {
            size_t begin = size;
            for (size_t i = 0; i + 2 < size; ++i)
                if (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1)
                {
                    if (begin < size)
                        inspect(begin, i > 0 && bytes[i - 1] == 0 ? i - 1 : i);
                    begin = i + 3;
                    i += 2;
                }
            if (begin < size)
                inspect(begin, size);
        }
        return result;
    }

    void Push(AVPacket* value, int lengths)
    {
        if (mode != Mode::Residual)
        {
            auto rpu = Rpu(value->data, value->size, lengths);
            check(!rpu.empty(), "enhancement RPU required");
            auto metadata = metadataParser.ParseMel(rpu.data(), rpu.size());
            if (metadata)
            {
                metadata->pts = metadata->best_effort_timestamp = value->pts;
                check(enhancement.frames.size() < 128, "bounded MEL presentation queue");
                auto at =
                    std::upper_bound(enhancement.frames.begin(),
                                     enhancement.frames.end(),
                                     value->pts,
                                     [](int64_t pts, AVFrame* frame) { return pts < frame->best_effort_timestamp; });
                enhancement.frames.insert(at, metadata);
                mode = Mode::Metadata;
                return;
            }
            // A later residual-bearing picture requires reference pictures too.
            // Re-read from the preceding keyframe instead of starting at a P/B picture.
            bool restart = mode == Mode::Metadata;
            mode = Mode::Residual;
            if (restart)
            {
                Seek(lastRequested);
                return;
            }
        }
        if (!enhancement.decoder)
            enhancement.init(enhancement.stream, device);
        enhancement.push(value);
    }

    ID3D11Device* device = nullptr; // Owned by the caller's Render for this reader's lifetime.

    static void Append(std::vector<uint8_t>& data, const uint8_t* bytes, size_t size)
    {
        data.insert(data.end(), {0, 0, 0, 1});
        data.insert(data.end(), bytes, bytes + size);
    }

    LONGLONG Time(AVFrame* frame) const
    {
        check(frame->best_effort_timestamp != AV_NOPTS_VALUE, "EL frame PTS required");
        return av_rescale_q(frame->best_effort_timestamp, enhancement.stream->time_base, AVRational{1, 10000000}) -
               origin;
    }

    void Seek(LONGLONG target)
    {
        auto pts = av_rescale_q(
            (std::max)(0LL, target - 20000000LL) + origin, AVRational{1, 10000000}, input->streams[video]->time_base);
        avcheck(av_seek_frame(input, video, pts, AVSEEK_FLAG_BACKWARD), "seek enhancement only");
        for (auto frame : enhancement.frames)
            av_frame_free(&frame);
        enhancement.frames.clear();
        if (enhancement.decoder)
            avcodec_flush_buffers(enhancement.decoder);
        metadataParser.Reset();
        av_packet_unref(packet);
        av_packet_unref(split);
        ended = false;
        needKeyframe = true;
        positioned = true;
    }

    bool Read(bool advance = false)
    {
        while (advance || enhancement.frames.empty())
        {
            CheckCancelled();
            advance = false;
            if (ended)
                return false;
            int result = av_read_frame(input, packet);
            if (result == AVERROR_EOF)
            {
                if (enhancement.decoder)
                    enhancement.push(nullptr);
                ended = true;
                continue;
            }
            avcheck(result, "read EL packet source");
            if (packet->stream_index == video && (!needKeyframe || (packet->flags & AV_PKT_FLAG_KEY)))
            {
                needKeyframe = false;
                if (separateTrack)
                {
                    auto parameters = enhancement.stream->codecpar;
                    int lengths = parameters->extradata_size > 21 && parameters->extradata[0] == 1
                                      ? (parameters->extradata[21] & 3) + 1
                                      : 0;
                    Push(packet, lengths);
                    av_packet_unref(packet);
                    continue;
                }
                std::vector<uint8_t> data;
                size_t offset = 0;
                while (offset < size_t(packet->size))
                {
                    check(offset + lengthSize <= size_t(packet->size), "EL NAL length");
                    uint32_t size = 0;
                    for (int i = 0; i < lengthSize; ++i)
                        size = (size << 8) | packet->data[offset++];
                    check(size >= 2 && size <= size_t(packet->size) - offset, "EL NAL bounds");
                    auto nal = packet->data + offset;
                    int type = (nal[0] >> 1) & 63;
                    if (type == 63)
                    {
                        check(size >= 4, "EL wrapper");
                        Append(data, nal + 2, size - 2);
                    }
                    else if (type == 62)
                        Append(data, nal, size);
                    offset += size;
                }
                check(!data.empty(), "EL payload required");
                avcheck(av_new_packet(split, static_cast<int>(data.size())), "EL packet allocation");
                memcpy(split->data, data.data(), data.size());
                split->pts = packet->pts;
                split->dts = packet->dts;
                split->duration = packet->duration;
                split->flags = packet->flags;
                Push(split, 0);
                av_packet_unref(split);
            }
            av_packet_unref(packet);
        }
        return true;
    }

  public:
    EnhancementReader(const char* path, ID3D11Device* decoderDevice, const std::atomic<bool>* cancel = nullptr)
        : cancelled(cancel), device(decoderDevice)
    {
        try
        {
            input = avformat_alloc_context();
            check(input != nullptr, "enhancement format context");
            if (cancelled)
                input->interrupt_callback = {[](void* p) -> int
                                             { return static_cast<const std::atomic<bool>*>(p)->load(); },
                                             const_cast<std::atomic<bool>*>(cancelled)};
            avcheck(avformat_open_input(&input, path, nullptr, nullptr), "open local enhancement file");
            avcheck(avformat_find_stream_info(input, nullptr), "enhancement stream information");
            video = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            check(video >= 0, "video source required");
            auto source = input->streams[video];
            auto c = source->codecpar;
            // A separate EL track already carries its own presentation timestamps.
            // Decode only that track; the player's existing decoder still supplies BL.
            for (unsigned i = 0; i < input->nb_streams; ++i)
            {
                auto candidate = input->streams[i];
                auto parameters = candidate->codecpar;
                auto config = av_packet_side_data_get(
                    parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
                if (i == unsigned(video) || parameters->codec_id != AV_CODEC_ID_HEVC || !config ||
                    config->size < sizeof(AVDOVIDecoderConfigurationRecord))
                    continue;
                auto record = reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(config->data);
                if (record->dv_profile == 7 && record->el_present_flag && record->rpu_present_flag &&
                    !record->bl_present_flag)
                {
                    origin = source->start_time == AV_NOPTS_VALUE
                                 ? 0
                                 : av_rescale_q(source->start_time, source->time_base, AVRational{1, 10000000});
                    video = int(i);
                    separateTrack = true;
                    enhancement.stream = candidate;
                    packet = av_packet_alloc();
                    split = av_packet_alloc();
                    check(packet && split, "EL packets");
                    return;
                }
            }
            auto sd = av_packet_side_data_get(c->coded_side_data, c->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
            check(c->codec_id == AV_CODEC_ID_HEVC && sd && sd->size >= sizeof(AVDOVIDecoderConfigurationRecord),
                  "Profile 7 metadata required");
            auto dv = reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(sd->data);
            check(dv->dv_profile == 7 && dv->el_present_flag && dv->bl_present_flag && dv->rpu_present_flag,
                  "Profile 7 layered file required");
            check(c->extradata_size > 21 && c->extradata[0] == 1, "HVCC source required");
            lengthSize = (c->extradata[21] & 3) + 1;
            origin = source->start_time == AV_NOPTS_VALUE
                         ? 0
                         : av_rescale_q(source->start_time, source->time_base, AVRational{1, 10000000});
            description = avformat_alloc_context();
            check(description != nullptr, "EL stream description");
            auto stream = avformat_new_stream(description, nullptr);
            check(stream != nullptr, "EL stream");
            stream->time_base = source->time_base;
            stream->r_frame_rate = source->r_frame_rate;
            stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            stream->codecpar->codec_id = AV_CODEC_ID_HEVC;
            // The enhancement layer's SPS supplies its actual dimensions.
            // Do not assume half-size UHD when initializing the HEVC decoder.
            enhancement.stream = stream;
            packet = av_packet_alloc();
            split = av_packet_alloc();
            check(packet && split, "EL packets");
        }
        catch (...)
        {
            av_packet_free(&packet);
            av_packet_free(&split);
            avformat_close_input(&input);
            avformat_free_context(description);
            throw;
        }
    }

    ~EnhancementReader()
    {
        av_packet_free(&packet);
        av_packet_free(&split);
        avformat_close_input(&input);
        avformat_free_context(description);
    }

    AVFrame* ForTime(LONGLONG target)
    {
        CheckCancelled();
        check(target >= 0, "nonnegative video time");
        if (!positioned || target < lastRequested || target - lastRequested > 1000000)
            Seek(target);
        lastRequested = target;
        while (Read())
        {
            auto frame = enhancement.frames.front();
            auto difference = Time(frame) - target;
            if (difference < -10000)
            {
                enhancement.frames.pop_front();
                av_frame_free(&frame);
                continue;
            }
            if (llabs(difference) > 10000)
            {
                // RPU packets follow HEVC decode order. Read ahead until the
                // requested presentation timestamp is available, without decoding EL.
                if (MetadataOnly() && Read(true))
                    continue;
                throw std::runtime_error("base/EL timestamp mismatch: base=" + std::to_string(target) +
                                         " enhancement=" + std::to_string(Time(frame)) + " (100ns)");
            }
            auto result = frame->format == AV_PIX_FMT_NONE ? av_frame_alloc() : av_frame_clone(frame);
            check(result != nullptr, "retain matched enhancement");
            if (frame->format == AV_PIX_FMT_NONE)
            {
                int copied = av_frame_copy_props(result, frame);
                if (copied < 0)
                {
                    av_frame_free(&result);
                    avcheck(copied, "retain matched MEL metadata");
                }
            }
            return result;
        }
        throw std::runtime_error("no enhancement frame for decoded base picture");
    }

    bool MetadataOnly() const
    {
        return mode == Mode::Metadata && !enhancement.decoder;
    }
};
