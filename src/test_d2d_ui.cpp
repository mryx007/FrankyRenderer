#include <windows.h>
#include <dwmapi.h>
#include "Direct2DSettingsHost.h"
#include "Settings.h"
#include <cstdio>

#pragma comment(lib, "dwmapi.lib")

static HWND g_hD2DView = nullptr;
static PlaceboSettings g_settings;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SETFOCUS:
        if (g_hD2DView) SetFocus(g_hD2DView);
        return 0;
    case WM_SIZE: {
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        Direct2DSettingsHost::Resize(g_hD2DView, clientRect);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        Direct2DSettingsHost::Destroy(g_hD2DView);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    g_settings = LoadSettings();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FrankyModernSettingsD2DWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 16, 18));
    RegisterClassExW(&wc);

    const int width = 1040;
    const int height = 760;

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const int posX = (screenW - width) / 2;
    const int posY = (screenH - height) / 2;

    HWND hwndMain = CreateWindowExW(WS_EX_APPWINDOW, L"FrankyModernSettingsD2DWnd",
                                    L"FrankyRenderer — Direct2D Native Settings (No WebView / Pure C++)",
                                    WS_OVERLAPPEDWINDOW,
                                    posX, posY, width, height,
                                    nullptr, nullptr, hInstance, nullptr);

    if (!hwndMain) return 1;

    // Enable Windows 10 (1809+) & Windows 11 Immersive Dark Mode
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(hwndMain, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));
    DwmSetWindowAttribute(hwndMain, 19 /* fallback for Windows 10 1809 */, &darkMode, sizeof(darkMode));

    COLORREF captionColor = RGB(21, 22, 24); // #151618 (matches neutral graphite sidebar)
    DwmSetWindowAttribute(hwndMain, 35 /* DWMWA_CAPTION_COLOR */, &captionColor, sizeof(captionColor));

    COLORREF textColor = RGB(241, 245, 249); // #f1f5f9
    DwmSetWindowAttribute(hwndMain, 36 /* DWMWA_TEXT_COLOR */, &textColor, sizeof(textColor));

    COLORREF borderColor = RGB(39, 40, 45); // #27282d
    DwmSetWindowAttribute(hwndMain, 34 /* DWMWA_BORDER_COLOR */, &borderColor, sizeof(borderColor));

    SetWindowPos(hwndMain, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    RECT clientRect;
    GetClientRect(hwndMain, &clientRect);
    g_hD2DView = Direct2DSettingsHost::Create(hwndMain, clientRect, &g_settings, true);

    ShowWindow(hwndMain, SW_SHOWNORMAL);
    UpdateWindow(hwndMain);
    SetForegroundWindow(hwndMain);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}
