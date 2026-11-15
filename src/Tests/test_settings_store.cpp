// The settings file: taken over from the registry on the first change,
// defaults after deleting it, readable values, hand edits and fallbacks.
// Uses its own registry key and file, never the user's settings.
#include "Settings/Settings.h"
#include "Settings/SettingsStore.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

static void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static std::string ReadText(const std::wstring& path)
{
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

static void WriteText(const std::wstring& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

static bool RegistryValue(const std::wstring& path, const wchar_t* name)
{
    return RegGetValueW(HKEY_CURRENT_USER, path.c_str(), name, RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

static bool RegistryKey(const std::wstring& path)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

// The next look at the file happens at once, as in another player 100 ms later.
static void Later()
{
    Sleep(120);
}

int main()
{
    wchar_t name[128];
    swprintf_s(name, L"Software\\FrankySettingsStoreTest_%lu", GetCurrentProcessId());
    const std::wstring root = name;
    const auto folder =
        std::filesystem::temp_directory_path() / ("FrankySettingsStoreTest_" + std::to_string(GetCurrentProcessId()));
    const std::wstring ini = (folder / L"Sub" / L"FrankyRenderer.ini").wstring();
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root.c_str());
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_INI", ini.c_str());
    int result = 0;
    try
    {
        Require(SettingsStore::IniPath() == ini && SettingsStore::Folder() == (folder / L"Sub").wstring(),
                "test file path");

        // An older version's registry settings, with crash notes beside them.
        const auto display = DisplayProfiles::Hash(L"test:monitor");
        PlaceboSettings old;
        wcscpy_s(old.displayProfile, display.c_str());
        old.brightness = .125f;
        old.smoothMotion = TRUE;
        wcscpy_s(old.iccPath, L"C:\\Profile \"\xe4\x20ac\".icc");
        old.iccEnabled = TRUE;
        // Blocks stored as one binary value; old text behind the end of a name.
        strcpy_s(old.scaling.filters[0].filter, "ewa_lanczos");
        strcpy_s(old.scaling.filters[0].filter, "auto");
        old.scaling.filters[0].filter[6] = 'x';
        old.tonal.exposure[0] = .2f;
        old.userShaders.shaders[16].enabled = 1;
        old.userShaders.shaders[16].values[0] = .5f;
        old.blackBars.enabled = TRUE;
        {
            SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_INI", nullptr);
            Require(SUCCEEDED(SaveSettings(old)), "seed registry settings");
            Require(SUCCEEDED(DisplayProfiles::SetLinked(false)), "seed registry root value");
            const DWORD slot = 3;
            Require(SettingsStore::Put(root + L"\\CustomShaders", L"a=b [x].glsl", REG_DWORD, &slot, 4) ==
                        ERROR_SUCCESS,
                    "seed custom shader slot");
            const SIZE window = {1013, 740};
            Require(SettingsStore::Put(root + L"\\Appearance", L"WindowSize", REG_BINARY, &window, sizeof(window)) ==
                        ERROR_SUCCESS,
                    "seed window size");
            const DWORD v2 = 7;
            Require(SettingsStore::Put(DisplayProfiles::Key(display), L"UserShadersV2", REG_BINARY, &v2, 4) ==
                        ERROR_SUCCESS,
                    "seed replaced shader layout");
            const DWORD note = 1;
            RegSetKeyValueW(HKEY_CURRENT_USER, root.c_str(), L"PendingSession.1", REG_DWORD, &note, 4);
            RegSetKeyValueW(HKEY_CURRENT_USER, root.c_str(), L"DiagnosticLogNextStart", REG_DWORD, &note, 4);
            Require(!std::filesystem::exists(ini), "registry mode writes no file");
            SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_INI", ini.c_str());
        }

        // Before any change everything still comes from the registry.
        Later();
        auto loaded = LoadSettings(display);
        Require(loaded.brightness == .125f && loaded.smoothMotion && !wcscmp(loaded.iccPath, old.iccPath),
                "registry settings are read without a file");
        Require(SUCCEEDED(SaveSettings(loaded)), "saving unchanged settings");
        Require(SUCCEEDED(DisplayProfiles::SetLinked(false)), "writing an unchanged value");
        Require(!std::filesystem::exists(ini), "unchanged values create no file");

        // The first change creates the file with every registry setting.
        loaded.brightness = .25f;
        Require(SUCCEEDED(SaveSettings(loaded)), "first change");
        Require(std::filesystem::exists(ini), "first change creates the file and its folder");
        const auto text = ReadText(ini);
        std::string narrow;
        for (wchar_t c : display)
            narrow += char(c);
        Require(text.find("[Settings\\Displays\\" + narrow + "]") != text.npos, "display section");
        Require(text.find("Brightness=0.25") != text.npos, "floats are readable");
        Require(text.find("Scaling.upscaler.filter=\"auto\"") != text.npos &&
                    text.find("Tonal.exposure0=0.2") != text.npos && text.find("BlackBars.enabled=1") != text.npos &&
                    text.find("Shaders.16.enabled=1") != text.npos &&
                    text.find("Shaders.16.parameter0=0.5") != text.npos && text.find("Shaders.order=") != text.npos &&
                    text.find("Extras.") != text.npos && text.find("ColorMapping.") != text.npos &&
                    text.find("WindowSize.width=1013") != text.npos,
                "blocks are written field by field");
        Require(text.find("hex") == text.npos && text.find("float:") == text.npos, "no hex or type marks");
        Require(text.find("UserShadersV2") == text.npos, "replaced layouts are left out");
        Require(text.find("SmoothMotion=1") != text.npos, "numbers are readable");
        Require(text.find("LinkDisplays=0") != text.npos, "root values are taken over");
        Require(text.find("\"a=b [x].glsl\"=3") != text.npos, "special names are quoted");
        Require(text.find("Pending") == text.npos && text.find("DiagnosticLogNextStart") == text.npos,
                "crash notes stay out of the file");
        Require(text.find("ICCPath=\"C:\\\\Profile \\\"\xc3\xa4\xe2\x82\xac\\\".icc\"") != text.npos,
                "paths are quoted UTF-8");
        Require(RegistryValue(root, L"PendingSession.1") && RegistryValue(root, L"DiagnosticLogNextStart"),
                "crash notes stay in the registry");
        Require(!RegistryKey(root + L"\\Displays") && !RegistryValue(root, L"LinkDisplays"),
                "registry settings are moved away");
        Require(RegistryKey(root + L".Backup\\Displays") && RegistryValue(root + L".Backup", L"LinkDisplays"),
                "registry backup");
        loaded = LoadSettings(display);
        Require(loaded.brightness == .25f && loaded.smoothMotion && !wcscmp(loaded.iccPath, old.iccPath),
                "settings come from the file");
        Require(!strcmp(loaded.scaling.filters[0].filter, "auto") && loaded.tonal.exposure[0] == .2f &&
                    loaded.userShaders.shaders[16].enabled && loaded.userShaders.shaders[16].values[0] == .5f &&
                    loaded.blackBars.enabled,
                "blocks are read back from their fields");

        // Every value read back from the text alone is the same.
        {
            SettingsStore::Node parsed;
            Require(SettingsStore::Parse(text, parsed), "parse own file");
            Require(SettingsStore::Serialize(parsed) == text, "file round trip");
            SettingsStore::Value value;
            const float nan = std::nanf("");
            value = {REG_BINARY, std::vector<BYTE>(4)};
            memcpy(value.data.data(), &nan, 4);
            Require(SettingsStore::ValueText(value).starts_with("hex:"), "NaN is kept as bytes");
            Require(SettingsStore::ParseValue("-1", value) && value.type == REG_DWORD &&
                        *reinterpret_cast<DWORD*>(value.data.data()) == 0xffffffff,
                    "negative numbers");
            Require(SettingsStore::ParseValue("hex(7):4100000000", value) && value.type == 7 && value.data.size() == 5,
                    "other value types");
            Require(!SettingsStore::ParseValue("hex:abc", value) && !SettingsStore::ParseValue("4294967296", value) &&
                        !SettingsStore::ParseValue("\"open", value),
                    "invalid values are rejected");
        }

        // Writes from this process are grouped; Flush writes what waits.
        for (int i = 1; i <= 20; ++i)
        {
            loaded.brightness = i / 64.f;
            Require(SUCCEEDED(SaveSettings(loaded)), "slider change");
        }
        Require(LoadSettings(display).brightness == 20 / 64.f, "the process sees its latest value at once");
        Require(SettingsStore::Flush(), "flush");
        Require(ReadText(ini).find("Brightness=0.3125") != std::string::npos, "flush writes the latest value");

        // Another player (or a hand edit) changes the file: it is read again,
        // a bad line costs only its own value.
        {
            auto edited = ReadText(ini);
            const auto at = edited.find("Brightness=0.3125");
            edited.replace(at, 17, "Brightness=0.5");
            // A deleted field keeps its default; a changed one is read.
            edited.replace(edited.find("Shaders.16.parameter0=0.5"), 25, "Shaders.16.parameter0=0.75");
            edited.erase(edited.find("Tonal.exposure0=0.2"), 19);
            const auto smooth = edited.find("SmoothMotion=1");
            edited.replace(smooth, 14, "SmoothMotion=oops");
            Sleep(20);
            WriteText(ini, edited);
            Later();
            loaded = LoadSettings(display);
            Require(loaded.brightness == .5f && !loaded.smoothMotion && !wcscmp(loaded.iccPath, old.iccPath),
                    "edited file is read again, bad value falls back to its default");
            Require(loaded.userShaders.shaders[16].values[0] == .75f && loaded.tonal.exposure[0] == 0 &&
                        loaded.userShaders.shaders[16].enabled,
                    "edited and deleted block fields");
        }

        // A file of a newer layout is never overwritten.
        {
            const auto kept = "; newer\r\n[FrankyRenderer]\r\nStorage=2\r\n[Settings]\r\nLinkDisplays=1\r\n";
            Sleep(20);
            WriteText(ini, kept);
            Later();
            Require(FAILED(DisplayProfiles::SetLinked(true)) || !SettingsStore::Flush(), "newer file refuses writes");
            Require(ReadText(ini) == kept, "newer file is kept");
            // Once the file can be read again, the waiting change is written.
            WriteText(ini, "");
            Later();
            Require(DisplayProfiles::Linked() && ReadText(ini).find("LinkDisplays=1") != std::string::npos,
                    "waiting change written later");
        }

        // Deleting the file gives the defaults; the next change starts a new
        // file from them, and the registry backup stays the original one.
        std::filesystem::remove(ini);
        Later();
        loaded = LoadSettings(display);
        Require(loaded.brightness == PlaceboSettings{}.brightness && !loaded.smoothMotion && !DisplayProfiles::Linked(),
                "deleted file gives defaults");
        loaded.smoothMotion = TRUE;
        Require(SUCCEEDED(SaveSettings(loaded)), "change after deleting");
        Require(ReadText(ini).find("SmoothMotion=1") != std::string::npos && LoadSettings(display).smoothMotion &&
                    LoadSettings(display).brightness == PlaceboSettings{}.brightness,
                "new file holds defaults and the change");
        Require(RegistryValue(root + L".Backup", L"LinkDisplays"), "backup survives");

        // A file of the first test builds (blocks as hex, "float:") is read and
        // written in the readable form at the next change.
        {
            TonalSettings tonal;
            tonal.exposure[0] = .3f;
            std::string hex;
            for (size_t i = 0; i < sizeof(tonal); ++i)
            {
                char digits[3];
                sprintf_s(digits, "%02x", reinterpret_cast<const BYTE*>(&tonal)[i]);
                hex += digits;
            }
            SettingsStore::Flush();
            Sleep(20);
            WriteText(ini,
                      "[FrankyRenderer]\r\nStorage=1\r\n[Settings\\Displays\\" + narrow +
                          "]\r\nBrightness=float:0.75\r\nHdrTonalV1=hex:" + hex + "\r\n");
            Later();
            loaded = LoadSettings(display);
            Require(loaded.brightness == .75f && loaded.tonal.exposure[0] == .3f, "earlier file layout is read");
            loaded.smoothMotion = TRUE;
            Require(SUCCEEDED(SaveSettings(loaded)) && SettingsStore::Flush(), "change after reading it");
            const auto rewritten = ReadText(ini);
            Require(rewritten.find("Tonal.exposure0=0.3") != rewritten.npos &&
                        rewritten.find("Brightness=0.75") != rewritten.npos &&
                        rewritten.find("hex") == rewritten.npos && rewritten.find("float:") == rewritten.npos,
                    "earlier file layout is written readable");
        }

        // Profile values are plain lines; an export is the file itself and an
        // import of it replaces every setting.
        {
            const std::string overrides = "Renderer.smoothMotion=1\nScaling.upscaler.filter=\"ewa_lanczos\"\n";
            Require(
                SettingsStore::Put(
                    root + L"\\Profiles\\Anime", L"Overrides", REG_BINARY, overrides.data(), DWORD(overrides.size())) ==
                        ERROR_SUCCESS &&
                    SettingsStore::Flush(),
                "profile values");
            const auto file = ReadText(ini);
            Require(file.find("[Settings\\Profiles\\Anime]\r\nRenderer.smoothMotion=1\r\nScaling.upscaler.filter="
                              "\"ewa_lanczos\"\r\n") != file.npos &&
                        file.find("Overrides") == file.npos && file.find("hex") == file.npos,
                    "profile values are written as lines");
            const auto exported = SettingsStore::Text();
            Require(exported == file && SettingsStore::IsSettingsText(exported), "export is the settings file");
            Require(!SettingsStore::IsSettingsText("[FrankyRenderer]\r\nFormatVersion=2\r\n"),
                    "an INI backup is not a settings file");
            auto imported = exported;
            imported.replace(imported.find("Brightness=0.75"), 15, "Brightness=0.5");
            const DWORD one = 1;
            Require(SettingsStore::Put(root + L"\\Extra", L"Gone", REG_DWORD, &one, 4) == ERROR_SUCCESS, "extra key");
            Require(SettingsStore::ReplaceAll(imported) && SettingsStore::Flush(), "import replaces the settings");
            Require(LoadSettings(display).brightness == .5f && !SettingsStore::Exists(root + L"\\Extra") &&
                        SettingsStore::Exists(root + L"\\Profiles\\Anime") && ReadText(ini) == imported,
                    "imported file is the settings file");
            Require(!SettingsStore::ReplaceAll("[FrankyRenderer]\r\nStorage=2\r\n"), "newer layout is refused");
        }

        // Without a place for the file, changes go to the registry.
        SettingsStore::Flush();
        std::filesystem::remove(ini);
        std::filesystem::create_directories(folder);
        WriteText((folder / L"Blocker").wstring(), "file, not folder");
        const std::wstring blocked = (folder / L"Blocker" / L"FrankyRenderer.ini").wstring();
        SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_INI", blocked.c_str());
        Later();
        loaded = LoadSettings(display);
        loaded.brightness = .75f;
        Require(SUCCEEDED(SaveSettings(loaded)), "save without a file");
        Require(!std::filesystem::exists(blocked) && LoadSettings(display).brightness == .75f &&
                    RegistryKey(root + L"\\Displays\\" + display),
                "registry fallback");

        puts(
            "PASS settings file: registry takeover with backup, crash notes kept, readable values and blocks, "
            "earlier hex layout converted, grouped writes, "
            "hand edits, newer layout kept, delete gives defaults, profile lines, export and import of the file, registry "
            "fallback");
    }
    catch (const std::exception& error)
    {
        printf("FAIL settings file: %s\n", error.what());
        result = 1;
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, (root + L".Backup").c_str());
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    return result;
}
