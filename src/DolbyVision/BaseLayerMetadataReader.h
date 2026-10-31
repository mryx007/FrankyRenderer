#pragma once
#include "MetadataParser.h"
#include "Processor.h"
#include <deque>
extern "C"
{
#include <libavutil/hdr_dynamic_metadata.h>
}

// Reads per-picture HDR metadata from the local base-layer packets, for decoders
// that pass the pictures without it: the Dolby Vision RPU of single-layer
// Profile 5/8 files, or HDR10+ (SMPTE ST 2094-40) SEI. Only demuxes; never
// decodes a picture.
class BaseLayerMetadataReader
{
  public:
    enum class Kind
    {
        DolbyVision,
        Hdr10Plus
    };

  private:
    struct Entry
    {
        LONGLONG time;
        AVFrame* metadata;
    };

    const Kind kind;
    const std::atomic<bool>* cancelled = nullptr;
    AVFormatContext* input = nullptr;
    AVPacket* packet = nullptr;
    int video = -1, lengthSize = 0, scanned = 0;
    LONGLONG origin = 0, lastRequested = -1, newest = LLONG_MIN;
    bool ended = false, positioned = false, seen = false, absent = false;
    std::deque<Entry> queue;
    DoviMetadataParser parser;
    StaticHdr staticHdr;

    // Length-prefixed (HVCC) or start-code (Annex B) NAL units of one packet.
    template <class F> static void ForEachNal(const uint8_t* bytes, size_t size, int lengths, F&& visit)
    {
        if (lengths)
        {
            size_t at = 0;
            while (at + lengths <= size)
            {
                uint32_t count = 0;
                for (int i = 0; i < lengths; ++i)
                    count = (count << 8) | bytes[at++];
                if (count < 2 || count > size - at)
                    return;
                visit(bytes + at, size_t(count));
                at += count;
            }
            return;
        }
        size_t begin = size;
        for (size_t i = 0; i + 2 < size; ++i)
            if (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1)
            {
                if (begin < size)
                {
                    const size_t end = i > 0 && bytes[i - 1] == 0 ? i - 1 : i;
                    if (end >= begin + 2)
                        visit(bytes + begin, end - begin);
                }
                begin = i + 3;
                i += 2;
            }
        if (begin + 2 <= size)
            visit(bytes + begin, size - begin);
    }

    // Returns an AV_FRAME_DATA_DYNAMIC_HDR_PLUS carrier from a prefix SEI NAL unit,
    // and keeps the first static mastering display / content light level SEI.
    AVFrame* ParseSei(const uint8_t* nal, size_t size)
    {
        std::vector<uint8_t> rbsp;
        rbsp.reserve(size);
        int zeros = 0;
        for (size_t i = 2; i < size; ++i)
        {
            const uint8_t value = nal[i];
            if (zeros >= 2 && value == 3)
            {
                zeros = 0;
                continue;
            }
            zeros = value == 0 ? zeros + 1 : 0;
            rbsp.push_back(value);
        }
        size_t at = 0;
        while (at + 2 <= rbsp.size() && rbsp[at] != 0x80)
        {
            unsigned type = 0, length = 0;
            while (at < rbsp.size() && rbsp[at] == 0xff)
                type += rbsp[at++];
            if (at >= rbsp.size())
                return nullptr;
            type += rbsp[at++];
            while (at < rbsp.size() && rbsp[at] == 0xff)
                length += rbsp[at++];
            if (at >= rbsp.size())
                return nullptr;
            length += rbsp[at++];
            if (length > rbsp.size() - at)
                return nullptr;
            const uint8_t* payload = rbsp.data() + at;
            const auto u16 = [&](size_t i) { return unsigned(payload[i] << 8 | payload[i + 1]); };
            const auto u32 = [&](size_t i) { return uint32_t(u16(i)) << 16 | u16(i + 2); };
            // mastering_display_colour_volume: G, B, R and white in 0.00002, luminance in 0.0001 nits.
            if (type == 137 && length >= 24 && !staticHdr.mastering)
            {
                const double maximum = u32(16) * 0.0001, minimum = u32(20) * 0.0001;
                if (maximum > 0 && maximum <= 10000 && minimum < maximum)
                {
                    staticHdr.mastering = true;
                    staticHdr.hdr.max_luma = float(maximum);
                    staticHdr.hdr.min_luma = float(minimum);
                    const auto xy = [&](size_t i) { return pl_cie_xy{u16(i) * 0.00002f, u16(i + 2) * 0.00002f}; };
                    staticHdr.hdr.prim.green = xy(0);
                    staticHdr.hdr.prim.blue = xy(4);
                    staticHdr.hdr.prim.red = xy(8);
                    staticHdr.hdr.prim.white = xy(12);
                }
            }
            // content_light_level_info: MaxCLL, MaxFALL in nits.
            if (type == 144 && length >= 4 && !staticHdr.light && u16(0) > 0 && u16(2) <= u16(0))
            {
                staticHdr.light = true;
                staticHdr.hdr.max_cll = float(u16(0));
                staticHdr.hdr.max_fall = float(u16(2));
            }
            // user_data_registered_itu_t_t35: USA, Samsung, HDR10+ application 4.
            if (type == 4 && length > 6 && payload[0] == 0xb5 && payload[1] == 0x00 && payload[2] == 0x3c &&
                payload[3] == 0x00 && payload[4] == 0x01 && payload[5] == 0x04)
            {
                std::unique_ptr<AVFrame, void (*)(AVFrame*)> carrier(av_frame_alloc(),
                                                                     [](AVFrame* f) { av_frame_free(&f); });
                check(carrier != nullptr, "HDR10+ carrier");
                auto metadata = av_dynamic_hdr_plus_create_side_data(carrier.get());
                check(metadata != nullptr, "HDR10+ metadata");
                if (av_dynamic_hdr_plus_from_t35(metadata, payload + 6, length - 6) < 0)
                    return nullptr;
                // The parser starts after the country code; record it as a decoder would.
                metadata->itu_t_t35_country_code = 0xb5;
                return carrier.release();
            }
            at += length;
        }
        return nullptr;
    }

    AVFrame* Extract(const uint8_t* bytes, size_t size)
    {
        AVFrame* result = nullptr;
        ForEachNal(bytes,
                   size,
                   lengthSize,
                   [&](const uint8_t* nal, size_t length)
                   {
                       if (result)
                           return;
                       const int type = (nal[0] >> 1) & 63;
                       if (kind == Kind::DolbyVision && type == 62)
                           result = parser.ParseSingleLayer(nal, length);
                       else if (kind == Kind::Hdr10Plus && type == 39)
                           result = ParseSei(nal, length);
                   });
        return result;
    }

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
        avcheck(av_seek_frame(input, video, pts, AVSEEK_FLAG_BACKWARD), "seek base-layer metadata");
        Clear();
        parser.Reset();
        av_packet_unref(packet);
        ended = false;
        positioned = true;
        newest = LLONG_MIN;
    }

    // Reads up to the next video packet and queues its metadata; false at the end.
    bool ReadOne()
    {
        for (;;)
        {
            check(!cancelled || !cancelled->load(), "metadata read cancelled");
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
                AVFrame* metadata = nullptr;
                try
                {
                    metadata = Extract(packet->data, size_t(packet->size));
                }
                catch (const std::bad_alloc&)
                {
                    throw;
                }
                catch (const std::exception&)
                {
                    // Unsupported metadata leaves only this picture on its static fallback.
                }
                seen |= metadata != nullptr;
                // Most HDR10 files carry no HDR10+; stop reading them after a short scan.
                if (!seen && ++scanned >= 48)
                    absent = true;
                if (metadata)
                {
                    // Metadata arrives in decode order; keep it sorted by presentation time.
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
    BaseLayerMetadataReader(const char* path, Kind what, const std::atomic<bool>* cancel)
        : kind(what), cancelled(cancel)
    {
        try
        {
            // Passive container reading only, as for the source probe: never run
            // script or capture demuxers.
            AVDictionary* options = nullptr;
            avcheck(av_dict_set(&options, "format_whitelist", "matroska,webm,mov,mpegts", 0),
                    "metadata reader options");
            input = avformat_alloc_context();
            check(input != nullptr, "metadata reader format context");
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
                const auto stream = input->streams[i];
                const auto parameters = stream->codecpar;
                bool selected = video < 0 && parameters->codec_id == AV_CODEC_ID_HEVC &&
                                !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC);
                if (selected && kind == Kind::DolbyVision)
                {
                    const auto config = av_packet_side_data_get(
                        parameters->coded_side_data, parameters->nb_coded_side_data, AV_PKT_DATA_DOVI_CONF);
                    const auto record = config && config->size >= sizeof(AVDOVIDecoderConfigurationRecord)
                                            ? reinterpret_cast<const AVDOVIDecoderConfigurationRecord*>(config->data)
                                            : nullptr;
                    selected = record && (record->dv_profile == 5 || record->dv_profile == 8) &&
                               record->rpu_present_flag && record->bl_present_flag && !record->el_present_flag;
                }
                if (selected)
                    video = int(i);
                else
                    stream->discard = AVDISCARD_ALL;
            }
            check(video >= 0,
                  kind == Kind::DolbyVision ? "single-layer Profile 5/8 stream required" : "HEVC stream required");
            const auto stream = input->streams[video];
            const auto parameters = stream->codecpar;
            // HVCC (MKV/MP4) uses length-prefixed NAL units; transport streams use start codes.
            if (parameters->extradata_size > 21 && parameters->extradata[0] == 1)
                lengthSize = (parameters->extradata[21] & 3) + 1;
            origin = stream->start_time == AV_NOPTS_VALUE
                         ? 0
                         : av_rescale_q(stream->start_time, stream->time_base, AVRational{1, 10000000});
            packet = av_packet_alloc();
            check(packet != nullptr, "metadata reader packet");
        }
        catch (...)
        {
            av_packet_free(&packet);
            avformat_close_input(&input);
            throw;
        }
    }

    ~BaseLayerMetadataReader()
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

    // Static HDR10 values found in the stream SEI so far (HDR10+ reader only).
    const StaticHdr& StreamStaticHdr() const
    {
        return staticHdr;
    }

    // True once the stream has shown no metadata of this kind at its start.
    bool Absent() const
    {
        return absent;
    }

    // Returns a metadata carrier (DOVI or HDR10+ frame side data) for the picture
    // presented at target (100 ns stream time), or null if the file has none.
    AVFrame* ForTime(LONGLONG target)
    {
        check(target >= 0, "nonnegative video time");
        if (absent)
            return nullptr;
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
                check(result != nullptr, "metadata carrier");
                const int copied = av_frame_copy_props(result, queue.front().metadata);
                if (copied < 0)
                {
                    av_frame_free(&result);
                    avcheck(copied, "retain picture metadata");
                }
                return result;
            }
            // Later pictures can precede this one in decode order: read half a
            // second ahead before concluding that this picture has no metadata.
            if (absent || newest > target + 5000000 || ++reads > 512 || !ReadOne())
                return nullptr;
        }
    }
};
