#pragma once
#include "Renderer/NvidiaAppSettings.h"
#include "Renderer/Trace.h"
#include "Renderer/WindowsHdrSwitch.h"
#include "Settings/CustomShaders.h"
#include <windows.h>
#include <commdlg.h>
#include <dxgi1_6.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "version.lib")

// A text file for bug reports: system, renderer state, settings and the log.
namespace Diagnostics
{
inline std::wstring RegistryText(const wchar_t* key, const wchar_t* name)
{
    wchar_t value[256] = {};
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS ? value
                                                                                                              : L"";
}

inline std::wstring WindowsVersion()
{
    constexpr wchar_t key[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    const auto build = RegistryText(key, L"CurrentBuildNumber");
    const auto display = RegistryText(key, L"DisplayVersion");
    auto product = RegistryText(key, L"ProductName");
    // Windows 11 still reports "Windows 10" here.
    if (_wtoi(build.c_str()) >= 22000)
        if (const auto at = product.find(L"Windows 10"); at != std::wstring::npos)
            product.replace(at, 10, L"Windows 11");
    return product + L" " + display + L" (build " + build + L")";
}

inline std::wstring FileVersion(const wchar_t* path)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!size || !GetFileVersionInfoW(path, 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
        return L"unknown version";
    return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS)) +
           L"." + std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." +
           std::to_wstring(LOWORD(info->dwFileVersionLS));
}

inline std::wstring PlayerName()
{
    wchar_t path[32768];
    const DWORD count = GetModuleFileNameW(nullptr, path, 32768);
    if (!count || count >= 32768)
        return L"unknown";
    return std::filesystem::path(path).filename().wstring() + L" " + FileVersion(path);
}

// Nightly packages name their build in INSTALL.txt.
inline std::wstring BuildName()
{
    std::ifstream install(CustomShaders::Folder().parent_path().parent_path() / L"INSTALL.txt");
    for (std::string line; std::getline(install, line);)
        if (line.rfind("Nightly test build:", 0) == 0)
            return L"nightly " + CustomShaders::Wide(CustomShaders::Trim(line.substr(19)));
    return L"local or stable build";
}

inline std::wstring HdrState(NvidiaAppSettings::State state)
{
    return state == NvidiaAppSettings::State::On ? L"on" : state == NvidiaAppSettings::State::Off ? L"off" : L"unknown";
}

inline float DisplayPeak(const wchar_t* device)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return 0;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) == S_OK; ++a)
    {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, output.ReleaseAndGetAddressOf()) == S_OK; ++o)
        {
            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 desc = {};
            if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)) &&
                !wcscmp(desc.DeviceName, device))
                return desc.MaxLuminance;
        }
    }
    return 0;
}

inline void AppendGraphicsCards(std::wostringstream& out)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, adapter.ReleaseAndGetAddressOf()) == S_OK; ++a)
    {
        DXGI_ADAPTER_DESC1 desc = {};
        if (FAILED(adapter->GetDesc1(&desc)) || desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        out << L"GPU: " << desc.Description;
        LARGE_INTEGER driver = {};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver)))
        {
            const unsigned third = HIWORD(driver.LowPart), fourth = LOWORD(driver.LowPart);
            out << L", driver " << HIWORD(driver.HighPart) << L"." << LOWORD(driver.HighPart) << L"." << third << L"."
                << fourth;
            // NVIDIA's own number is the last five digits of that version.
            if (desc.VendorId == 0x10DE)
            {
                const unsigned number = third % 10 * 10000 + fourth;
                wchar_t text[16];
                swprintf_s(text, L"%u.%02u", number / 100, number % 100);
                out << L" (" << text << L")";
            }
        }
        out << L"\n";
    }
}

inline BOOL CALLBACK AppendDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto& out = *reinterpret_cast<std::wostringstream*>(data);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    DEVMODEW mode = {};
    mode.dmSize = sizeof(mode);
    if (!GetMonitorInfoW(monitor, &info) || !EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        return TRUE;
    out << L"Display " << info.szDevice << L": " << mode.dmPelsWidth << L"x" << mode.dmPelsHeight << L" @ "
        << mode.dmDisplayFrequency << L" Hz";
    if (const auto hdr = WindowsHdr::Query(info.szDevice))
        out << L", HDR " << (!hdr->supported ? L"not supported" : hdr->enabled ? L"on" : L"off");
    if (const float peak = DisplayPeak(info.szDevice); peak > 0)
        out << L", peak " << int(peak) << L" nits";
    out << L"\n";
    return TRUE;
}

// The statistics text carries markup for the on-screen box.
inline std::wstring PlainStatistics(const std::wstring& text)
{
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\x02')
            ++i;
        else if (text[i] == L'\x01')
            out += L"-- ";
        else if (text[i] == L'\t')
            out += L": ";
        else
            out += text[i];
    }
    return out;
}

inline std::wstring ReplaceNoCase(std::wstring text, const std::wstring& find, const std::wstring& with)
{
    if (find.empty())
        return text;
    const auto lower = [](std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), towlower);
        return value;
    };
    const auto wanted = lower(find);
    auto folded = lower(text);
    for (size_t at = folded.find(wanted); at != std::wstring::npos; at = folded.find(wanted, at + with.size()))
    {
        text.replace(at, find.size(), with);
        folded.replace(at, find.size(), with);
    }
    return text;
}

// Reports go to public bug trackers: no user names, no folders.
inline std::wstring HidePrivateText(std::wstring text)
{
    wchar_t profile[MAX_PATH] = {}, user[256] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH))
        text = ReplaceNoCase(std::move(text), profile, L"<user folder>");
    if (GetEnvironmentVariableW(L"USERNAME", user, 256))
        text = ReplaceNoCase(std::move(text), std::wstring(L"\\Users\\") + user, L"\\Users\\<user>");
    return text;
}

// Exported settings can hold ICC and LUT paths.
inline std::string HideSettingPaths(const std::string& settings)
{
    std::istringstream input(settings);
    std::string out;
    for (std::string line; std::getline(input, line);)
    {
        const auto equals = line.find('=');
        if (equals != std::string::npos && line.find(":\\", equals) != std::string::npos)
            line = line.substr(0, equals + 1) + "<path hidden>";
        out += line + "\n";
    }
    return out;
}

inline std::string ReadFileText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

inline std::wstring BuildReport(const std::string& settings)
{
    std::wostringstream out;
    out << L"FrankyRenderer diagnostic report\n"
        << L"Renderer: " << BuildName() << L"\n"
        << L"Player: " << PlayerName() << L"\n"
        << L"Windows: " << WindowsVersion() << L"\n";
    AppendGraphicsCards(out);
    EnumDisplayMonitors(nullptr, nullptr, AppendDisplay, reinterpret_cast<LPARAM>(&out));
    NvidiaAppSettings nvidia;
    out << L"NVIDIA app: Super Resolution " << HdrState(nvidia.SuperResolution()) << L", RTX Video HDR "
        << HdrState(nvidia.TrueHdr()) << L"\n";
    CustomShaders::Refresh();
    int custom = 0;
    for (int id = ShaderBuiltinCount; id < ShaderCount; ++id)
        custom += ShaderListed(id);
    out << L"Custom shaders: " << custom << L"\n";

    const auto live = LiveText();
    out << L"\n[Now playing]\n"
        << (live.empty() ? L"Nothing is playing. Statistics appear here when Ctrl+J is on during playback.\n"
                         : PlainStatistics(live) + L"\n");
    const auto current = ReadFileText(LogFile()), previous = ReadFileText(LogFile(L"FrankyRenderer.old.log"));
    out << L"\n[Log]\n";
    if (current.empty() && previous.empty())
        out << L"No log was recorded. Switch on Diagnostic Log, restart the player, reproduce the problem, then save "
               L"the report again.\n";
    else
        out << CustomShaders::Wide(previous + current);
    out << L"\n[Settings]\n" << CustomShaders::Wide(HideSettingPaths(settings));
    return HidePrivateText(out.str());
}

inline bool WriteReport(const std::filesystem::path& path, const std::wstring& report)
{
    std::ofstream file(path, std::ios::binary);
    const auto text = "\xEF\xBB\xBF" + CustomShaders::Utf8(report);
    return file.write(text.data(), std::streamsize(text.size())).good();
}

inline void ShowInExplorer(const std::filesystem::path& path)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str()))
    {
        SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
    }
    if (SUCCEEDED(com))
        CoUninitialize();
}

// Writes the report to the desktop and shows it; asks where to save only when
// the desktop cannot be written. False when nothing was saved.
inline bool SaveReport(const std::string& settings, HWND owner)
{
    const auto report = BuildReport(settings);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[80];
    swprintf_s(name,
               L"FrankyRenderer-Report-%04u%02u%02u-%02u%02u%02u.txt",
               now.wYear,
               now.wMonth,
               now.wDay,
               now.wHour,
               now.wMinute,
               now.wSecond);
    std::filesystem::path target;
    PWSTR desktop = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)))
        target = std::filesystem::path(desktop) / name;
    CoTaskMemFree(desktop);
    if (target.empty() || !WriteReport(target, report))
    {
        wchar_t file[MAX_PATH];
        wcscpy_s(file, name);
        OPENFILENAMEW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = L"Text files\0*.txt\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrDefExt = L"txt";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog))
            return false;
        target = file;
        if (!WriteReport(target, report))
            return false;
    }
    ShowInExplorer(target);
    return true;
}
}
