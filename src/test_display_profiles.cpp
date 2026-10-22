#define NOMINMAX
#include "SettingsIni.h"
#include "LiveSettings.h"
#include <cstdio>
#include <stdexcept>

static void Check(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static DisplayProfiles::Device Device(const wchar_t* name, const wchar_t* identity)
{
    return {DisplayProfiles::Hash(identity), name, identity, nullptr, true};
}

static std::vector<BYTE> Edid(unsigned serial)
{
    std::vector<BYTE> bytes(128);
    const BYTE magic[] = {0, 255, 255, 255, 255, 255, 255, 0};
    std::copy(std::begin(magic), std::end(magic), bytes.begin());
    bytes[8] = 10;
    bytes[9] = 20;
    bytes[10] = 30;
    bytes[11] = 40;
    for (int i = 0; i < 4; ++i)
        bytes[12 + i] = BYTE(serial >> (8 * i));
    unsigned checksum = 0;
    for (int i = 0; i < 127; ++i)
        checksum += bytes[i];
    bytes[127] = BYTE(0 - checksum);
    return bytes;
}

class ProfileConfig final : public IPlaceboSettings
{
    ULONG references = 1;

  public:
    PlaceboSettings current;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != __uuidof(IPlaceboSettings))
            return E_NOINTERFACE;
        *out = static_cast<IPlaceboSettings*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++references;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto left = --references;
        if (!left)
            delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE GetSettings(PlaceboSettings* out) override
    {
        if (!out)
            return E_POINTER;
        *out = current;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetSettings(const PlaceboSettings* next) override
    {
        if (!next)
            return E_POINTER;
        const auto result = SaveSettings(*next);
        if (SUCCEEDED(result) && !wcscmp(next->displayProfile, current.displayProfile))
            current = *next;
        return result;
    }

    HRESULT STDMETHODCALLTYPE GetStatus(PlaceboStatus*) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE ReadPixels(int, int, BYTE*, SIZE_T) override
    {
        return E_NOTIMPL;
    }
};

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc > 1 && !wcscmp(argv[1], L"--list"))
        {
            for (const auto& device : DisplayProfiles::Connected())
                wprintf(L"Connected: %ls; identity=%ls; monitor=%p\n",
                        device.name.c_str(),
                        device.identity.c_str(),
                        device.monitor);
            puts("Read-only display discovery; no profiles created.");
            return 0;
        }
        Check(GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0) > 0,
              "isolated registry root required");
        auto serial = Edid(123456);
        Check(DisplayProfiles::Identity(serial, L"port A") == DisplayProfiles::Identity(serial, L"port B"),
              "serial survives port changes");
        Check(DisplayProfiles::Identity(Edid(123457), L"port A") != DisplayProfiles::Identity(serial, L"port A"),
              "different serials separate devices");
        Check(DisplayProfiles::Identity(Edid(0), L"port A") != DisplayProfiles::Identity(Edid(0), L"port B"),
              "no serial uses path without merging devices");
        serial[30] ^= 1;
        Check(DisplayProfiles::Identity(serial, L"port A") == DisplayProfiles::CanonicalPath(L"port A"),
              "bad EDID checksum falls back to path");
        auto monitor = Device(L"Monitor", L"test:monitor"), tv = Device(L"TV", L"test:tv"),
             newDevice = Device(L"New display", L"test:new");
        Check(DisplayProfiles::ValidId(monitor.id) && monitor.id != tv.id, "safe unique profile keys");
        Check(!DisplayProfiles::ValidId(L"..\\other"), "profile path traversal rejected");
        PlaceboSettings legacy;
        legacy.brightness = .123f;
        legacy.smoothMotion = TRUE;
        legacy.hdrMode = 1;
        legacy.hdrPassthrough = TRUE;
        legacy.userShaders.shaders[0].enabled = TRUE;
        legacy.userShaders.shaders[0].overrides = 1;
        legacy.userShaders.shaders[0].values[0] = .37f;
        legacy.scaling.antiringing = .41f;
        legacy.tonal.exposure[2] = .5f;
        Check(SUCCEEDED(SaveSettings(legacy)), "legacy setup");
        Check(SUCCEEDED(EnsureDisplayProfiles({monitor, tv}, legacy)), "first migration");
        auto a = LoadSettings(monitor.id), b = LoadSettings(tv.id);
        Check(SettingsIni::Serialize(a) == SettingsIni::Serialize(legacy) &&
                  SettingsIni::Serialize(b) == SettingsIni::Serialize(legacy),
              "initial attached displays retain all legacy values");
        Check(a.displayProfile == monitor.id && b.displayProfile == tv.id, "context is separate from values");
        a.brightness = .25f;
        a.smoothMotion = FALSE;
        a.hdrMode = 2;
        a.hdrPassthrough = FALSE;
        a.userShaders.shaders[0].enabled = FALSE;
        a.scaling.antiringing = .11f;
        b.brightness = -.25f;
        b.extras.values[0] = .7f;
        wcscpy_s(b.lutPath, L"C:\\Calibration\\TV.cube");
        Check(SUCCEEDED(SaveSettings(a)) && SUCCEEDED(SaveSettings(b)), "save independent full profiles");
        Check(SettingsIni::Serialize(LoadSettings(monitor.id)) == SettingsIni::Serialize(a),
              "monitor settings retained");
        Check(SettingsIni::Serialize(LoadSettings(tv.id)) == SettingsIni::Serialize(b), "TV settings retained");
        Check(SettingsIni::Serialize(LoadSettings()) == SettingsIni::Serialize(legacy),
              "legacy global values unchanged");
        Check(SUCCEEDED(EnsureDisplayProfiles({monitor, tv, newDevice}, a)), "discover genuinely new device");
        Check(SettingsIni::Serialize(LoadSettings(newDevice.id)) == SettingsIni::Serialize(PlaceboSettings{}),
              "new device uses factory defaults, not another display's settings");
        Check(SUCCEEDED(EnsureDisplayProfiles({monitor}, a)) && SUCCEEDED(EnsureDisplayProfiles({tv}, a)),
              "disconnect and reconnect");
        Check(SettingsIni::Serialize(LoadSettings(tv.id)) == SettingsIni::Serialize(b),
              "reconnect preserves TV profile");
        Check(SUCCEEDED(DisplayProfiles::Rename(tv.id, L"  Wohnzimmer-TV  ")), "rename display");
        Check(FAILED(DisplayProfiles::Rename(tv.id, L"  ")) &&
                  FAILED(DisplayProfiles::Rename(tv.id, std::wstring(81, L'x'))),
              "invalid names rejected");
        Check(SUCCEEDED(EnsureDisplayProfiles({tv}, a)), "discovery refresh after rename");
        auto all = DisplayProfiles::All();
        auto found = std::find_if(all.begin(), all.end(), [&](const auto& d) { return d.id == tv.id; });
        Check(found != all.end() && found->name == L"Wohnzimmer-TV" && !found->connected,
              "custom name survives detection; disconnected display stays listed");
        Check(SettingsIni::Serialize(LoadSettings(tv.id)) == SettingsIni::Serialize(b),
              "rename does not change picture values");
        auto imported = SettingsIni::Parse(SettingsIni::Serialize(a));
        Check(!imported.displayProfile[0], "INI remains portable without device identity");
        auto config = new ProfileConfig;
        config->current = a;
        HWND host = CreateWindowW(L"STATIC",
                                  L"profile session",
                                  WS_POPUP,
                                  0,
                                  0,
                                  300,
                                  300,
                                  nullptr,
                                  nullptr,
                                  GetModuleHandleW(nullptr),
                                  nullptr);
        HWND page = CreateWindowW(
            L"STATIC", L"profile page", WS_CHILD, 0, 0, 200, 200, host, nullptr, GetModuleHandleW(nullptr), nullptr);
        LiveSettings::Attach(page, config, [] { return S_OK; });
        LiveSettings::RememberDisplay(page, b);
        auto changedA = a, changedB = b;
        changedA.brightness = .5f;
        changedB.brightness = -.5f;
        Check(SUCCEEDED(config->SetSettings(&changedA)) && SUCCEEDED(config->SetSettings(&changedB)),
              "multi-device preview");
        SendMessageW(host, WM_COMMAND, IDCANCEL, 0);
        DestroyWindow(host);
        Check(SettingsIni::Serialize(LoadSettings(monitor.id)) == SettingsIni::Serialize(a) &&
                  SettingsIni::Serialize(LoadSettings(tv.id)) == SettingsIni::Serialize(b),
              "Cancel restores all edited display profiles");
        config->Release();
        PlaceboSettings invalid;
        wcscpy_s(invalid.displayProfile, L"..\\other");
        Check(FAILED(SaveSettings(invalid)), "invalid profile key cannot write outside device root");
        puts(
            "PASS display profiles: stable serial identity, path fallback, initial migration, all settings separated, factory defaults for new devices, reconnect, custom names, portable INI and key validation");
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("FAIL display profiles: %s\n", e.what());
        return 1;
    }
}
