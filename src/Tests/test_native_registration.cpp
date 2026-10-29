// FrankyRenderer native registration integration test. GPL-3.0-or-later.
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#include <cstdio>
#include <string>
#include <stdexcept>

static void Require(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}

static std::wstring Read(HKEY root, const std::wstring& key)
{
    wchar_t value[32768] = {};
    DWORD size = sizeof(value);
    const auto result = RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &size);
    return result == ERROR_SUCCESS ? value : L"";
}

static void Write(HKEY root, const std::wstring& key, const std::wstring& value)
{
    Require(RegSetKeyValueW(
                root, key.c_str(), nullptr, REG_SZ, value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t))) ==
                ERROR_SUCCESS,
            "write disposable registration fixture");
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
        return 2;
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize COM");
    IFilterMapper2* mapper = nullptr;
    Require(SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mapper))),
            "preload system filter mapper");
    HMODULE module =
        LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
        return 3;
    using Registration = HRESULT(WINAPI*)();
    const auto install = reinterpret_cast<Registration>(GetProcAddress(module, "DllRegisterServer"));
    const auto remove = reinterpret_cast<Registration>(GetProcAddress(module, "DllUnregisterServer"));
    if (!install || !remove)
        return 4;

    GUID unique;
    CoCreateGuid(&unique);
    wchar_t id[40];
    StringFromGUID2(unique, id, 40);
    const std::wstring path = std::wstring(L"Software\\FrankyTests\\NativeRegistration-") + id;
    HKEY fixture = nullptr, classes = nullptr, machine = nullptr, user = nullptr;
    const auto create = [](HKEY parent, const wchar_t* key, HKEY* out)
    {
        Require(RegCreateKeyExW(parent, key, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, out, nullptr) == ERROR_SUCCESS,
                "create disposable registry root");
    };
    create(HKEY_CURRENT_USER, path.c_str(), &fixture);
    create(fixture, L"Classes", &classes);
    create(fixture, L"Machine", &machine);
    create(fixture, L"User", &user);
    const std::wstring own = L"{B864E92A-1BDF-4DA1-9004-CBF467DF239A}";
    const std::wstring page = L"{19A4F3E2-F52E-4DBE-9167-332E7C38E80C}";
    const std::wstring alias = L"{71F080AA-8661-4093-B15E-4F6903E77D0A}";
    const std::wstring original = L"C:\\OriginalMPCVR\\MpcVideoRenderer64.ax";
    const std::wstring category = L"CLSID\\{083863F1-70DE-11D0-BD40-00A0C911CE86}\\Instance\\";
    Write(classes, L"CLSID\\" + alias + L"\\InprocServer32", original);
    Write(classes, category + alias, L"Original MPCVR category marker");
    bool redirectedClasses = false, redirectedMachine = false, redirectedUser = false;
    int result = 0;
    try
    {
        // Every standard registry root used by the DLL is redirected in this
        // process. The user's COM registrations cannot be changed by this test.
        Require(RegOverridePredefKey(HKEY_CLASSES_ROOT, classes) == ERROR_SUCCESS, "redirect classes");
        redirectedClasses = true;
        Require(RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine) == ERROR_SUCCESS, "redirect machine");
        redirectedMachine = true;
        Require(RegOverridePredefKey(HKEY_CURRENT_USER, user) == ERROR_SUCCESS, "redirect user");
        redirectedUser = true;
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            const HRESULT hr = install();
            printf("DllRegisterServer: %08lX\n", hr);
            Require(SUCCEEDED(hr), "register in disposable registry");
            Require(!_wcsicmp(Read(classes, L"CLSID\\" + own + L"\\InprocServer32").c_str(), argv[1]),
                    "dedicated renderer registered to new AX filename");
            Require(!_wcsicmp(Read(classes, L"CLSID\\" + page + L"\\InprocServer32").c_str(), argv[1]),
                    "settings page registered to new AX filename");
            Require(Read(classes, L"CLSID\\" + alias + L"\\InprocServer32") == original,
                    "original MPCVR registration unchanged");
            Require(Read(classes, category + alias) == L"Original MPCVR category marker",
                    "original MPCVR category unchanged");
            HKEY entry = nullptr;
            Require(RegOpenKeyExW(classes, (category + own).c_str(), 0, KEY_READ, &entry) == ERROR_SUCCESS,
                    "dedicated renderer listed in DirectShow category");
            RegCloseKey(entry);
        }
        Require(SUCCEEDED(remove()), "unregister native renderer");
        Require(Read(classes, L"CLSID\\" + own + L"\\InprocServer32").empty(), "dedicated renderer removed");
        Require(Read(classes, L"CLSID\\" + page + L"\\InprocServer32").empty(), "settings page removed");
        Require(Read(classes, L"CLSID\\" + alias + L"\\InprocServer32") == original,
                "uninstall preserves original MPCVR");
        Require(Read(classes, category + alias) == L"Original MPCVR category marker",
                "uninstall preserves original MPCVR category");
        puts("PASS actual native registration/unregistration, new AX filename and original MPCVR coexistence");
    }
    catch (const std::exception& error)
    {
        printf("FAIL native registration: %s\n", error.what());
        result = 1;
    }
    if (redirectedUser)
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    if (redirectedMachine)
        RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
    if (redirectedClasses)
        RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
    RegCloseKey(user);
    RegCloseKey(machine);
    RegCloseKey(classes);
    RegCloseKey(fixture);
    RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    mapper->Release();
    FreeLibrary(module);
    CoUninitialize();
    return result;
}
