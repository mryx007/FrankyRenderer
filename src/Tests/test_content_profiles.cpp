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
        const std::wstring tv(64, L'a'), monitor(64, L'b');
        const auto anime = Parse(L"Anime",
                                 "[Profile]\r\nfolders=\"D:\\Anime; E:/Serien/Anime/\"\r\n"
                                 "[Renderer]\r\nsmoothMotion=1\r\nsharpenStrength=0.25\r\nhdrMode=1\r\n");
        Require(anime.folders.size() == 2, "Folders were not split");
        Require(anime.monitors.empty(), "A profile without displays applies on all of them");
        Require(anime.overrides.size() == 3, "Profile settings were dropped");

        const auto fourK = Parse(L"Anime 4K",
                                 "[Profile]\r\nfolders=\"d:\\anime\\4k\"\r\nmonitors=\"" + std::string(64, 'a') +
                                     "\"\r\n[Renderer]\r\nsmoothMotion=0\r\n");
        Require(fourK.monitors.size() == 1 && fourK.monitors[0] == tv, "Displays were not read");
        const std::vector<Profile> profiles{anime, fourK};
        auto match = [&](const wchar_t* file, const std::wstring& display = {})
        {
            const auto* found = Match(profiles, file, display.empty() ? tv : display);
            return found ? found->name : std::wstring();
        };
        Require(match(L"D:\\Anime\\Frieren\\01.mkv") == L"Anime", "Subfolder did not match");
        Require(match(L"d:\\ANIME\\x.mkv") == L"Anime", "Case changed the match");
        Require(match(L"D:\\Anime\\4K\\Frieren\\01.mkv") == L"Anime 4K", "Closest folder did not win");
        Require(match(L"E:\\Serien\\Anime\\x.mkv") == L"Anime", "Second folder or slashes did not match");
        Require(match(L"D:\\Anime2\\x.mkv").empty(), "Folder prefix matched a sibling folder");
        Require(match(L"D:\\Filme\\x.mkv").empty(), "Unrelated folder matched");
        Require(match(L"").empty(), "Stream without file matched");
        // The folder decides; a display the profile does not allow gets Default.
        Require(match(L"D:\\Anime\\4K\\x.mkv", monitor).empty(), "Profile applied on a display it does not allow");
        Require(FolderOwner(profiles, L"d:/anime/") == &profiles[0], "Folder owner was not found");
        Require(!FolderOwner(profiles, L"D:\\Filme"), "Free folder had an owner");

        // A profile the menu previews wins over the folder; Default means none.
        Require(Resolve(profiles, L"Anime 4K", L"D:\\Filme\\x.mkv", monitor) == &profiles[1],
                "Chosen profile was not used");
        Require(!Resolve(profiles, DefaultName, L"D:\\Anime\\x.mkv", tv), "Default still used a profile");
        Require(Resolve(profiles, L"", L"D:\\Anime\\x.mkv", tv) == &profiles[0],
                "Folder did not decide without a preview");

        // The menu previews only on the video that was playing when it chose.
        PublishPlaying(L"D:\\Anime\\x.mkv", L"Anime");
        Require(PlayingProfile() == L"Anime", "Playing profile was not published");
        StartPreview(L"Anime 4K");
        Require(Preview(L"d:\\anime\\X.mkv") == L"Anime 4K", "Preview did not reach the playing video");
        Require(Preview(L"D:\\Filme\\y.mkv").empty(), "Preview reached another video");
        StopPreview();
        Require(Preview(L"D:\\Anime\\x.mkv").empty(), "Preview outlived the menu");

        PlaceboSettings base;
        base.smoothMotion = FALSE;
        base.sharpenStrength = .5f;
        base.hdrMode = 2;
        base.saturation = 1.25f;
        const auto applied = Apply(base, anime);
        Require(applied.smoothMotion == TRUE && applied.sharpenStrength == .25f && applied.hdrMode == 1,
                "Overrides were not applied");
        Require(applied.saturation == 1.25f, "Other settings changed");

        // A profile stores only what differs from Default; a reset returns to Default.
        auto edited = applied;
        edited.saturation = 1.5f;
        const auto differences = Differences(base, edited);
        Require(differences.size() == 4 && differences.at({"Renderer", "saturation"}) == "1.5",
                "Differences were not found");
        const auto before = edited;
        edited.sharpenStrength = 0; // as if a reset wrote the built-in value
        RestoreChanged(edited, before, base);
        Require(edited.sharpenStrength == .5f && edited.saturation == 1.5f, "Reset did not return to Default");

        const auto broken = Parse(L"Broken", "[Profile]\r\nfolders=\"D:\\X\"\r\n[Renderer]\r\nsharpenStrength=9\r\n");
        Require(Apply(base, broken).sharpenStrength == .5f, "Out-of-range profile was applied");

        const auto empty = Parse(L"New", "[Renderer]\r\nsmoothMotion=1\r\n");
        Require(empty.folders.empty() && !Match({empty}, L"D:\\Anime\\x.mkv"), "Profile without folders matched");

        // Save and load keep folder spelling, displays, overrides and the name.
        wchar_t temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        const auto directory = std::wstring(temp) + L"FrankyProfilesTest" + std::to_wstring(GetCurrentProcessId());
        Require(Save(fourK, directory) && Save(anime, directory), "Profile was not saved");
        const auto loaded = Load(directory);
        std::filesystem::remove_all(directory);
        Require(loaded.size() == 2 && loaded[0].name == L"Anime", "Saved profiles were not loaded in order");
        Require(loaded[0].folders == anime.folders && loaded[1].monitors == fourK.monitors, "Lists changed");
        Require(loaded[0].overrides == anime.overrides, "Overrides changed");
        Require(loaded[0].folders[0] == L"D:\\Anime", "Folder was not kept as written");
        puts("PASS content profiles: folders, closest folder wins, sibling prefix, case, displays, folder owner, "
             "menu preview, differences, reset to Default, invalid profile ignored, empty profile, save and load");
        return 0;
    }
    catch (const std::exception& error)
    {
        printf("FAIL %s\n", error.what());
        return 1;
    }
}
