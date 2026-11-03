#include "Settings/SettingsIni.h"
#include <cstdio>
#include <sstream>

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
        for (int depth : {4, 5})
        {
            auto lowDepth = original;
            lowDepth.displayDepth = depth;
            Require(SUCCEEDED(ValidateSettings(lowDepth)), "Low dither depth was rejected");
            const auto restoredDepth = SettingsIni::Parse(SettingsIni::Serialize(lowDepth));
            Require(restoredDepth.displayDepth == depth, "Low dither depth was lost in INI round-trip");
            Require(BackendSettings(restoredDepth).displayDepth == depth, "Low dither depth was lost for backend");
        }
        original.rtxSuperResolution = TRUE;
        original.imageAnalysis = TRUE;
        original.clippingProtection = TRUE;
        auto legacy = SettingsIni::Parse(
            "[FrankyRenderer]\nFormatVersion=1\n[Renderer]\nhdrMode=0\nhdrPassthrough=1\nautoHdr=1\n");
        Require(legacy.hdrMode == 1 && !legacy.autoHdr, "legacy Auto migrates without enabling Windows HDR");
        original.brightness = .125f;
        original.saturation = 1.2345678f;
        original.autoHdr = FALSE;
        original.maximumFramesInFlight = 6;
        original.presentAhead = 3;
        original.motionThreshold = .25f;
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
        Require(restored.imageAnalysis, "Image analysis flag was lost");
        Require(restored.presentAhead == 3, "Present in advance was lost");
        Require(restored.motionThreshold == .25f, "Oversample threshold was lost");
        Require(SettingsIni::Serialize(restored) == text, "Backup round-trip changed values");
        Require(restored.saturation == original.saturation, "Float precision was lost");
        Require(restored.debandThreshold == -1 && restored.scaling.antiringing == -1, "Default sentinel was lost");
        Require(!wcscmp(restored.iccPath, original.iccPath), "Unicode path was lost");
        Require(!memcmp(&restored.userShaders, &original.userShaders, sizeof(original.userShaders)),
                "Shader values/order were lost");
        Require(SettingsIni::Serialize(SettingsIni::Parse("\xef\xbb\xbf" + text)) == text, "UTF-8 BOM failed");
        {
            // A file from before the RAVU Zoom R3 shaders: 21 chain slots, no R3 sections.
            PlaceboSettings old;
            for (int i = 0; i < 21; ++i)
                old.userShaders.order[i] = 20 - i;
            old.userShaders.order[21] = 21;
            old.userShaders.order[22] = 22;
            std::string trimmed, section, line;
            std::istringstream lines(SettingsIni::Serialize(old));
            while (std::getline(lines, line))
            {
                if (line.starts_with("["))
                    section = line;
                if (section.find("ravu_zoom_r3") != std::string::npos ||
                    section.find("ravu_zoom_ar_r3") != std::string::npos ||
                    (section.starts_with("[ShaderOrder]") &&
                     (line.starts_with("slot21") || line.starts_with("slot22"))))
                    continue;
                trimmed += line + "\n";
            }
            Require(trimmed.size() < SettingsIni::Serialize(old).size(), "Old INI simulation removed nothing");
            const auto migrated = SettingsIni::Parse(trimmed);
            const int expected[] = {20, 19, 18, 17, 16, 15, 21, 22, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
            Require(!memcmp(migrated.userShaders.order, expected, sizeof(expected)),
                    "Old shader order did not gain the R3 shaders");
        }
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
        // Registry profiles saved before the RAVU Zoom R3 shaders (only with the
        // disposable key from run_tests.py, never the real settings).
        if (GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0))
        {
            struct
            {
                ShaderConfig shaders[21];
                int order[21];
            } v2 = {};

            for (int i = 0; i < 21; ++i)
                v2.order[i] = (i + 3) % 21;
            v2.shaders[15].enabled = 1;
            v2.shaders[4].enabled = 1;
            HKEY key = nullptr;
            Require(RegCreateKeyExW(HKEY_CURRENT_USER,
                                    DisplayProfiles::Root().c_str(),
                                    0,
                                    nullptr,
                                    0,
                                    KEY_SET_VALUE,
                                    nullptr,
                                    &key,
                                    nullptr) == ERROR_SUCCESS,
                    "Test settings key");
            RegDeleteValueW(key, L"UserShadersV3");
            Require(
                RegSetValueExW(key, L"UserShadersV2", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&v2), sizeof(v2)) ==
                    ERROR_SUCCESS,
                "Write V2 shaders");
            RegCloseKey(key);
            const auto shaders = LoadSettings().userShaders;
            const int expected[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 21, 22, 16, 17, 18, 19, 20, 0, 1, 2};
            Require(!memcmp(shaders.order, expected, sizeof(expected)), "V2 shader order was not completed");
            Require(shaders.shaders[15].enabled && shaders.shaders[4].enabled && !shaders.shaders[21].enabled &&
                        !shaders.shaders[22].enabled,
                    "V2 shader switches were not kept");
            auto saved = LoadSettings();
            saved.userShaders.shaders[22].enabled = 1;
            Require(SUCCEEDED(SaveSettings(saved)), "Save V3 shaders");
            Require(LoadSettings().userShaders.shaders[22].enabled, "V3 shaders were not read back");
        }
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
