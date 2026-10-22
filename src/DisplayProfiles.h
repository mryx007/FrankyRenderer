#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace DisplayProfiles
{
struct Device
{
    std::wstring id, name, identity;
    HMONITOR monitor = nullptr;
    bool connected = false;
};

inline std::wstring Root()
{
    wchar_t test[512];
    const auto size = GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", test, 512);
    return size && size < 512 ? test : L"Software\\PlaceboRenderer";
}

inline bool ValidId(const std::wstring& id)
{
    return id.size() == 64 && std::all_of(id.begin(),
                                          id.end(),
                                          [](wchar_t c) { return c >= L'0' && c <= L'9' || c >= L'a' && c <= L'f'; });
}

inline std::wstring Hash(const std::wstring& identity)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};
    BYTE hash[32];
    const auto result = BCryptHash(algorithm,
                                   nullptr,
                                   0,
                                   reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(identity.data())),
                                   ULONG(identity.size() * sizeof(wchar_t)),
                                   hash,
                                   sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0)
        return {};
    const wchar_t digits[] = L"0123456789abcdef";
    std::wstring id;
    for (BYTE byte : hash)
    {
        id += digits[byte >> 4];
        id += digits[byte & 15];
    }
    return id;
}

inline std::wstring CanonicalPath(std::wstring path)
{
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return towupper(c); });
    return L"path:" + path;
}

inline std::wstring Identity(const std::vector<BYTE>& edid, const std::wstring& path)
{
    if (edid.size() >= 128)
    {
        unsigned checksum = 0;
        for (size_t i = 0; i < 128; ++i)
            checksum += edid[i];
        const BYTE magic[] = {0, 255, 255, 255, 255, 255, 255, 0};
        if (!(checksum & 255) && std::equal(std::begin(magic), std::end(magic), edid.begin()))
        {
            wchar_t model[32];
            swprintf_s(model, L"edid:%02x%02x:%02x%02x:", edid[8], edid[9], edid[10], edid[11]);
            unsigned serial = 0;
            for (int i = 0; i < 4; ++i)
                serial |= unsigned(edid[12 + i]) << (8 * i);
            if (serial && serial != 0xffffffff)
                return std::wstring(model) + std::to_wstring(serial);
            for (int offset = 54; offset <= 108; offset += 18)
                if (!edid[offset] && !edid[offset + 1] && !edid[offset + 2] && edid[offset + 3] == 0xff)
                {
                    std::wstring text;
                    for (int i = 5; i < 18 && edid[offset + i] >= 32 && edid[offset + i] < 127; ++i)
                        text += wchar_t(edid[offset + i]);
                    while (!text.empty() && text.back() == L' ')
                        text.pop_back();
                    if (!text.empty() && text.find_first_not_of(L"0 ") != std::wstring::npos)
                        return std::wstring(model) + text;
                }
        }
    }
    return CanonicalPath(path);
}

inline std::vector<BYTE> ReadEdid(const std::wstring& path)
{
    const auto first = path.find(L'#'), second = path.find(L'#', first == path.npos ? first : first + 1);
    const auto third = path.find(L'#', second == path.npos ? second : second + 1);
    if (first == path.npos || second == path.npos || third == path.npos)
        return {};
    const auto key = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + path.substr(first + 1, second - first - 1) +
                     L"\\" + path.substr(second + 1, third - second - 1) + L"\\Device Parameters";
    BYTE bytes[512];
    DWORD size = sizeof(bytes), type = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"EDID", RRF_RT_REG_BINARY, &type, bytes, &size) != ERROR_SUCCESS)
        return {};
    return {bytes, bytes + size};
}

inline std::vector<Device> Connected()
{
    UINT32 pc = 0, mc = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pc, &mc))
        return {};
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pc, paths.data(), &mc, modes.data(), nullptr))
        return {};

    struct Screen
    {
        HMONITOR monitor;
        std::wstring name;
    };

    std::vector<Screen> screens;
    EnumDisplayMonitors(
        nullptr,
        nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM value) -> BOOL
        {
            MONITORINFOEXW info = {};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info))
                reinterpret_cast<std::vector<Screen>*>(value)->push_back({monitor, info.szDevice});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&screens));
    std::vector<Device> result;
    std::vector<std::wstring> devicePaths;
    for (UINT32 i = 0; i < pc; ++i)
    {
        const auto& p = paths[i];
        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
        target.header = {
            DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), p.targetInfo.adapterId, p.targetInfo.id};
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header = {
            DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), p.sourceInfo.adapterId, p.sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&target.header) || DisplayConfigGetDeviceInfo(&source.header) ||
            !target.monitorDevicePath[0])
            continue;
        Device device;
        device.identity = Identity(ReadEdid(target.monitorDevicePath), target.monitorDevicePath);
        device.name = target.monitorFriendlyDeviceName[0] ? target.monitorFriendlyDeviceName : L"Display";
        device.connected = true;
        for (const auto& screen : screens)
            if (!_wcsicmp(screen.name.c_str(), source.viewGdiDeviceName))
                device.monitor = screen.monitor;
        devicePaths.push_back(target.monitorDevicePath);
        result.push_back(std::move(device));
    }
    // Identical/missing serials must not merge two connected physical units.
    for (size_t i = 0; i < result.size(); ++i)
    {
        const auto original = result[i].identity;
        bool duplicate = false;
        for (size_t j = 0; j < result.size(); ++j)
            if (i != j && Identity(ReadEdid(devicePaths[j]), devicePaths[j]) == original)
                duplicate = true;
        // Once identical units received path-based IDs, preserve those IDs
        // when only one unit remains connected.
        HKEY previous = nullptr;
        const auto pathKey = Root() + L"\\Displays\\" + Hash(CanonicalPath(devicePaths[i]));
        const bool usedPath =
            RegOpenKeyExW(HKEY_CURRENT_USER, pathKey.c_str(), 0, KEY_READ, &previous) == ERROR_SUCCESS;
        if (previous)
            RegCloseKey(previous);
        if (duplicate || usedPath)
            result[i].identity = CanonicalPath(devicePaths[i]);
        result[i].id = Hash(result[i].identity);
    }
    result.erase(std::remove_if(result.begin(), result.end(), [](const Device& d) { return !ValidId(d.id); }),
                 result.end());
    return result;
}

inline std::wstring Key(const std::wstring& id)
{
    return Root() + L"\\Displays\\" + id;
}

inline std::wstring ReadString(HKEY key, const wchar_t* name)
{
    wchar_t text[2048];
    DWORD bytes = sizeof(text);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, text, &bytes) == ERROR_SUCCESS ? text : L"";
}

inline std::vector<Device> All()
{
    auto devices = Connected();
    HKEY root;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (Root() + L"\\Displays").c_str(), 0, KEY_READ, &root) != ERROR_SUCCESS)
        return devices;
    for (DWORD index = 0;; ++index)
    {
        wchar_t id[128];
        DWORD count = 128;
        if (RegEnumKeyExW(root, index, id, &count, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        if (!ValidId(id) || std::any_of(devices.begin(), devices.end(), [&](const Device& d) { return d.id == id; }))
            continue;
        HKEY key;
        if (RegOpenKeyExW(root, id, 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            devices.push_back({id, ReadString(key, L"DeviceName"), ReadString(key, L"DeviceIdentity"), nullptr, false});
            RegCloseKey(key);
        }
    }
    RegCloseKey(root);
    for (auto& device : devices)
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, Key(device.id).c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            const auto alias = ReadString(key, L"DisplayName");
            if (!alias.empty())
                device.name = alias;
            RegCloseKey(key);
        }
    }
    return devices;
}

inline HRESULT Rename(const std::wstring& id, std::wstring name)
{
    if (!ValidId(id))
        return E_INVALIDARG;
    const auto first = name.find_first_not_of(L" \t\r\n");
    if (first == name.npos)
        return E_INVALIDARG;
    name = name.substr(first, name.find_last_not_of(L" \t\r\n") - first + 1);
    if (name.size() > 80 || std::any_of(name.begin(), name.end(), [](wchar_t c) { return c < 32; }))
        return E_INVALIDARG;
    HKEY key;
    auto result = RegOpenKeyExW(HKEY_CURRENT_USER, Key(id).c_str(), 0, KEY_SET_VALUE, &key);
    if (result != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(result);
    result = RegSetValueExW(key,
                            L"DisplayName",
                            0,
                            REG_SZ,
                            reinterpret_cast<const BYTE*>(name.c_str()),
                            DWORD((name.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return HRESULT_FROM_WIN32(result);
}
}
