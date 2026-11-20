#pragma once
#include <windows.h>
#include <bcrypt.h>
#include "Settings/SettingsStore.h"
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <string>
#include <vector>
#include <array>
#include <stdexcept>
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
    return SettingsStore::Root();
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
        const bool usedPath = SettingsStore::Exists(Root() + L"\\Displays\\" + Hash(CanonicalPath(devicePaths[i])));
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

// While displays are linked, saving one display's settings writes the same
// values to every display. Unlinking keeps them, so each starts from that
// shared state and can then be changed alone.
inline bool Linked()
{
    DWORD value = 0, bytes = sizeof(value);
    return SettingsStore::Get(Root(), L"LinkDisplays", REG_DWORD, &value, &bytes) == ERROR_SUCCESS && value;
}

inline HRESULT SetLinked(bool linked)
{
    const DWORD value = linked;
    return HRESULT_FROM_WIN32(SettingsStore::Put(Root(), L"LinkDisplays", REG_DWORD, &value, sizeof(value)));
}

// Every display that has stored settings, without querying the hardware.
inline std::vector<std::wstring> StoredIds()
{
    std::vector<std::wstring> ids;
    SettingsStore::Key root;
    if (SettingsStore::Open(Root() + L"\\Displays", root) != ERROR_SUCCESS)
        return ids;
    for (const auto& id : root.SubKeys())
        if (ValidId(id))
            ids.push_back(id);
    return ids;
}

inline std::wstring ReadString(const SettingsStore::Key& key, const wchar_t* name)
{
    wchar_t text[2048];
    DWORD bytes = sizeof(text), type = 0;
    return key.Query(name, &type, text, &bytes) == ERROR_SUCCESS && type == REG_SZ && bytes >= sizeof(wchar_t) &&
                   !text[bytes / sizeof(wchar_t) - 1]
               ? text
               : L"";
}

struct Metadata
{
    bool exists = false;
    std::array<std::wstring, 3> text;
};

inline constexpr const wchar_t* MetadataKeys[] = {L"DeviceName", L"DeviceIdentity", L"DisplayName"};

inline Metadata ReadMetadata(const std::wstring& id)
{
    if (!ValidId(id))
        throw std::runtime_error("Invalid display profile identity.");
    Metadata metadata;
    SettingsStore::Key key;
    const auto result = SettingsStore::Open(Key(id), key);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        return metadata;
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Could not read display profile.");
    metadata.exists = true;
    for (int i = 0; i < 3; ++i)
        metadata.text[i] = ReadString(key, MetadataKeys[i]);
    return metadata;
}

// Counts deleted profile keys; settings written before no longer match the registry.
inline std::atomic<unsigned> KeysDeleted{0};

inline HRESULT WriteMetadata(const std::wstring& id, const Metadata& metadata)
{
    if (!ValidId(id))
        return E_INVALIDARG;
    if (!metadata.exists)
    {
        ++KeysDeleted;
        return HRESULT_FROM_WIN32(SettingsStore::DeleteTree(Key(id)));
    }
    SettingsStore::Batch batch;
    SettingsStore::Key key;
    auto result = SettingsStore::Create(Key(id), key);
    for (int i = 0; i < 3 && result == ERROR_SUCCESS; ++i)
    {
        const auto& value = metadata.text[i];
        result = value.empty()
                     ? key.Delete(MetadataKeys[i])
                     : key.Set(MetadataKeys[i], REG_SZ, value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t)));
    }
    return HRESULT_FROM_WIN32(result);
}

inline std::vector<Device> All()
{
    auto devices = Connected();
    for (const auto& id : StoredIds())
    {
        if (std::any_of(devices.begin(), devices.end(), [&](const Device& d) { return d.id == id; }))
            continue;
        const SettingsStore::Key key(L"Displays\\" + id);
        devices.push_back({id, ReadString(key, L"DeviceName"), ReadString(key, L"DeviceIdentity"), nullptr, false});
    }
    for (auto& device : devices)
    {
        SettingsStore::Key key;
        if (SettingsStore::Open(Key(device.id), key) == ERROR_SUCCESS)
        {
            const auto alias = ReadString(key, L"DisplayName");
            if (!alias.empty())
                device.name = alias;
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
    SettingsStore::Key key;
    auto result = SettingsStore::Open(Key(id), key);
    if (result == ERROR_SUCCESS)
        result = key.Set(L"DisplayName", REG_SZ, name.c_str(), DWORD((name.size() + 1) * sizeof(wchar_t)));
    return HRESULT_FROM_WIN32(result);
}
}
