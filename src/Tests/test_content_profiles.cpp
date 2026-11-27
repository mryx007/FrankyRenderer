#include "Settings/ContentProfiles.h"
#include "Settings/FolderLut.h"
#include <cstdio>
#include <fstream>

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

        // A profile stores only what differs from Default.
        auto edited = applied;
        edited.saturation = 1.5f;
        const auto differences = Differences(base, edited);
        Require(differences.size() == 4 && differences.at({"Renderer", "saturation"}) == "1.5",
                "Differences were not found");

        const auto broken = Parse(L"Broken", "[Profile]\r\nfolders=\"D:\\X\"\r\n[Renderer]\r\nsharpenStrength=9\r\n");
        Require(Apply(base, broken).sharpenStrength == .5f, "Out-of-range profile was applied");

        const auto empty = Parse(L"New", "[Renderer]\r\nsmoothMotion=1\r\n");
        Require(empty.folders.empty() && !Match({empty}, L"D:\\Anime\\x.mkv"), "Profile without folders matched");

        // Save and load keep folder spelling, displays, overrides and the name,
        // in the settings file. Its own registry key and file, never the user's.
        wchar_t temp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, temp);
        const auto id = std::to_wstring(GetCurrentProcessId());
        const auto root = L"Software\\FrankyProfilesTest" + id;
        const auto folder = std::wstring(temp) + L"FrankyProfilesTest" + id;
        SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root.c_str());
        SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_INI", (folder + L"\\Settings.ini").c_str());
        // A profile file of an earlier version is taken over and removed.
        std::filesystem::create_directories(folder + L"\\Profiles");
        std::ofstream(folder + L"\\Profiles\\Old.ini", std::ios::binary)
            << "[Profile]\r\nfolders=\"F:\\Old\"\r\nmonitors=\"\"\r\n[Renderer]\r\nsmoothMotion=1\r\n";
        Require(Save(fourK) && Save(anime), "Profile was not saved");
        auto loaded = Load();
        Require(loaded.size() == 3 && loaded[0].name == L"Anime" && loaded[1].name == L"Anime 4K" &&
                    loaded[2].name == L"Old",
                "Saved profiles were not loaded in order");
        Require(loaded[0].folders == anime.folders && loaded[1].monitors == fourK.monitors, "Lists changed");
        Require(loaded[0].overrides == anime.overrides, "Overrides changed");
        Require(loaded[0].folders[0] == L"D:\\Anime", "Folder was not kept as written");
        Require(loaded[2].folders == std::vector<std::wstring>{L"F:\\Old"} && loaded[2].overrides.size() == 1 &&
                    !std::filesystem::exists(folder + L"\\Profiles"),
                "An earlier profile file was not taken over");
        SettingsStore::Flush();
        std::ifstream file(folder + L"\\Settings.ini", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        Require(text.find("[Settings\\Profiles\\Anime]") != text.npos &&
                    text.find("Renderer.smoothMotion=1") != text.npos &&
                    text.find("Folders=\"D:\\\\Anime;") != text.npos,
                "Profiles are not readable in the settings file");
        // A cancelled menu puts the profiles back as they were.
        const auto opening = Capture();
        Require(Remove(L"Old") && Load().size() == 2, "Profile was not removed");
        Restore(opening);
        Require(Load().size() == 3 && Find(Load(), L"Old"), "Profiles were not put back");
        Restore({});
        Require(Load().empty(), "Profiles were not cleared");
        SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_INI", nullptr);
        RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
        std::filesystem::remove_all(folder);
        puts("PASS content profiles: folders, closest folder wins, sibling prefix, case, displays, folder owner, "
             "menu preview, differences, reset to Default, invalid profile ignored, empty profile, save and load in "
             "Settings.ini, earlier files taken over, put back after Cancel");

        // LUT from Video Folder: the video's name wins, else the only .cube file.
        {
            const auto folder = std::wstring(temp) + L"FrankyFolderLutTest" + id;
            std::filesystem::create_directories(folder);
            const auto touch = [&](const wchar_t* name) { std::ofstream(folder + L"\\" + name) << "LUT_3D_SIZE 2\n"; };
            const auto video = folder + L"\\Film.mkv";
            touch(L"Film.mkv");
            Require(FolderLut::Find(L"").result == FolderLut::Result::NoPath, "A stream had a folder");
            Require(FolderLut::Find(video).result == FolderLut::Result::None, "A folder without LUT found one");
            touch(L"Kino.CUBE");
            auto found = FolderLut::Find(video);
            Require(found.result == FolderLut::Result::Only && found.path == folder + L"\\Kino.CUBE",
                    "The folder's only LUT was not used");
            touch(L"Other.cube");
            Require(FolderLut::Find(video).result == FolderLut::Result::Several && FolderLut::Find(video).path.empty(),
                    "One of several LUTs was chosen at random");
            touch(L"Film.cube");
            found = FolderLut::Find(video);
            Require(found.result == FolderLut::Result::Named && found.path == folder + L"\\Film.cube",
                    "The LUT named like the video did not win");
            Require(FolderLut::Find(folder + L"\\Missing\\x.mkv").result == FolderLut::Result::None,
                    "A missing folder failed");
            std::filesystem::remove_all(folder);
            // The switch is a setting a profile can change.
            const auto profile = Parse(L"Grading", "[Profile]\r\nfolders=\r\n[Renderer]\r\nlutFromFolder=1\r\n");
            Require(Apply(PlaceboSettings{}, profile).lutFromFolder == TRUE, "A profile could not switch it on");
            PlaceboSettings invalid;
            invalid.lutFromFolder = 2;
            Require(FAILED(ValidateSettings(invalid)), "An invalid switch value was accepted");
            puts("PASS LUT from Video Folder: named LUT wins, only LUT, several choose none, no LUT, no path, "
                 "missing folder, profile override, validation");
        }
        {
            // A new profile starts with all built-in settings, as after a fresh install.
            PlaceboSettings main;
            main.brightness = .5f;
            main.smoothMotion = !PlaceboSettings{}.smoothMotion;
            main.outputLevels = PlaceboSettings{}.outputLevels ? 0 : 1;
            main.hdrNits = 1000;
            Profile fresh;
            fresh.overrides = Neutral(main);
            const auto shown = Apply(main, fresh);
            Require(shown.brightness == PlaceboSettings{}.brightness &&
                        shown.smoothMotion == PlaceboSettings{}.smoothMotion,
                    "A new profile did not start with built-in video settings");
            Require(shown.outputLevels == PlaceboSettings{}.outputLevels && shown.hdrNits == PlaceboSettings{}.hdrNits,
                    "A new profile took display settings from Main");
            main.brightness = 0;
            Require(Apply(main, fresh).brightness == PlaceboSettings{}.brightness,
                    "Resetting Main changed the new profile");
            Require(Neutral(PlaceboSettings{}).empty(), "Built-in settings need no profile values");
            puts("PASS new profile: all built-in settings, independent of Main's changes");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        printf("FAIL %s\n", error.what());
        return 1;
    }
}
