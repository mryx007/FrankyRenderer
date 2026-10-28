#include "Settings.h"
#include <ocidl.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <string>
#include "Direct2DFrame.h"
using Microsoft::WRL::ComPtr;

static void Check(HRESULT hr, const char* label)
{
    if (FAILED(hr))
    {
        printf("FAIL %s: %08X\n", label, unsigned(hr));
        exit(1);
    }
}

class SettingsMock final : public IPlaceboSettings
{
    std::atomic<ULONG> references = 1;

  public:
    PlaceboSettings settings;
    unsigned writes = 0;

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
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

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++references;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        auto n = --references;
        if (!n)
            delete this;
        return n;
    }

    STDMETHODIMP GetSettings(PlaceboSettings* out) override
    {
        if (!out)
            return E_POINTER;
        *out = settings;
        return S_OK;
    }

    STDMETHODIMP SetSettings(const PlaceboSettings* in) override
    {
        if (!in)
            return E_POINTER;
        settings = *in;
        writes++;
        return S_OK;
    }

    STDMETHODIMP GetStatus(PlaceboStatus*) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP ReadPixels(int, int, BYTE*, SIZE_T) override
    {
        return E_NOTIMPL;
    }
};

class Site final : public IPropertyPageSite
{
    std::atomic<ULONG> references = 1;

  public:
    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IPropertyPageSite)
            return E_NOINTERFACE;
        *out = static_cast<IPropertyPageSite*>(this);
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++references;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        auto n = --references;
        if (!n)
            delete this;
        return n;
    }

    STDMETHODIMP OnStatusChange(DWORD) override
    {
        return S_OK;
    }

    STDMETHODIMP GetLocaleID(LCID* out) override
    {
        if (!out)
            return E_POINTER;
        *out = 0x409;
        return S_OK;
    }

    STDMETHODIMP GetPageContainer(IUnknown** out) override
    {
        if (out)
            *out = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP TranslateAccelerator(MSG*) override
    {
        return S_FALSE;
    }
};

int wmain(int argc, wchar_t** argv)
{
    Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "COM");
    {
        INITCOMMONCONTROLSEX common = {sizeof(common), ICC_TAB_CLASSES};
        InitCommonControlsEx(&common);

        struct Template
        {
            DLGTEMPLATE dialog;
            WORD menu, cls, title;
        } resource = {};

        resource.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        resource.dialog.cx = 600;
        resource.dialog.cy = 400;
        HWND host = CreateDialogIndirectParamW(
            GetModuleHandleW(nullptr),
            &resource.dialog,
            nullptr,
            [](HWND window, UINT message, WPARAM, LPARAM data) -> INT_PTR
            {
                if (message == WM_NOTIFY && reinterpret_cast<NMHDR*>(data)->code == TCN_SELCHANGING)
                {
                    auto frame = reinterpret_cast<Direct2DFrame*>(GetWindowLongPtrW(window, DWLP_USER));
                    if (frame)
                        frame->Detach();
                }
                return FALSE;
            },
            0);
        if (!host)
            return 10;
        SetWindowLongPtrW(host,
                          GWL_EXSTYLE,
                          GetWindowLongPtrW(host, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME |
                              WS_EX_CLIENTEDGE);
        HWND wrapper = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 15, 40, 850, 620, host, nullptr, nullptr, nullptr);
        HWND child = CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 8, 8, 800, 600, wrapper, nullptr, nullptr, nullptr);
        Direct2DFrame frame;
        frame.Attach(child); // Controls do not exist yet.
        HWND tabs = CreateWindowExW(
            0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE, 10, 10, 900, 650, host, nullptr, nullptr, nullptr);
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"FrankyRenderer");
        TabCtrl_InsertItem(tabs, 0, &item);
        for (int id : {IDOK, IDCANCEL})
            CreateWindowExW(0,
                            L"BUTTON",
                            id == IDOK ? L"OK" : L"Cancel",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            10,
                            680,
                            80,
                            30,
                            host,
                            reinterpret_cast<HMENU>(INT_PTR(id)),
                            nullptr,
                            nullptr);
        SetWindowPos(host, nullptr, 0, 0, 380, 310, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        frame.Attach(child, SIZE{1040, 720});
        RECT r;
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2);
        RECT client;
        GetClientRect(host, &client);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor);
        RECT frameBounds;
        GetWindowRect(host, &frameBounds);
        const UINT dpi = GetDpiForWindow(host);
        if (client.right < 640 || client.bottom < 400 || client.right > MulDiv(960, dpi, 96) ||
            frameBounds.right - frameBounds.left > (monitor.rcWork.right - monitor.rcWork.left) * 9 / 10 ||
            frameBounds.bottom - frameBounds.top > (monitor.rcWork.bottom - monitor.rcWork.top) * 9 / 10)
            return 15;
        if (GetWindowLongPtrW(host, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
            return 17;
        if (GetWindowLongPtrW(host, GWL_EXSTYLE) &
            (WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_STATICEDGE))
            return 19;
        if ((GetWindowLongPtrW(tabs, GWL_STYLE) & WS_VISIBLE) || r.left ||
            r.top != MulDiv(36, int(GetDpiForWindow(host)), 96) || r.right != client.right || r.bottom >= client.bottom)
            return 11;
        HWND close = GetDlgItem(host, 0x7f10);
        RECT closeBounds;
        GetClientRect(close, &closeBounds);
        if (!close || closeBounds.right < MulDiv(48, int(GetDpiForWindow(host)), 96))
            return 18;
        {
            HDC dc = CreateCompatibleDC(nullptr);
            BITMAPINFO bitmap = {};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = closeBounds.right;
            bitmap.bmiHeader.biHeight = -closeBounds.bottom;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            bitmap.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            HBITMAP image = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
            auto old = SelectObject(dc, image);
            DRAWITEMSTRUCT draw = {};
            draw.CtlType = ODT_BUTTON;
            draw.CtlID = 0x7f10;
            draw.hwndItem = close;
            draw.hDC = dc;
            draw.rcItem = closeBounds;
            SendMessageW(host, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
            if (GetPixel(dc, 1, 1) != RGB(15, 16, 18))
                return 20;
            SendMessageW(close, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
            SendMessageW(host, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
            if (GetPixel(dc, 1, 1) != RGB(36, 38, 43))
                return 21;
            draw.itemState = ODS_SELECTED;
            SendMessageW(host, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
            if (GetPixel(dc, 1, 1) != RGB(60, 64, 72))
                return 22;
            draw.itemState = 0;
            SendMessageW(close, WM_MOUSELEAVE, 0, 0);
            SendMessageW(host, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
            if (GetPixel(dc, 1, 1) != RGB(15, 16, 18))
                return 23;
            SelectObject(dc, old);
            DeleteObject(image);
            DeleteDC(dc);
            puts("PASS neutral gray close-button hover, press and mouse-leave colors");
        }
        wchar_t title[80];
        GetWindowTextW(host, title, 80);
        if (wcscmp(title, L"FrankyRenderer"))
            return 12;
        if ((GetWindowLongPtrW(GetDlgItem(host, IDOK), GWL_STYLE) & BS_TYPEMASK) != BS_OWNERDRAW)
            return 13;
        RECT cancel;
        GetWindowRect(GetDlgItem(host, IDCANCEL), &cancel);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&cancel), 2);
        if (cancel.right != client.right - MulDiv(36, int(GetDpiForWindow(host)), 96))
            return 16;
        SetWindowPos(child, nullptr, 15, 40, 800, 600, SWP_NOZORDER | SWP_NOACTIVATE);
        frame.Refresh();
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2);
        if (r.left || r.top != MulDiv(36, int(GetDpiForWindow(host)), 96))
            return 14;
        frame.Detach();
        item.pszText = const_cast<wchar_t*>(L"Pin Info");
        TabCtrl_InsertItem(tabs, 1, &item);
        frame.Attach(child, SIZE{1040, 720});
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2);
        if ((GetWindowLongPtrW(tabs, GWL_STYLE) & WS_VISIBLE) || TabCtrl_GetItemCount(tabs) != 2 ||
            r.top != MulDiv(36, int(GetDpiForWindow(host)), 96) || (GetWindowLongPtrW(host, GWL_STYLE) & WS_CAPTION))
            return 23;
        RECT expected;
        GetWindowRect(host, &expected);
        TabCtrl_SetCurSel(tabs, 1);
        TabCtrl_SetCurFocus(tabs, 1);
        SendMessageW(tabs, WM_KEYDOWN, VK_RIGHT, 0);
        RECT pinTab;
        TabCtrl_GetItemRect(tabs, 1, &pinTab);
        const LPARAM point = MAKELPARAM((pinTab.left + pinTab.right) / 2, (pinTab.top + pinTab.bottom) / 2);
        SendMessageW(tabs, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(tabs, WM_LBUTTONUP, 0, point);
        if (TabCtrl_GetCurSel(tabs) != 0 || SendMessageW(host, PSM_SETCURSEL, 1, 0))
            return 24;
        NMHDR changing = {tabs, 0, TCN_SELCHANGING};
        if (!SendMessageW(host, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&changing)))
            return 25;
        for (int i = 0; i < 3; i++)
        {
            // Reproduce MPC-BE moving its property sheet after activation.
            SetWindowPos(host, nullptr, 0, 0, 560, 1400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            RECT actual;
            GetWindowRect(host, &actual);
            if (actual.right - actual.left != expected.right - expected.left ||
                actual.bottom - actual.top != expected.bottom - expected.top)
                return 26;
            GetWindowRect(child, &r);
            MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2);
            GetClientRect(host, &client);
            if (r.left || r.top != MulDiv(36, int(GetDpiForWindow(host)), 96) || r.right != client.right)
                return 27;
        }
        frame.Detach();
        TabCtrl_SetCurSel(tabs, 1);
        if (!(GetWindowLongPtrW(tabs, GWL_STYLE) & WS_VISIBLE) || TabCtrl_GetCurSel(tabs) != 1)
            return 28;
        DestroyWindow(host);
        puts("PASS modal frame: hidden single tab, edge-to-edge page, dark buttons, correct title and layout refresh");
        puts("PASS shared frame: extra tabs hidden, page navigation blocked, stable host sizing and clean detach");
    }
    if (argc > 1 && !wcscmp(argv[1], L"--registered"))
    {
        ComPtr<IPropertyPage> page;
        Check(CoCreateInstance(CLSID_FrankyDirect2DPage, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&page)),
              "registered Direct2D property page");
        puts("PASS registered Direct2D settings page");
        return 0;
    }
    wchar_t exe[32768];
    GetModuleFileNameW(nullptr, exe, 32768);
    std::wstring path = exe;
    path.resize(path.find_last_of(L'\\') + 1);
    path += L"FrankyRenderer.ax";
    for (int i = 1; i + 1 < argc; ++i)
        if (!wcscmp(argv[i], L"--dll"))
            path = argv[++i];
    for (int repeat = 0; repeat < 3; repeat++)
    {
        HMODULE dll =
            LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll)
        {
            printf("FAIL load %lu\n", GetLastError());
            return 1;
        }
        using Getter = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
        auto getter = reinterpret_cast<Getter>(GetProcAddress(dll, "DllGetClassObject"));
        ComPtr<IClassFactory> factory;
        Check(getter(CLSID_FrankyDirect2DPage, IID_PPV_ARGS(&factory)), "page factory");
        ComPtr<IPropertyPage> page;
        Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&page)), "page object");
        ComPtr<SettingsMock> mock;
        mock.Attach(new SettingsMock);
        ComPtr<Site> site;
        site.Attach(new Site);
        Check(page->SetPageSite(site.Get()), "page site");
        IUnknown* object = mock.Get();
        Check(page->SetObjects(1, &object), "connect");
        PROPPAGEINFO info = {sizeof(info)};
        Check(page->GetPageInfo(&info), "resource and title");
        if (!info.pszTitle || info.size.cx < 500)
            return 2;
        CoTaskMemFree(info.pszTitle);
        CoTaskMemFree(info.pszDocString);
        CoTaskMemFree(info.pszHelpFile);
        HWND parent = CreateWindowExW(0,
                                      L"STATIC",
                                      L"Hidden Direct2D integration test",
                                      WS_POPUP,
                                      -10000,
                                      -10000,
                                      1100,
                                      800,
                                      nullptr,
                                      nullptr,
                                      GetModuleHandleW(nullptr),
                                      nullptr);
        if (!parent)
            return 3;
        RECT bounds = {0, 0, 1100, 800};
        Check(page->Activate(parent, &bounds, FALSE), "activate hidden page");
        HWND dialog = GetWindow(parent, GW_CHILD);
        HWND view = FindWindowExW(dialog, nullptr, L"FrankyDirect2DSettingsHost", nullptr);
        if (!view)
            return 4;
        // Closing an uncommitted host must restore the captured opening state.
        auto changed = mock->settings;
        changed.brightness = .125f;
        Check(mock->SetSettings(&changed), "simulate settings preview through interface");
        Check(page->Deactivate(), "deactivate");
        DestroyWindow(parent);
        if (mock->settings.brightness != 0 || mock->writes < 2)
        {
            puts("FAIL cancel restoration");
            return 5;
        }
        Check(page->SetObjects(0, nullptr), "disconnect");
        Check(page->SetPageSite(nullptr), "detach site");
        page.Reset();
        factory.Reset();
        FreeLibrary(dll);
    }
    CoUninitialize();
    puts("PASS Direct2D page factory, resources, hidden activation, cancel restoration and repeated DLL unload/reload");
    return 0;
}
