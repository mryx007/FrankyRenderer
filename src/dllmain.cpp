#include "Filter.h"
#include "Direct2DSettingsPage.h"
#include "LegacyPropertyPages.h"

template <class T> static CUnknown* WINAPI Create(IUnknown* outer, HRESULT* hr)
{
    *hr = S_OK;
    auto object = new (std::nothrow) T(outer, hr);
    if (!object)
        *hr = E_OUTOFMEMORY;
    return object;
}

static const AMOVIESETUP_MEDIATYPE types[] = {{&MEDIATYPE_Video, &GUID_NULL}};
static const AMOVIESETUP_PIN pins[] = {
    {const_cast<LPWSTR>(L"Input"), FALSE, FALSE, FALSE, FALSE, &CLSID_NULL, nullptr, 1, types}};
static const AMOVIESETUP_FILTER filter = {
    &CLSID_RegisteredRenderer, RendererFilterName, MERIT_DO_NOT_USE, 1, pins, CLSID_LegacyAmFilterCategory};
CFactoryTemplate g_Templates[] = {
    {L"FrankyRenderer Settings", &CLSID_FrankyDirect2DPage, Create<Direct2DSettingsPage>, nullptr, nullptr},
    {RendererFilterName, &CLSID_RegisteredRenderer, Create<Renderer>, nullptr, &filter}};

int g_cTemplates = static_cast<int>(std::size(g_Templates));

static HRESULT RemoveObsoleteFilterCategory()
{
    // Earlier builds omitted the explicit category required by these BaseClasses
    // and registered under GUID_NULL. MPC-BE enumerates LegacyAmFilterCategory.
    const HRESULT initialized = CoInitialize(nullptr);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return initialized;
    IFilterMapper2* mapper = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FilterMapper2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mapper));
    if (SUCCEEDED(hr))
    {
        hr = mapper->UnregisterFilter(&GUID_NULL, nullptr, CLSID_RegisteredRenderer);
        mapper->Release();
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
            hr = S_OK;
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (SUCCEEDED(hr))
    {
        wchar_t module[32768];
        const auto length = GetModuleFileNameW(g_hInst, module, DWORD(std::size(module)));
        if (!length || length >= std::size(module))
            return E_FAIL;
        hr = LegacyPropertyPages::RemoveOwned(HKEY_CLASSES_ROOT, module);
    }
    return hr;
}

STDAPI DllRegisterServer()
{
    const HRESULT hr = AMovieDllRegisterServer2(TRUE);
    return FAILED(hr) ? hr : RemoveObsoleteFilterCategory();
}

STDAPI DllUnregisterServer()
{
    const HRESULT hr = AMovieDllRegisterServer2(FALSE);
    return FAILED(hr) ? hr : RemoveObsoleteFilterCategory();
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return DllEntryPoint(instance, reason, reserved);
}
