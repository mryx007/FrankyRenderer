#include "Filter.h"
#include "PropertyPage.h"
#include "ScalingPage.h"
#include "ColorMappingPage.h"
#include "ShaderPage.h"
#include "Direct2DSettingsPage.h"

template <class T> static CUnknown* WINAPI Create(IUnknown* outer, HRESULT* hr)
{
    *hr = S_OK;
    auto object = new (std::nothrow) T(outer, hr);
    if (!object)
        *hr = E_OUTOFMEMORY;
    return object;
}

static const GUID subtypeNV12 = {0x3231564e, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}},
                  subtypeP010 = {0x30313050, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}},
                  subtypeP016 = {0x36313050, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
static const AMOVIESETUP_MEDIATYPE types[] = {{&MEDIATYPE_Video, &GUID_NULL}};
static const AMOVIESETUP_PIN pins[] = {
    {const_cast<LPWSTR>(L"Input"), FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, 1, types}};
static const AMOVIESETUP_FILTER filter = {&CLSID_MPCCompatible, L"FrankyRenderer", MERIT_DO_NOT_USE, 1, pins};
CFactoryTemplate g_Templates[] = {
    {L"FrankyRenderer Settings", &CLSID_FrankyDirect2DPage, Create<Direct2DSettingsPage>, nullptr, nullptr},
    {L"FrankyRenderer Color Mapping", &CLSID_PlaceboMappingPage, Create<ColorMappingPage>, nullptr, nullptr},
    {L"FrankyRenderer Color Adjustment", &CLSID_PlaceboAdjustmentPage, Create<AdjustmentPage>, nullptr, nullptr},
    {L"FrankyRenderer Debanding", &CLSID_PlaceboDebandingPage, Create<DebandingPage>, nullptr, nullptr},
    {L"FrankyRenderer Scaling", &CLSID_PlaceboScalingPage, Create<ScalingPage>, nullptr, nullptr},
    {L"FrankyRenderer", &CLSID_MPCCompatible, Create<Renderer>, nullptr, &filter},
    {L"FrankyRenderer", &CLSID_PlaceboRenderer, Create<Renderer>, nullptr, nullptr},
    {L"FrankyRenderer Settings", &CLSID_PlaceboSettingsPage, Create<SettingsPage>, nullptr, nullptr},
    {L"FrankyRenderer Processing", &CLSID_PlaceboProcessingPage, Create<ProcessingPage>, nullptr, nullptr},
    {L"FrankyRenderer Color", &CLSID_PlaceboColorPage, Create<ColorPage>, nullptr, nullptr},
    {L"FrankyRenderer HDR details", &CLSID_PlaceboHdrPage, Create<HdrPage>, nullptr, nullptr},
    {L"FrankyRenderer Shaders", &CLSID_PlaceboEffectsPage, Create<ShaderPage>, nullptr, nullptr}};
int g_cTemplates = static_cast<int>(std::size(g_Templates));

STDAPI DllRegisterServer()
{
    return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer()
{
    return AMovieDllRegisterServer2(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return DllEntryPoint(instance, reason, reserved);
}
