#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <windowsx.h>
#pragma comment(lib, "dwmapi.lib")

// Restyle the existing modal property frame. The host still owns OK/Cancel,
// keyboard handling and the live-preview transaction.
class Direct2DFrame
{
    HWND host = nullptr, page = nullptr, tab = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(15, 16, 18));
    std::wstring originalTitle;
    LONG_PTR originalExStyle = 0, originalStyle = 0;
    HWND closeButton = nullptr, titleLabel = nullptr;
    bool closeHovered = false;
    HFONT titleFont = nullptr;
    static constexpr int CloseId = 0x7f10;

    struct Button
    {
        HWND window;
        LONG_PTR style;
        RECT rect;
    };

    std::vector<Button> buttons;
    bool layout = false, attaching = false;

    static LRESULT CALLBACK CloseProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        if (msg == WM_MOUSEMOVE && !self->closeHovered)
        {
            self->closeHovered = true;
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, h, 0};
            TrackMouseEvent(&track);
            InvalidateRect(h, nullptr, FALSE);
        }
        if (msg == WM_MOUSELEAVE || msg == WM_CANCELMODE)
        {
            self->closeHovered = false;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(h, CloseProc, 304);
        return DefSubclassProc(h, msg, w, l);
    }

    SIZE preferred = {};

    void SizeToContent()
    {
        if (!host || preferred.cx <= 0 || preferred.cy <= 0)
            return;
        const UINT dpi = GetDpiForWindow(host);
        RECT outer = {0, 0, preferred.cx, preferred.cy + MulDiv(84, dpi, 96)};
        AdjustWindowRectExForDpi(
            &outer, DWORD(GetWindowLongPtrW(host, GWL_STYLE)), FALSE, DWORD(GetWindowLongPtrW(host, GWL_EXSTYLE)), dpi);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT work = monitor.rcWork;
        const int width = std::min(int(outer.right - outer.left), int(work.right - work.left));
        const int height = std::min(int(outer.bottom - outer.top), int(work.bottom - work.top));
        RECT current;
        GetWindowRect(host, &current);
        const int x = std::clamp(int(current.left), int(work.left), int(work.right) - width),
                  y = std::clamp(int(current.top), int(work.top), int(work.bottom) - height);
        SetWindowPos(host, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Layout()
    {
        if (layout || !host || !IsWindow(page))
            return;
        layout = true;
        RECT client;
        GetClientRect(host, &client);
        const int dpi = int(GetDpiForWindow(host));
        auto px = [dpi](int n) { return MulDiv(n, dpi, 96); };
        const int footer = px(48), header = px(36), margin = px(8),
                  height = std::max(0, int(client.bottom) - footer - header);
        std::vector<HWND> containers;
        for (HWND child = page; child && child != host; child = GetParent(child))
            containers.push_back(child);
        for (auto i = containers.rbegin(); i != containers.rend(); ++i)
        {
            const int top = GetParent(*i) == host ? header : 0;
            RECT current;
            GetWindowRect(*i, &current);
            MapWindowPoints(nullptr, GetParent(*i), reinterpret_cast<POINT*>(&current), 2);
            if (current.left != 0 || current.top != top || current.right != client.right ||
                current.bottom != top + height)
                SetWindowPos(*i, nullptr, 0, top, client.right, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (closeButton)
            SetWindowPos(closeButton, nullptr, client.right - px(48), 0, px(48), header, SWP_NOZORDER | SWP_NOACTIVATE);
        if (titleLabel)
            SetWindowPos(titleLabel,
                         nullptr,
                         px(16),
                         px(8),
                         std::max(0, int(client.right) - px(80)),
                         px(24),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        int right = client.right - px(36);
        for (int id : {IDCANCEL, IDOK})
            if (HWND button = GetDlgItem(host, id))
            {
                const int width = px(88);
                SetWindowPos(button,
                             nullptr,
                             right - width,
                             header + height + px(8),
                             width,
                             px(30),
                             SWP_NOZORDER | SWP_NOACTIVATE);
                right -= width + margin;
            }
        if (tab)
            ShowWindow(tab, SW_HIDE);
        layout = false;
    }

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        if (msg == WM_COMMAND && LOWORD(w) == CloseId)
        {
            PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        }
        if (msg == WM_NCHITTEST)
        {
            auto hit = DefSubclassProc(h, msg, w, l);
            if (hit == HTCLIENT)
            {
                POINT point = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ScreenToClient(h, &point);
                RECT client;
                GetClientRect(h, &client);
                const int dpi = int(GetDpiForWindow(h));
                if (point.y >= 0 && point.y < MulDiv(36, dpi, 96) && point.x < client.right - MulDiv(48, dpi, 96))
                    return HTCAPTION;
            }
            return hit;
        }
        if (msg == WM_ERASEBKGND)
        {
            RECT r;
            GetClientRect(h, &r);
            FillRect(reinterpret_cast<HDC>(w), &r, self->background);
            return 1;
        }
        if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC)
        {
            SetBkColor(reinterpret_cast<HDC>(w), RGB(15, 16, 18));
            SetTextColor(reinterpret_cast<HDC>(w), RGB(235, 237, 240));
            return reinterpret_cast<LRESULT>(self->background);
        }
        if (msg == WM_DRAWITEM)
        {
            auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (draw && draw->CtlType == ODT_BUTTON && draw->CtlID == CloseId)
            {
                HBRUSH hover = nullptr;
                if (self->closeHovered || (draw->itemState & ODS_SELECTED))
                    hover = CreateSolidBrush(draw->itemState & ODS_SELECTED ? RGB(60, 64, 72) : RGB(36, 38, 43));
                FillRect(draw->hDC, &draw->rcItem, hover ? hover : self->background);
                if (hover)
                    DeleteObject(hover);
                const int dpi = int(GetDpiForWindow(h));
                const int radius = MulDiv(7, dpi, 96), cx = (draw->rcItem.left + draw->rcItem.right) / 2,
                          cy = (draw->rcItem.top + draw->rcItem.bottom) / 2;
                HPEN pen = CreatePen(PS_SOLID, std::max(2, MulDiv(2, dpi, 96)), RGB(225, 229, 235));
                auto old = SelectObject(draw->hDC, pen);
                MoveToEx(draw->hDC, cx - radius, cy - radius, nullptr);
                LineTo(draw->hDC, cx + radius, cy + radius);
                MoveToEx(draw->hDC, cx + radius, cy - radius, nullptr);
                LineTo(draw->hDC, cx - radius, cy + radius);
                SelectObject(draw->hDC, old);
                DeleteObject(pen);
                return TRUE;
            }
            if (draw && draw->CtlType == ODT_BUTTON && (draw->CtlID == IDOK || draw->CtlID == IDCANCEL))
            {
                const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
                FillRect(draw->hDC, &draw->rcItem, self->background);
                HBRUSH fill = CreateSolidBrush(pressed ? RGB(55, 59, 66) : RGB(36, 38, 43));
                const int diameter = MulDiv(12, int(GetDpiForWindow(h)), 96);
                HRGN shape = CreateRoundRectRgn(draw->rcItem.left,
                                                draw->rcItem.top,
                                                draw->rcItem.right + 1,
                                                draw->rcItem.bottom + 1,
                                                diameter,
                                                diameter);
                if (shape)
                {
                    FillRgn(draw->hDC, shape, fill);
                    DeleteObject(shape);
                }
                DeleteObject(fill);
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, RGB(241, 245, 249));
                wchar_t label[80];
                GetWindowTextW(draw->hwndItem, label, 80);
                auto font = SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0);
                auto old = SelectObject(draw->hDC, reinterpret_cast<HGDIOBJ>(font));
                auto r = draw->rcItem;
                DrawTextW(draw->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(draw->hDC, old);
                if (draw->itemState & ODS_FOCUS)
                {
                    InflateRect(&r, -3, -3);
                    DrawFocusRect(draw->hDC, &r);
                }
                return TRUE;
            }
        }
        const auto result = DefSubclassProc(h, msg, w, l);
        if (msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED || msg == WM_SHOWWINDOW || msg == WM_DPICHANGED)
            self->Layout();
        if (msg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(h, Proc, 303);
            self->host = nullptr;
        }
        return result;
    }

  public:
    void Refresh()
    {
        Layout();
    }

    void Attach(HWND pageWindow, SIZE contentSize = {})
    {
        if (host || attaching)
            return;
        page = pageWindow;
        preferred = contentSize;
        HWND candidate = GetParent(page), tabs = nullptr;
        // OLE can put an intermediate page container below its frame. The
        // frame class and the order in which it creates controls vary by host.
        for (; candidate; candidate = GetParent(candidate))
        {
            tabs = FindWindowExW(candidate, nullptr, WC_TABCONTROLW, nullptr);
            if (tabs && TabCtrl_GetItemCount(tabs) == 1 && GetDlgItem(candidate, IDOK) &&
                GetDlgItem(candidate, IDCANCEL))
                break;
        }
        if (!candidate)
            return;
        attaching = true;
        host = candidate;
        tab = tabs;
        wchar_t title[512];
        GetWindowTextW(host, title, 512);
        originalTitle = title;
        if (!SetWindowSubclass(host, Proc, 303, reinterpret_cast<DWORD_PTR>(this)))
        {
            host = nullptr;
            attaching = false;
            return;
        }
        originalExStyle = GetWindowLongPtrW(host, GWL_EXSTYLE);
        originalStyle = GetWindowLongPtrW(host, GWL_STYLE);
        SetWindowLongPtrW(host, GWL_STYLE, originalStyle & ~WS_CAPTION);
        SetWindowLongPtrW(host,
                          GWL_EXSTYLE,
                          originalExStyle & ~(WS_EX_TOOLWINDOW | WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE |
                                              WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
        closeButton = CreateWindowExW(0,
                                      L"BUTTON",
                                      L"Close",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      0,
                                      0,
                                      48,
                                      36,
                                      host,
                                      reinterpret_cast<HMENU>(INT_PTR(CloseId)),
                                      nullptr,
                                      nullptr);
        if (closeButton)
            SetWindowSubclass(closeButton, CloseProc, 304, reinterpret_cast<DWORD_PTR>(this));
        titleLabel = CreateWindowExW(WS_EX_TRANSPARENT,
                                     L"STATIC",
                                     L"FrankyRenderer",
                                     WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     16,
                                     8,
                                     240,
                                     24,
                                     host,
                                     nullptr,
                                     nullptr,
                                     nullptr);
        titleFont = CreateFontW(-MulDiv(14, int(GetDpiForWindow(host)), 96),
                                0,
                                0,
                                0,
                                FW_SEMIBOLD,
                                FALSE,
                                FALSE,
                                FALSE,
                                DEFAULT_CHARSET,
                                0,
                                0,
                                CLEARTYPE_QUALITY,
                                0,
                                L"Segoe UI");
        SendMessageW(titleLabel, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        SetWindowPos(
            host, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        for (int id : {IDOK, IDCANCEL})
        {
            HWND button = GetDlgItem(host, id);
            RECT rect;
            GetWindowRect(button, &rect);
            MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&rect), 2);
            const auto style = GetWindowLongPtrW(button, GWL_STYLE);
            buttons.push_back({button, style, rect});
            SetWindowLongPtrW(button, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
        }
        BOOL dark = TRUE;
        DwmSetWindowAttribute(host, 20, &dark, sizeof(dark));
        const COLORREF color = RGB(15, 16, 18);
        DwmSetWindowAttribute(host, 35, &color, sizeof(color));
        const COLORREF border = RGB(39, 40, 45);
        DwmSetWindowAttribute(host, 34, &border, sizeof(border));
        SetWindowTextW(host, L"FrankyRenderer");
        SizeToContent();
        Layout();
        InvalidateRect(host, nullptr, TRUE);
        attaching = false;
    }

    void Detach()
    {
        if (IsWindow(closeButton))
            DestroyWindow(closeButton);
        if (IsWindow(titleLabel))
            DestroyWindow(titleLabel);
        closeButton = titleLabel = nullptr;
        if (titleFont)
        {
            DeleteObject(titleFont);
            titleFont = nullptr;
        }
        if (host && IsWindow(host))
        {
            SetWindowLongPtrW(host, GWL_EXSTYLE, originalExStyle);
            SetWindowLongPtrW(host, GWL_STYLE, originalStyle);
        }
        if (host && IsWindow(host))
        {
            RemoveWindowSubclass(host, Proc, 303);
            for (const auto& b : buttons)
                if (IsWindow(b.window))
                {
                    SetWindowLongPtrW(b.window, GWL_STYLE, b.style);
                    SetWindowPos(b.window,
                                 nullptr,
                                 b.rect.left,
                                 b.rect.top,
                                 b.rect.right - b.rect.left,
                                 b.rect.bottom - b.rect.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
            if (IsWindow(tab))
                ShowWindow(tab, SW_SHOW);
            SetWindowTextW(host, originalTitle.c_str());
        }
        host = nullptr;
        page = nullptr;
        buttons.clear();
    }

    ~Direct2DFrame()
    {
        Detach();
        DeleteObject(background);
    }
};
