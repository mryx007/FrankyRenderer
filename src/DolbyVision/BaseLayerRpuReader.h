#pragma once
#include "EnhancementReader.h"
#include <deque>

// Reads the Dolby Vision RPU of single-layer Profile 5/8 files from the local
// base-layer packets, for decoders that pass the pictures without their metadata.
// Only demuxes; never decodes a picture.
class BaseLayerRpuReader
{
    struct Entry
    {
        LONGLONG time;
        AVFrame* metadata;
    };

    const std::atomic<bool>* cancelled = nullptr;
    AVFormatContext* input = nullptr;
    AVPacket* packet = nullptr;
    int video = -1, lengthSize = 0;
    LONGLONG origin = 0, lastRequested = -1, newest = LLONG_MIN;
    bool ended = false, positioned = false;
    std::deque<Entry> queue;
    DoviMetadataParser parser;

    void Clear()
    {
        for (auto& entry : queue)
            av_frame_free(&entry.metadata);
        queue.clear();
    }

    void Seek(LONGLONG target)
    {
        const auto stream = input->streams[video];
        const auto pts =
            av_rescale_q((std::max)(0LL, target - 20000000LL) + origin, AVRational{1, 10000000}, stream->time_base);
        avcheck(av_seek_frame(input, video, pts, AVSEEK_FLAG_BACKWARD), "seek base-layer RPU");
        Clear();
        parser.Reset();
        av_packet_unref(packet);
        ended = false;
        positioned = true;
        newest = LLONG_MIN;
    }

    // Reads up to the next video packet and queues its RPU; false at the end.
    bool ReadOne()
    {
        for (;;)
        {
            check(!cancelled || !cancelled->load(), "RPU read cancelled");
            if (ended)
                return false;
            const int result = av_read_frame(input, packet);
            if (result == AVERROR_EOF)
            {
                ended = true;
                return false;
            }
            avcheck(result, "read base-layer packet");
            const bool picture = packet->stream_index == video && packet->pts != AV_NOPTS_VALUE;
            if (picture)
            {
                const auto time =
                    av_rescale_q(packet->pts, input->streams[video]->time_base, AVRational{1, 10000000}) - origin;
                newest = (std::max)(newest, time);
                const auto rpu = EnhancementReader::Rpu(packet->data, size_t(packet->size), lengthSize);
                AVFrame* metadata = nullptr;
                try
                {
                    if (!rpu.empty())
                        metadata = parser.ParseSingleLayer(rpu.data(), rpu.size());
                }
                catch (const std::bad_alloc&)
                {
                    throw;
                }
                catch (const std::exception&)
                {
                    // An unsupported RPU leaves only this picture on HDR10 fallback.
                }
                if (metadata)
                {
                    // RPUs arrive in decode order; keep them sorted by presentation time.
                    auto at = std::upper_bound(queue.begin(),
                                               queue.end(),
                                               time,
                                               [](LONGLONG value, const Entry& entry) { return value < entry.time; });
                    queue.insert(at, {time, metadata});
                    while (queue.size() > 256)
                    {
                        av_frame_free(&queue.front().metadata);
                        queue.pop_front();
                    }
                }
            }
            av_packet_unref(packet);
            if (picture)
                return true;
        }
    }

  public:
    BaseLayerRpuReader(const char* path, const std::atomic<bool>* cancel) : cancelled(cancel)
    {
        try
        {
            // Passive container reading only, as for the source probe: never run
            // script or capture demuxers.
            AVDictionary* options = nullptr;
            avcheck(av_dict_set(&options, "format_whitelist", "matroska,webm,mov,mpegts", 0), "RPU reader options");
            input = avformat_alloc_context();
            check(input != nullptr, "RPU reader format context");
            if (cancelled)
                input->interrupt_callback = {[](void* p) -> int
                                             { return static_cast<const std::atomic<bool>*>(p)->load(); },
                                             const_cast<std::atomic<bool>*>(cancelled)};
            const int opened = avformat_open_input(&input, path, nullptr, &options);
            av_dict_free(&options);
            avcheck(opened, "open local base-layer file");
            if (strstr(input->iformat->name, "mpegts"))
                avcheck(avformat_find_stream_info(input, nullptr), "base-layer stream information");
            for (unsigned i = 0; i < input->nb_streams; ++i)
            {
                const auto parameters = input->streams[i]->codecpar;
                const auto config = av_packet_side_data_get(
                    parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
                if (video < 0 && parameters->codec_id == AV_CODEC_ID_HEVC && config &&
                    config->size >= sizeof(AVDOVIDecoderConfigurationRecord))
                {
                    const auto record = reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(config->data);
                    if ((record->dv_profile == 5 || record->dv_profile == 8) && record->rpu_present_flag &&
                        record->bl_present_flag && !record->el_present_flag)
                    {
                        video = int(i);
                        continue;
                    }
                }
                input->streams[i]->discard = AVDISCARD_ALL;
            }
            check(video >= 0, "single-layer Profile 5/8 stream required");
            const auto stream = input->streams[video];
            const auto parameters = stream->codecpar;
            // HVCC (MKV/MP4) uses length-prefixed NAL units; transport streams use start codes.
            if (parameters->extradata_size > 21 && parameters->extradata[0] == 1)
                lengthSize = (parameters->extradata[21] & 3) + 1;
            origin = stream->start_time == AV_NOPTS_VALUE
                         ? 0
                         : av_rescale_q(stream->start_time, stream->time_base, AVRational{1, 10000000});
            packet = av_packet_alloc();
            check(packet != nullptr, "RPU reader packet");
        }
        catch (...)
        {
            av_packet_free(&packet);
            avformat_close_input(&input);
            throw;
        }
    }

    ~BaseLayerRpuReader()
    {
        Clear();
        av_packet_free(&packet);
        avformat_close_input(&input);
    }

    // The next request repositions, e.g. after a flush or seek.
    void Invalidate()
    {
        positioned = false;
    }

    // Returns a metadata carrier (AV_FRAME_DATA_DOVI_METADATA) for the picture
    // presented at target (100 ns stream time), or null if the file has none.
    AVFrame* ForTime(LONGLONG target)
    {
        check(target >= 0, "nonnegative video time");
        if (!positioned || target < lastRequested - 10000 || target - lastRequested > 20000000)
            Seek(target);
        lastRequested = target;
        for (int reads = 0;;)
        {
            while (!queue.empty() && queue.front().time < target - 10000)
            {
                av_frame_free(&queue.front().metadata);
                queue.pop_front();
            }
            if (!queue.empty() && queue.front().time <= target + 10000)
            {
                AVFrame* result = av_frame_alloc();
                check(result != nullptr, "RPU carrier");
                const int copied = av_frame_copy_props(result, queue.front().metadata);
                if (copied < 0)
                {
                    av_frame_free(&result);
                    avcheck(copied, "retain RPU metadata");
                }
                return result;
            }
            // Later pictures can precede this one in decode order: read half a
            // second ahead before concluding that this picture has no RPU.
            if (newest > target + 5000000 || ++reads > 512 || !ReadOne())
                return nullptr;
        }
    }
};
