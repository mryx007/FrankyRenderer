#pragma once
#include "EnhancementReader.h"
#include <windows.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

// A Blu-ray playlist (BDMV\PLAYLIST\*.mpls) lists the clips a title plays.
// Players present the title as one timeline: each play item continues where
// the previous one ended, starting from its clip's IN time.
struct BlurayClip
{
    std::wstring path;          // BDMV\STREAM\<clip>.m2ts
    LONGLONG in = 0, start = 0; // clip IN time and title position, 100 ns
    LONGLONG duration = 0;      // OUT minus IN, 100 ns
};

inline bool IsBlurayPlaylist(const std::wstring& path)
{
    return path.size() > 5 && !_wcsicmp(path.c_str() + path.size() - 5, L".mpls");
}

// Reads the play items of an .mpls playlist. Empty for anything that is not a
// readable playlist whose clips exist. Only the first angle of multi-angle
// items is used.
inline std::vector<BlurayClip> ReadBlurayPlaylist(const std::wstring& path)
{
    std::vector<BlurayClip> clips;
    if (!IsBlurayPlaylist(path))
        return clips;
    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto u16 = [&](size_t at) { return at + 2 <= data.size() ? unsigned(data[at] << 8 | data[at + 1]) : 0u; };
    const auto u32 = [&](size_t at) { return at + 4 <= data.size() ? uint32_t(u16(at)) << 16 | u16(at + 2) : 0u; };
    if (data.size() < 20 || memcmp(data.data(), "MPLS", 4))
        return clips;
    const size_t list = u32(8);
    const unsigned items = u16(list + 6);
    // The playlist sits in BDMV\PLAYLIST; its clips in BDMV\STREAM.
    const auto folder = path.substr(0, path.find_last_of(L"\\/") + 1) + L"..\\STREAM\\";
    LONGLONG position = 0;
    for (size_t at = list + 10, item = 0; item < items; ++item)
    {
        const unsigned length = u16(at);
        if (length < 20 || at + 2 + length > data.size())
            return {};
        const uint8_t* name = data.data() + at + 2;
        BlurayClip clip;
        clip.path = folder + std::wstring(name, name + 5) + L".m2ts";
        // IN and OUT count the 45 kHz clock.
        const LONGLONG in = u32(at + 2 + 12), out = u32(at + 2 + 16);
        if (out <= in || GetFileAttributesW(clip.path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return {};
        clip.in = in * 10000000 / 45000;
        clip.duration = (out - in) * 10000000 / 45000;
        clip.start = position;
        position += clip.duration;
        clips.push_back(std::move(clip));
        at += 2 + length;
    }
    return clips;
}

// Enhancement layer for a whole title: a single file, or the clip of a Blu-ray
// playlist that covers the requested title time.
class TitleEnhancementReader
{
    std::vector<BlurayClip> clips;
    ID3D11Device* device;
    const std::atomic<bool>* cancelled;
    std::unique_ptr<EnhancementReader> reader;
    size_t current = SIZE_MAX;

    static std::string Utf8(const std::wstring& text)
    {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
        return result;
    }

  public:
    TitleEnhancementReader(const std::wstring& path, ID3D11Device* decoderDevice, const std::atomic<bool>* cancel)
        : device(decoderDevice), cancelled(cancel)
    {
        clips = ReadBlurayPlaylist(path);
        if (clips.empty())
        {
            // One file: its own stream start is time zero.
            reader = std::make_unique<EnhancementReader>(Utf8(path).c_str(), device, cancelled);
            current = 0;
        }
    }

    AVFrame* ForTime(LONGLONG target)
    {
        if (clips.empty())
            return reader->ForTime(target);
        size_t index = 0;
        while (index + 1 < clips.size() && target >= clips[index + 1].start)
            ++index;
        if (index != current)
        {
            reader.reset();
            reader = std::make_unique<EnhancementReader>(
                Utf8(clips[index].path).c_str(), device, cancelled, clips[index].in);
            current = index;
        }
        return reader->ForTime(target - clips[index].start);
    }
};
