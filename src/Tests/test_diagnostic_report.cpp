#include "Renderer/DiagnosticReport.h"
#include <cstdio>
#include <cstdlib>

static void Require(bool ok, const char* message)
{
    if (ok)
        return;
    printf("FAIL %s\n", message);
    exit(1);
}

int main()
{
    wchar_t root[80], appData[260];
    swprintf_s(root, L"Software\\PlaceboRendererTest_diag_%lu", GetCurrentProcessId());
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root);
    const auto folder =
        std::filesystem::temp_directory_path() / (L"FrankyDiag" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    wcscpy_s(appData, folder.c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA", appData);

    Require(!TraceEnabled(), "tests never write a log");
    Trace("not written");
    Require(!std::filesystem::exists(Diagnostics::LogFile()), "nothing is recorded unless it is switched on");

    Require(!Diagnostics::Armed(), "off by default");
    Diagnostics::SetArmed(true);
    Require(Diagnostics::Armed(), "switch stays on for the next start");
    Require(Diagnostics::ConsumeArmed() && !Diagnostics::Armed(), "one session uses it up");
    Require(!Diagnostics::ConsumeArmed(), "the following session records nothing");
    Diagnostics::SetArmed(true);
    Diagnostics::SetArmed(false);
    Require(!Diagnostics::ConsumeArmed(), "switching off before the restart cancels it");

    Require(Diagnostics::PlainStatistics(L"\x01Video\nDrops\t\x02r2 dropped\x02w") == L"-- Video\nDrops: 2 dropped",
            "statistics lose their on-screen markup");

    const auto settings = Diagnostics::HideSettingPaths("[Renderer]\nicc=D:\\Colour\\my.icc\nsaturation=1\n");
    Require(settings.find("Colour") == std::string::npos && settings.find("saturation=1") != std::string::npos,
            "setting paths are hidden, values stay");

    wchar_t profile[MAX_PATH] = {};
    GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    const auto hidden = Diagnostics::HidePrivateText(std::wstring(L"file ") + profile + L"\\Videos\\holiday.mkv");
    Require(hidden.find(profile) == std::wstring::npos && hidden.find(L"<user folder>") != std::wstring::npos,
            "the user folder is hidden");

    {
        std::ofstream log(Diagnostics::LogFile(), std::ios::binary);
        log << "2026-11-09 12:00:00 PID=1 Renderer stop begin\n";
    }
    const auto report = Diagnostics::BuildReport("[Renderer]\nsaturation=1\n");
    for (const wchar_t* part : {L"Renderer: ",
                                L"Player: ",
                                L"Windows: ",
                                L"GPU: ",
                                L"Display ",
                                L"NVIDIA app: ",
                                L"[Now playing]",
                                L"[Log]",
                                L"Renderer stop begin",
                                L"[Settings]",
                                L"saturation=1"})
        if (report.find(part) == std::wstring::npos)
        {
            wprintf(L"missing: %ls\n%ls\n", part, report.substr(0, 700).c_str());
            Require(false, "report section");
        }
    Diagnostics::SetLive([] { return std::wstring(L"Filters in graph: LAV\n\x01Video"); });
    Require(Diagnostics::BuildReport("").find(L"-- Video") != std::wstring::npos, "report includes what is playing");
    Diagnostics::SetLive(nullptr);

    std::error_code error;
    std::filesystem::remove_all(folder, error);
    RegDeleteTreeW(HKEY_CURRENT_USER, root);
    puts("PASS diagnostic report: one session switch, private text hidden, report sections");
    return 0;
}
