#include "Settings/ContentProfiles.h"
#include <cstdio>

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

int wmain()
{
    try
    {
        using namespace ContentProfiles;
        const auto anime = Parse(L"Anime",
                                 "[Profile]\r\nfolders=\"D:\\Anime; E:/Serien/Anime/\"\r\n"
                                 "[Renderer]\r\nsmoothMotion=1\r\nsharpenStrength=0.25\r\n"
                                 "hdrMode=1\r\noutputFormat=3\r\n");
        Require(anime.folders.size() == 2, "Folders were not split");
        Require(anime.overrides.size() == 2, "Display-dependent settings were kept");
        Require(!anime.overrides.count({"Renderer", "hdrMode"}), "HDR mode is a display setting");

        const auto fourK =
            Parse(L"Anime 4K", "[Profile]\r\nfolders=\"d:\\anime\\4k\"\r\n[Renderer]\r\nsmoothMotion=0\r\n");
        const std::vector<Profile> profiles{anime, fourK};
        auto match = [&](const wchar_t* file)
        {
            const auto* found = Match(profiles, file);
            return found ? found->name : std::wstring();
        };
        Require(match(L"D:\\Anime\\Frieren\\01.mkv") == L"Anime", "Subfolder did not match");
        Require(match(L"d:\\ANIME\\x.mkv") == L"Anime", "Case changed the match");
        Require(match(L"D:\\Anime\\4K\\Frieren\\01.mkv") == L"Anime 4K", "Closest folder did not win");
        Require(match(L"E:\\Serien\\Anime\\x.mkv") == L"Anime", "Second folder or slashes did not match");
        Require(match(L"D:\\Anime2\\x.mkv").empty(), "Folder prefix matched a sibling folder");
        Require(match(L"D:\\Filme\\x.mkv").empty(), "Unrelated folder matched");
        Require(match(L"").empty(), "Stream without file matched");

        PlaceboSettings base;
        base.smoothMotion = FALSE;
        base.sharpenStrength = .5f;
        base.hdrMode = 2;
        base.saturation = 1.25f;
        const auto applied = Apply(base, anime);
        Require(applied.smoothMotion == TRUE && applied.sharpenStrength == .25f, "Overrides were not applied");
        Require(applied.hdrMode == 2 && applied.saturation == 1.25f, "Other settings changed");

        const auto broken = Parse(L"Broken", "[Profile]\r\nfolders=\"D:\\X\"\r\n[Renderer]\r\nsharpenStrength=9\r\n");
        Require(Apply(base, broken).sharpenStrength == .5f, "Out-of-range profile was applied");

        // Display mode settings apply only to the display they were set for.
        const std::wstring tvId(64, L'a'), monitorId(64, L'b');
        const auto modes = Parse(L"Modes",
                                 "[Renderer]\r\ndisplayModeSwitch=1\r\n[DisplayMode." + std::string(64, 'a') +
                                     "]\r\ndisplayModeSwitch=1\r\ndisplayModes=\"2160p23\"\r\n");
        PlaceboSettings tv = base, monitor = base;
        wcscpy_s(tv.displayProfile, tvId.c_str());
        wcscpy_s(monitor.displayProfile, monitorId.c_str());
        Require(Apply(tv, modes).displayModeSwitch && !wcscmp(Apply(tv, modes).displayModes, L"2160p23"),
                "Display mode settings were not applied to their display");
        Require(!Apply(monitor, modes).displayModeSwitch && !Apply(monitor, modes).displayModes[0],
                "Display mode settings were applied to another display");

        const auto empty = Parse(L"New", "[Renderer]\r\nsmoothMotion=1\r\n");
        Require(empty.folders.empty() && !Match({empty}, L"D:\\Anime\\x.mkv"), "Profile without folders matched");

        // Save and load keep folder spelling, overrides and the name.
        wchar_t temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        const auto directory = std::wstring(temp) + L"FrankyProfilesTest" + std::to_wstring(GetCurrentProcessId());
        Require(Save(anime, directory), "Profile was not saved");
        const auto loaded = Load(directory);
        std::filesystem::remove_all(directory);
        Require(loaded.size() == 1 && loaded[0].name == L"Anime", "Saved profile was not loaded");
        Require(loaded[0].folders == anime.folders, "Folder spelling changed");
        Require(loaded[0].overrides == anime.overrides, "Overrides changed");
        Require(loaded[0].folders[0] == L"D:\\Anime", "Folder was not kept as written");
        puts("PASS content profiles: folders, closest folder wins, sibling prefix, case, display settings ignored, "
             "overrides only, invalid profile ignored, empty profile, save and load");
        return 0;
    }
    catch (const std::exception& error)
    {
        printf("FAIL %s\n", error.what());
        return 1;
    }
}
