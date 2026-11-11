#pragma once
#include <filesystem>
#include <string>
#include <windows.h>

// "LUT from Video Folder": the 3D LUT next to a video. A .cube file with the
// video's name wins (Film.mkv -> Film.cube); otherwise the folder's only .cube
// file. Several others and no name match choose none, never one at random.
namespace FolderLut
{
enum class Result
{
    NoPath,  // the player gave no file path
    None,    // no .cube file in the folder
    Several, // several .cube files, none named like the video
    Named,   // <video name>.cube
    Only     // the folder's only .cube file
};

struct Found
{
    Result result = Result::NoPath;
    std::wstring path;
};

inline bool IsCube(const std::filesystem::path& path)
{
    return !_wcsicmp(path.extension().c_str(), L".cube");
}

inline Found Find(const std::wstring& video)
{
    Found found;
    if (video.empty())
        return found;
    found.result = Result::None;
    try
    {
        const std::filesystem::path file(video);
        std::error_code error;
        auto named = file;
        named.replace_extension(L".cube");
        if (std::filesystem::is_regular_file(named, error))
        {
            found = {Result::Named, named.wstring()};
            return found;
        }
        int count = 0;
        std::wstring only;
        for (const auto& entry : std::filesystem::directory_iterator(file.parent_path(), error))
        {
            std::error_code itemError;
            if (!entry.is_regular_file(itemError) || !IsCube(entry.path()))
                continue;
            if (++count == 1)
                only = entry.path().wstring();
        }
        if (count == 1)
            found = {Result::Only, only};
        else if (count > 1)
            found.result = Result::Several;
    }
    catch (const std::exception&)
    {
        // An unusual path (stream URL, invalid characters) has no folder LUT.
        found = {};
        found.result = Result::None;
    }
    return found;
}
} // namespace FolderLut
