#include "Settings/Settings.h"
#include <cstdio>
#include <stdexcept>

static void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main()
{
    wchar_t root[128];
    swprintf_s(root, L"Software\\FrankyBeforePlaybackTest_%lu", GetCurrentProcessId());
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root);
    int result = 0;
    try
    {
        const auto monitorHandle = reinterpret_cast<HMONITOR>(ULONG_PTR(1));
        const auto tvHandle = reinterpret_cast<HMONITOR>(ULONG_PTR(2));
        const auto extraHandle = reinterpret_cast<HMONITOR>(ULONG_PTR(3));
        const DisplayProfiles::Device monitor = {
            DisplayProfiles::Hash(L"test:monitor"), L"Monitor", L"test:monitor", monitorHandle, true};
        const DisplayProfiles::Device tv = {DisplayProfiles::Hash(L"test:tv"), L"TV", L"test:tv", tvHandle, true};
        const DisplayProfiles::Device extra = {
            DisplayProfiles::Hash(L"test:new"), L"New display", L"test:new", extraHandle, true};
        const std::vector<DisplayProfiles::Device> devices = {monitor, tv};
        PlaceboSettings draft = LoadSettings();
        Require(SUCCEEDED(PrepareDisplaySettings(draft, devices, monitorHandle)), "prepare pre-playback editor");
        Require(draft.displayProfile == monitor.id, "editor must target its display before playback");
        strcpy_s(draft.scaling.filters[0].filter, "ewa_lanczos");
        draft.scaling.antiringing = .3f;
        draft.smoothMotion = TRUE;
        Require(SUCCEEDED(SaveSettings(draft)), "save menu edits");
        const auto playing = LoadSettings(monitor.id);
        Require(!strcmp(playing.scaling.filters[0].filter, "ewa_lanczos") && playing.scaling.antiringing == .3f &&
                    playing.smoothMotion,
                "playback must load the pre-playback menu edits");
        const auto legacy = LoadSettings();
        Require(!legacy.displayProfile[0] && !legacy.smoothMotion && !strcmp(legacy.scaling.filters[0].filter, "auto"),
                "editing a display must not write legacy/global values");
        Require(!LoadSettings(tv.id).smoothMotion && !strcmp(LoadSettings(tv.id).scaling.filters[0].filter, "auto"),
                "other displays must keep their own settings");
        auto active = playing;
        Require(SUCCEEDED(PrepareDisplaySettings(active, devices, tvHandle)) && active.displayProfile == monitor.id &&
                    active.smoothMotion,
                "active output profile must survive a dialog on another display");
        auto offline = playing;
        Require(SUCCEEDED(PrepareDisplaySettings(offline, {}, nullptr)) && offline.displayProfile == monitor.id,
                "explicit profiles must remain editable when disconnected");
        auto unknown = LoadSettings();
        Require(FAILED(PrepareDisplaySettings(unknown, devices, extraHandle)) && !unknown.displayProfile[0],
                "unknown output must not silently expose the legacy editor");
        auto staleLegacy = legacy;
        staleLegacy.smoothMotion = TRUE;
        Require(SUCCEEDED(SaveSettings(staleLegacy)), "seed stale legacy value");
        auto fresh = LoadSettings();
        Require(SUCCEEDED(PrepareDisplaySettings(fresh, {monitor, tv, extra}, extraHandle)) &&
                    fresh.displayProfile == extra.id && !fresh.smoothMotion &&
                    !strcmp(fresh.scaling.filters[0].filter, "auto"),
                "new displays must use factory defaults, not legacy or another display");
        const auto migrationRoot = std::wstring(root) + L"\\LegacyMigration";
        SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", migrationRoot.c_str());
        Require(SUCCEEDED(SaveSettings(staleLegacy)), "seed initial legacy migration");
        auto migrated = LoadSettings();
        Require(SUCCEEDED(PrepareDisplaySettings(migrated, devices, monitorHandle)) &&
                    migrated.displayProfile == monitor.id && migrated.smoothMotion && LoadSettings(tv.id).smoothMotion,
                "first migration must preserve previous settings on attached displays");
        puts(
            "PASS pre-playback settings: assigned display, scaling/motion persistence, separate profiles, active output priority, disconnected profiles, factory defaults and legacy migration");
    }
    catch (const std::exception& error)
    {
        printf("FAIL pre-playback settings: %s\n", error.what());
        result = 1;
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, root);
    return result;
}
