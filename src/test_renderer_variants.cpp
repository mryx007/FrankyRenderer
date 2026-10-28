// Registration and identity isolation for the native and PotPlayer packages.
#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;

static void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

static std::wstring Guid(const GUID& id)
{
    wchar_t text[40];
    Require(StringFromGUID2(id, text, 40) != 0, "GUID string");
    return text;
}

static std::wstring Read(HKEY root, const std::wstring& key)
{
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value);
    return RegGetValueW(root, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS ? value
                                                                                                            : L"";
}

static bool Exists(HKEY root, const std::wstring& key)
{
    HKEY opened = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ, &opened) != ERROR_SUCCESS)
        return false;
    RegCloseKey(opened);
    return true;
}

struct Module
{
    using Registration = HRESULT(WINAPI*)();
    using Getter = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    HMODULE handle;
    Registration install, remove;
    Getter getter;

    explicit Module(const wchar_t* path)
    {
        handle = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        Require(handle != nullptr, "load package module");
        install = reinterpret_cast<Registration>(GetProcAddress(handle, "DllRegisterServer"));
        remove = reinterpret_cast<Registration>(GetProcAddress(handle, "DllUnregisterServer"));
        getter = reinterpret_cast<Getter>(GetProcAddress(handle, "DllGetClassObject"));
        Require(install && remove && getter, "package exports");
    }

    void Identity(const CLSID& expected, const CLSID& excluded, const CLSID& page)
    {
        ComPtr<IClassFactory> factory, other;
        Require(SUCCEEDED(getter(expected, IID_PPV_ARGS(&factory))), "expected factory available");
        Require(getter(excluded, IID_PPV_ARGS(&other)) == CLASS_E_CLASSNOTAVAILABLE && !other,
                "opposite renderer factory absent");
        ComPtr<IBaseFilter> filter;
        Require(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&filter))), "create variant renderer");
        CLSID actual{};
        Require(SUCCEEDED(filter->GetClassID(&actual)) && actual == expected,
                "actual renderer identity matches package");
        ComPtr<ISpecifyPropertyPages> pages;
        Require(SUCCEEDED(filter.As(&pages)), "variant property pages");
        CAUUID ids{};
        Require(SUCCEEDED(pages->GetPages(&ids)), "get variant page identity");
        bool correct = ids.cElems == 1 && ids.pElems && ids.pElems[0] == page;
        CoTaskMemFree(ids.pElems);
        Require(correct, "independent settings page identity");
    }
};

static int Test(int argc, wchar_t** argv)
{
    if (argc != 3)
        return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    ComPtr<IFilterMapper2> mapper;
    Require(SUCCEEDED(CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mapper))),
            "preload mapper before disposable registry");
    GUID unique;
    CoCreateGuid(&unique);
    std::wstring fixturePath = L"Software\\FrankyTests\\RendererVariants-" + Guid(unique);
    HKEY fixture = nullptr, classes = nullptr, machine = nullptr, user = nullptr;
    auto create = [](HKEY parent, const wchar_t* path, HKEY* key)
    {
        Require(RegCreateKeyExW(parent, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, key, nullptr) == ERROR_SUCCESS,
                "disposable registry fixture");
    };
    create(HKEY_CURRENT_USER, fixturePath.c_str(), &fixture);
    create(fixture, L"Classes", &classes);
    create(fixture, L"Machine", &machine);
    create(fixture, L"User", &user);
    const CLSID native = {0xb864e92a, 0x1bdf, 0x4da1, {0x90, 0x04, 0xcb, 0xf4, 0x67, 0xdf, 0x23, 0x9a}};
    const CLSID potplayer = {0x71f080aa, 0x8661, 0x4093, {0xb1, 0x5e, 0x4f, 0x69, 0x03, 0xe7, 0x7d, 0x0a}};
    const CLSID nativePage = {0x19a4f3e2, 0xf52e, 0x4dbe, {0x91, 0x67, 0x33, 0x2e, 0x7c, 0x38, 0xe8, 0x0c}};
    const CLSID potPage = {0x98f20bc7, 0x42a9, 0x4fba, {0xb1, 0x71, 0x39, 0xca, 0x08, 0x65, 0x1f, 0xca}};
    const CLSID nativeFilter = {0x40f090c8, 0xe82b, 0x4ce9, {0x93, 0x1b, 0xa3, 0x47, 0x60, 0xe6, 0xd4, 0x9e}};
    const CLSID potFilter = {0x6a4027eb, 0x542f, 0x450d, {0xa3, 0xe0, 0xd7, 0x89, 0x9d, 0x2b, 0x54, 0xc4}};
    const auto category = std::wstring(L"CLSID\\{083863F1-70DE-11D0-BD40-00A0C911CE86}\\Instance\\");
    auto server = [](const CLSID& id) { return L"CLSID\\" + Guid(id) + L"\\InprocServer32"; };
    bool redirected[3]{};
    int result = 0;
    try
    {
        Module n(argv[1]), p(argv[2]);
        Require(RegOverridePredefKey(HKEY_CLASSES_ROOT, classes) == ERROR_SUCCESS, "redirect classes");
        redirected[0] = true;
        Require(RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine) == ERROR_SUCCESS, "redirect machine");
        redirected[1] = true;
        Require(RegOverridePredefKey(HKEY_CURRENT_USER, user) == ERROR_SUCCESS, "redirect user");
        redirected[2] = true;
        n.Identity(native, potplayer, nativePage);
        p.Identity(potplayer, native, potPage);
        for (auto module : {&n, &p})
            for (auto id : {nativeFilter, potFilter})
            {
                ComPtr<IClassFactory> unused;
                Require(module->getter(id, IID_PPV_ARGS(&unused)) == CLASS_E_CLASSNOTAVAILABLE && !unused,
                        "renderer must not expose a separate DV filter class");
            }

        puts("PASS each package exposes only its renderer identity and its own settings page");
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            Require(SUCCEEDED(n.install()), "install native in disposable registry");
            Require(Read(classes, server(native)) == argv[1] && Exists(classes, category + Guid(native)),
                    "native class and MPC-BE category registered");
            Require(!Exists(classes, server(potplayer)), "native registration adds no compatibility alias");
            Require(SUCCEEDED(p.install()), "install PotPlayer in disposable registry");
            Require(Read(classes, server(potplayer)) == argv[2] && Exists(classes, category + Guid(potplayer)),
                    "PotPlayer class and filter category registered");
            Require(Read(classes, server(native)) == argv[1], "PotPlayer registration preserves native renderer");
            Require(!Exists(classes, server(nativeFilter)) && !Exists(classes, server(potFilter)) &&
                        !Exists(classes, category + Guid(nativeFilter)) && !Exists(classes, category + Guid(potFilter)),
                    "integrated packages must not add DV filter registrations");
            Require(Read(classes, server(nativePage)) == argv[1] && Read(classes, server(potPage)) == argv[2],
                    "both settings pages remain independent");
            Require(SUCCEEDED(n.remove()), "remove native variant");
            Require(!Exists(classes, server(native)) && !Exists(classes, category + Guid(native)) &&
                        Read(classes, server(potplayer)) == argv[2],
                    "native removal preserves PotPlayer");
            Require(SUCCEEDED(n.install()) && SUCCEEDED(p.remove()), "reinstall native then remove PotPlayer");
            Require(Read(classes, server(native)) == argv[1] && !Exists(classes, server(potplayer)) &&
                        !Exists(classes, category + Guid(potplayer)),
                    "PotPlayer removal preserves native");
            Require(SUCCEEDED(n.remove()), "remove final native variant");
            Require(!Exists(classes, server(nativePage)) && !Exists(classes, server(potPage)),
                    "settings pages removed");
        }
        puts("PASS independent install/uninstall, coexistence and DirectShow category registration");
    }
    catch (const std::exception& error)
    {
        printf("FAIL variants: %s\n", error.what());
        result = 1;
    }
    if (redirected[2])
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    if (redirected[1])
        RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
    if (redirected[0])
        RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
    for (HKEY key : {user, machine, classes, fixture})
        RegCloseKey(key);
    RegDeleteTreeW(HKEY_CURRENT_USER, fixturePath.c_str());
    mapper.Reset();
    CoUninitialize();
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        return Test(argc, argv);
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "FAIL test fixture: %s\n", error.what());
        return 1;
    }
}
