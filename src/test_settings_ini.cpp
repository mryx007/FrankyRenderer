#include "SettingsIni.h"
#include <cstdio>

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static void Reject(const std::string& text)
{
    bool rejected = false;
    try
    {
        SettingsIni::Parse(text);
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Require(rejected, "Invalid INI was accepted");
}

int wmain()
{
    try
    {
        PlaceboSettings original;
        original.brightness = .125f;
        original.saturation = 1.2345678f;
        original.autoHdr = FALSE;
        original.maximumFramesInFlight = 6;
        original.tonal.exposure[2] = .375f;
        original.tonal.contrast[1] = -.125f;
        original.blackBars.enabled = TRUE;
        original.blackBars.delay = 3.25f;
        original.blackBars.zoom = 1;
        original.extras.values[0] = .12345679f;
        strcpy_s(original.colorMapping.values[MappingIndex("knee_adaptation")], "0.75");
        auto& scaler = original.scaling.filters[0];
        strcpy_s(scaler.filter, "ewa_lanczos");
        scaler.values[2] = 1.125f;
        scaler.overrides = 1u << 2;
        original.userShaders.shaders[0].enabled = 1;
        original.userShaders.shaders[0].overrides = 1;
        original.userShaders.shaders[0].values[0] = .375f;
        std::swap(original.userShaders.order[0], original.userShaders.order[ShaderCount - 1]);
        wcscpy_s(original.iccPath, L"C:\\Farben\\Übertragung 日本語.icc");
        wcscpy_s(original.lutPath, L"C:\\LUTs\\a=b;# test.cube");
        const auto text = SettingsIni::Serialize(original);
        auto restored = SettingsIni::Parse(text);
        Require(SettingsIni::Serialize(restored) == text, "Backup round-trip changed values");
        Require(restored.saturation == original.saturation, "Float precision was lost");
        Require(restored.debandThreshold == -1 && restored.scaling.antiringing == -1, "Default sentinel was lost");
        Require(!wcscmp(restored.iccPath, original.iccPath), "Unicode path was lost");
        Require(!memcmp(&restored.userShaders, &original.userShaders, sizeof(original.userShaders)),
                "Shader values/order were lost");
        Require(SettingsIni::Serialize(SettingsIni::Parse("\xef\xbb\xbf" + text)) == text, "UTF-8 BOM failed");
        Require(SettingsIni::Serialize(SettingsIni::Parse("[FrankyRenderer]\nFormatVersion=1\n")) ==
                    SettingsIni::Serialize(PlaceboSettings{}),
                "Missing settings did not use defaults");
        for (const auto& bad : {std::string{},
                                std::string("[FrankyRenderer]\nFormatVersion=9\n"),
                                text + "\n[Renderer]\nbrightness=2\n",
                                text + "\n[Renderer]\nautoHdr=2\n",
                                std::string("[FrankyRenderer]\nFormatVersion=1\n[Renderer]\nbrightness=nan\n"),
                                std::string("[FrankyRenderer]\nFormatVersion=1\n[Renderer]\nbrightness=2\n"),
                                std::string("[FrankyRenderer]\nFormatVersion=1\n[Renderer]\nautoHdr=2\n"),
                                std::string("[FrankyRenderer]\nFormatVersion=1\n[Renderer]\niccPath=\"broken\\\"\n"),
                                text + "\n[Renderer]\nunknownSetting=1\n",
                                std::string(1024 * 1024 + 1, 'x')})
            Reject(bad);
        const auto folder = std::filesystem::absolute(L"src/build/ini-tests");
        std::filesystem::create_directories(folder);
        const auto path = folder / L"Einstellungen-ä.ini";
        std::wstring error;
        Require(SUCCEEDED(SettingsIni::Export(path.c_str(), original, error)), "Could not export INI");
        PlaceboSettings loaded;
        Require(SUCCEEDED(SettingsIni::Import(path.c_str(), loaded, error)), "Could not import INI");
        Require(SettingsIni::Serialize(loaded) == text, "File round-trip changed settings");
        auto invalid = original;
        invalid.brightness = 2;
        Require(FAILED(SettingsIni::Export(path.c_str(), invalid, error)), "Invalid settings were exported");
        Require(SUCCEEDED(SettingsIni::Import(path.c_str(), loaded, error)), "Failed export damaged previous backup");
        const auto badPath = folder / "invalid.ini";
        std::ofstream(badPath) << "[FrankyRenderer]\nFormatVersion=1\n[Renderer]\nbrightness=2\n";
        Require(FAILED(SettingsIni::Import(badPath.c_str(), loaded, error)), "Invalid file was imported");
        Require(SettingsIni::Serialize(loaded) == text, "Failed import partially modified settings");
        Require(FAILED(SettingsIni::Export(folder.c_str(), original, error)), "Directory accepted as export target");
        puts(
            "PASS INI: all setting groups, exact floats/defaults, Unicode paths, shader order and parameters, BOM, defaults for missing keys, malformed/range/duplicate/version rejection, atomic export and unchanged state on failed import");
        return 0;
    }
    catch (const std::exception& ex)
    {
        printf("FAIL INI: %s\n", ex.what());
        return 1;
    }
}
