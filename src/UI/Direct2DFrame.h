#pragma once
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <windowsx.h>
#include "Settings/SettingsLayout.h"
#include "UI/MenuTheme.h"
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

// Restyle the existing modal property frame. The host still owns OK/Cancel,
// keyboard handling and the live-preview transaction.
class Direct2DFrame
{
    HWND host = nullptr, page = nullptr, tab = nullptr;
    HWND opening = nullptr;
    HBRUSH background = nullptr;
    int paletteVersion = -1;

    // Frame background in the menu theme; rebuilt when the theme changes.
    HBRUSH Background()
    {
        if (paletteVersion != g_menuPaletteVersion || !background)
        {
            if (background)
                DeleteObject(background);
            background = CreateSolidBrush(MenuColorRef(CurrentMenuPalette().app));
            paletteVersion = g_menuPaletteVersion;
        }
        return background;
    }

    std::wstring originalTitle;
    LONG_PTR originalExStyle = 0, originalStyle = 0;
    HWND closeButton = nullptr, titleLabel = nullptr;
    HWND resizeGrip = nullptr;
    std::vector<HWND> resizeEdges;
    static constexpr int ResizeDirections[] = {
        HTBOTTOMRIGHT, HTLEFT, HTRIGHT, HTTOP, HTBOTTOM, HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT};
    int dragDirection = HTBOTTOMRIGHT;
    std::vector<std::pair<HWND, LONG_PTR>> pageStyles;
    RECT dragBounds = {};
    POINT dragOrigin = {};
    bool gripDragging = false;
    bool closeHovered = false;
    HWND hoveredButton = nullptr; // OK or Cancel under the mouse
    HFONT titleFont = nullptr;
    bool additionalPages = false, sizing = false;
    int rendererTab = 0;
    static constexpr int CloseId = 0x7f10;

    struct Button
    {
        HWND window;
        LONG_PTR style;
        RECT rect;
    };

    std::vector<Button> buttons;
    bool layout = false, attaching = false;

    static LRESULT CALLBACK TabProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        // Leave the player's page objects intact, but keep its extra Pin Info
        // page out of the renderer menu, including keyboard/programmatic navigation.
        if (msg == TCM_SETCURSEL && int(w) != self->rendererTab)
            return -1;
        if ((msg == TCM_SETCURFOCUS && int(w) != self->rendererTab) || msg == WM_KEYDOWN || msg == WM_LBUTTONDOWN ||
            msg == WM_LBUTTONUP)
            return 0;
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(h, TabProc, 305);
        return DefSubclassProc(h, msg, w, l);
    }

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

    static LRESULT CALLBACK ButtonProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        if (msg == WM_MOUSEMOVE && self->hoveredButton != h)
        {
            self->hoveredButton = h;
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, h, 0};
            TrackMouseEvent(&track);
            InvalidateRect(h, nullptr, FALSE);
        }
        if ((msg == WM_MOUSELEAVE || msg == WM_CANCELMODE) && self->hoveredButton == h)
        {
            self->hoveredButton = nullptr;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(h, ButtonProc, 309);
        return DefSubclassProc(h, msg, w, l);
    }

    SIZE preferred = {};
    UINT preferredDpi = 96;
    SIZE savedSize = {}; // Outer window size at 96 DPI, independent of renderer settings.
    bool userSizing = false;

    void DrawChrome(HWND control, HDC dc, RECT bounds)
    {
        FillRect(dc, &bounds, Background());
        if (control == titleLabel)
        {
            auto font = SelectObject(dc, titleFont);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, MenuColorRef(CurrentMenuPalette().textMain));
            DrawTextW(dc, L"FrankyRenderer", -1, &bounds, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, font);
            return;
        }
        DRAWITEMSTRUCT draw = {};
        draw.CtlType = ODT_BUTTON;
        draw.CtlID = GetDlgCtrlID(control);
        draw.itemAction = ODA_DRAWENTIRE;
        draw.itemState = (SendMessageW(control, BM_GETSTATE, 0, 0) & BST_PUSHED) ? ODS_SELECTED : 0;
        if (GetFocus() == control)
            draw.itemState |= ODS_FOCUS;
        draw.hwndItem = control;
        draw.hDC = dc;
        draw.rcItem = bounds;
        SendMessageW(host, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    }

    static LRESULT CALLBACK ChromeProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        if (msg == WM_PRINTCLIENT)
        {
            RECT bounds;
            GetClientRect(h, &bounds);
            self->DrawChrome(h, reinterpret_cast<HDC>(w), bounds);
            return 0;
        }
        if (msg == WM_ERASEBKGND)
        {
            RECT bounds;
            GetClientRect(h, &bounds);
            FillRect(reinterpret_cast<HDC>(w), &bounds, self->Background());
            return 1;
        }
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT paint;
            HDC target = BeginPaint(h, &paint);
            RECT bounds;
            GetClientRect(h, &bounds);
            HDC buffer = CreateCompatibleDC(target);
            HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1L, bounds.right), std::max(1L, bounds.bottom));
            if (buffer && bitmap)
            {
                auto previous = SelectObject(buffer, bitmap);
                self->DrawChrome(h, buffer, bounds);
                BitBlt(target, 0, 0, bounds.right, bounds.bottom, buffer, 0, 0, SRCCOPY);
                SelectObject(buffer, previous);
            }
            if (bitmap)
                DeleteObject(bitmap);
            if (buffer)
                DeleteDC(buffer);
            EndPaint(h, &paint);
            return 0;
        }
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(h, ChromeProc, 308);
        return DefSubclassProc(h, msg, w, l);
    }

    void BeginResize(HWND capture, int direction, POINT point)
    {
        dragDirection = direction;
        dragOrigin = point;
        GetWindowRect(host, &dragBounds);
        gripDragging = userSizing = true;
        SetCapture(capture);
    }

    void MoveResize(POINT point)
    {
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor);
        const int dpi = int(GetDpiForWindow(host));
        const LONG minWidth = std::min<LONG>(MulDiv(760, dpi, 96), monitor.rcWork.right - monitor.rcWork.left);
        const LONG minHeight = std::min<LONG>(MulDiv(480, dpi, 96), monitor.rcWork.bottom - monitor.rcWork.top);
        const LONG dx = point.x - dragOrigin.x, dy = point.y - dragOrigin.y;
        const int direction = dragDirection;
        RECT next = dragBounds;
        if (direction == HTLEFT || direction == HTTOPLEFT || direction == HTBOTTOMLEFT)
            next.left =
                std::clamp(next.left + dx, std::min(monitor.rcWork.left, next.right - minWidth), next.right - minWidth);
        if (direction == HTRIGHT || direction == HTTOPRIGHT || direction == HTBOTTOMRIGHT)
            next.right =
                std::clamp(next.right + dx, next.left + minWidth, std::max(monitor.rcWork.right, next.left + minWidth));
        if (direction == HTTOP || direction == HTTOPLEFT || direction == HTTOPRIGHT)
            next.top = std::clamp(
                next.top + dy, std::min(monitor.rcWork.top, next.bottom - minHeight), next.bottom - minHeight);
        if (direction == HTBOTTOM || direction == HTBOTTOMLEFT || direction == HTBOTTOMRIGHT)
            next.bottom = std::clamp(
                next.bottom + dy, next.top + minHeight, std::max(monitor.rcWork.bottom, next.top + minHeight));
        RECT current;
        GetWindowRect(host, &current);
        if (!EqualRect(&current, &next))
        {
            SetWindowPos(host,
                         nullptr,
                         next.left,
                         next.top,
                         next.right - next.left,
                         next.bottom - next.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RedrawWindow(host, nullptr, nullptr, RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
        }
    }

    void EndResize()
    {
        if (!gripDragging)
            return;
        gripDragging = false;
        SaveSize();
        userSizing = false;
    }

    static LRESULT CALLBACK PageBackgroundProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        if (msg == WM_ERASEBKGND)
        {
            RECT bounds;
            GetClientRect(h, &bounds);
            FillRect(reinterpret_cast<HDC>(w), &bounds, self->Background());
            return 1;
        }
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(h, PageBackgroundProc, 307);
        return DefSubclassProc(h, msg, w, l);
    }

    static LRESULT CALLBACK GripProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        if (msg == WM_SETCURSOR)
        {
            const int direction = ResizeDirections[GetDlgCtrlID(h) - 0x7f11];
            const auto cursor = direction == HTLEFT || direction == HTRIGHT            ? IDC_SIZEWE
                                : direction == HTTOP || direction == HTBOTTOM          ? IDC_SIZENS
                                : direction == HTTOPRIGHT || direction == HTBOTTOMLEFT ? IDC_SIZENESW
                                                                                       : IDC_SIZENWSE;
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(h, &paint);
            RECT bounds;
            GetClientRect(h, &bounds);
            FillRect(dc, &bounds, self->Background());
            EndPaint(h, &paint);
            return 0;
        }
        if (msg == WM_ERASEBKGND)
            return 1;
        if (msg == WM_LBUTTONDOWN)
        {
            POINT point = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &point);
            self->BeginResize(h, ResizeDirections[GetDlgCtrlID(h) - 0x7f11], point);
            return 0;
        }
        if (msg == WM_MOUSEMOVE && self->gripDragging)
        {
            POINT point = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &point);
            self->MoveResize(point);
            return 0;
        }
        if (msg == WM_LBUTTONUP && self->gripDragging)
        {
            ReleaseCapture();
            return 0;
        }
        if (msg == WM_CAPTURECHANGED && self->gripDragging)
        {
            self->EndResize();
            return 0;
        }
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(h, GripProc, 306);
        return DefSubclassProc(h, msg, w, l);
    }

    void LoadSize()
    {
        savedSize = {};
        SIZE value = {};
        DWORD bytes = sizeof(value);
        if (RegGetValueW(HKEY_CURRENT_USER,
                         MenuAppearanceKey().c_str(),
                         L"WindowSize",
                         RRF_RT_REG_BINARY,
                         nullptr,
                         &value,
                         &bytes) == ERROR_SUCCESS &&
            bytes == sizeof(value) && value.cx >= 640 && value.cy >= 400 && value.cx <= 16384 && value.cy <= 16384)
            savedSize = value;
    }

    void SaveSize()
    {
        if (!host || IsIconic(host) || IsZoomed(host))
            return;
        RECT bounds;
        if (!GetWindowRect(host, &bounds))
            return;
        const UINT dpi = GetDpiForWindow(host);
        savedSize = {MulDiv(bounds.right - bounds.left, 96, dpi), MulDiv(bounds.bottom - bounds.top, 96, dpi)};
        HKEY key = nullptr;
        if (RegCreateKeyExW(
                HKEY_CURRENT_USER, MenuAppearanceKey().c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
            ERROR_SUCCESS)
        {
            RegSetValueExW(
                key, L"WindowSize", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&savedSize), sizeof(savedSize));
            RegCloseKey(key);
        }
    }

    void SizeToContent()
    {
        if (sizing || userSizing || !host || preferred.cx <= 0 || preferred.cy <= 0)
            return;
        const UINT dpi = GetDpiForWindow(host);
        const auto content = SettingsLayout::ContentSize(preferred, preferredDpi, dpi);
        RECT outer = {0, 0, content.cx, content.cy + MulDiv(84, dpi, 96)};
        AdjustWindowRectExForDpi(
            &outer, DWORD(GetWindowLongPtrW(host, GWL_STYLE)), FALSE, DWORD(GetWindowLongPtrW(host, GWL_EXSTYLE)), dpi);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST), &monitor);
        const RECT work = monitor.rcWork;
        auto fitted = SettingsLayout::FitFrame({outer.right - outer.left, outer.bottom - outer.top},
                                               {work.right - work.left, work.bottom - work.top},
                                               dpi);
        if (savedSize.cx > 0 && savedSize.cy > 0)
            fitted = {std::min<LONG>(MulDiv(savedSize.cx, dpi, 96), work.right - work.left),
                      std::min<LONG>(MulDiv(savedSize.cy, dpi, 96), work.bottom - work.top)};
        const int width = fitted.cx, height = fitted.cy;
        RECT current;
        GetWindowRect(host, &current);
        const int x = std::clamp(int(current.left), int(work.left), int(work.right) - width),
                  y = std::clamp(int(current.top), int(work.top), int(work.bottom) - height);
        if (current.left == x && current.top == y && current.right - current.left == width &&
            current.bottom - current.top == height)
            return;
        sizing = true;
        SetWindowPos(host, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        sizing = false;
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
        auto placeChrome = [&](HWND control, int x, int y, int width, int height)
        {
            if (!control)
                return;
            RECT current;
            GetWindowRect(control, &current);
            MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&current), 2);
            if (current.left == x && current.top == y && current.right == x + width && current.bottom == y + height)
                return;
            SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
            InvalidateRect(control, nullptr, FALSE);
        };
        placeChrome(closeButton, client.right - px(48), 0, px(48), header);
        if (titleLabel)
            placeChrome(titleLabel, px(16), px(8), std::max(0, std::min(px(240), int(client.right) - px(80))), px(24));
        const int edge = px(6), corner = px(12), grip = px(20);
        const int right = int(client.right), bottom = int(client.bottom);
        const RECT edgeRects[] = {{right - grip, bottom - grip, right, bottom},
                                  {0, corner, edge, bottom - corner},
                                  {right - edge, corner, right, bottom - grip},
                                  {corner, 0, right - corner, edge},
                                  {corner, bottom - edge, right - grip, bottom},
                                  {0, 0, corner, corner},
                                  {right - corner, 0, right, corner},
                                  {0, bottom - corner, corner, bottom}};
        for (size_t i = 0; i < resizeEdges.size(); ++i)
        {
            const auto& rect = edgeRects[i];
            SetWindowPos(resizeEdges[i],
                         HWND_TOP,
                         rect.left,
                         rect.top,
                         rect.right - rect.left,
                         rect.bottom - rect.top,
                         SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        int buttonRight = client.right - px(36);
        for (int id : {IDCANCEL, IDOK})
            if (HWND button = GetDlgItem(host, id))
            {
                const int width = px(88);
                placeChrome(button, buttonRight - width, header + height + px(8), width, px(30));
                buttonRight -= width + margin;
            }
        if (tab)
            ShowWindow(tab, SW_HIDE);
        layout = false;
    }

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto self = reinterpret_cast<Direct2DFrame*>(data);
        if (self->additionalPages)
        {
            if ((msg == PSM_SETCURSEL && (int(w) != self->rendererTab || l)) || msg == PSM_SETCURSELID)
                return FALSE;
            if (msg == WM_NOTIFY && l)
            {
                auto notification = reinterpret_cast<NMHDR*>(l);
                if (notification->hwndFrom == self->tab && notification->code == TCN_SELCHANGING)
                    return TRUE;
            }
            if (msg == PSM_ISDIALOGMESSAGE && l)
            {
                auto key = reinterpret_cast<MSG*>(l);
                if (key->message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) &&
                    (key->wParam == VK_TAB || key->wParam == VK_NEXT || key->wParam == VK_PRIOR))
                    return TRUE;
            }
        }
        if (msg == WM_COMMAND && LOWORD(w) == CloseId)
        {
            PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        }
        // A thick frame without caption keeps a visible resize band above the
        // client area. Start the client at the window's top edge so the header
        // and close button reach the rounded corner; WM_NCHITTEST below still
        // resizes from that edge. Maximized windows keep their normal frame.
        if (msg == WM_NCCALCSIZE && w && !IsZoomed(h))
        {
            auto& client = reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0];
            const LONG top = client.top;
            const LRESULT result = DefSubclassProc(h, msg, w, l);
            client.top = top;
            return result;
        }
        // The system frame painting still assumes that top band and draws a
        // light bar over the header, e.g. when focus changes. Let it paint,
        // then repaint the header strip with the window's own content.
        if ((msg == WM_NCPAINT || msg == WM_NCACTIVATE) && !IsZoomed(h))
        {
            const LRESULT result = DefSubclassProc(h, msg, w, l);
            RECT band;
            GetClientRect(h, &band);
            band.bottom = MulDiv(12, int(GetDpiForWindow(h)), 96);
            RedrawWindow(h, &band, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return result;
        }
        if (msg == WM_NCHITTEST)
        {
            // OLE property-frame hosts can keep fixed-dialog hit testing even
            // after WS_THICKFRAME is enabled. Own the resize edges explicitly.
            if (!IsZoomed(h))
            {
                RECT bounds;
                GetWindowRect(h, &bounds);
                const int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
                const int edge = MulDiv(8, int(GetDpiForWindow(h)), 96);
                if (x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom)
                {
                    const bool left = x < bounds.left + edge, right = x >= bounds.right - edge;
                    const bool top = y < bounds.top + edge, bottom = y >= bounds.bottom - edge;
                    if (top)
                        return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
                    if (bottom)
                        return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
                    if (left || right)
                        return left ? HTLEFT : HTRIGHT;
                }
            }
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
        if (msg == WM_NCLBUTTONDOWN && w >= HTLEFT && w <= HTBOTTOMRIGHT)
        {
            self->BeginResize(h, int(w), {GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            return 0;
        }
        if (msg == WM_MOUSEMOVE && self->gripDragging && GetCapture() == h)
        {
            POINT point = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &point);
            self->MoveResize(point);
            return 0;
        }
        if (msg == WM_LBUTTONUP && self->gripDragging && GetCapture() == h)
        {
            ReleaseCapture();
            return 0;
        }
        if (msg == WM_CAPTURECHANGED && self->gripDragging)
            self->EndResize();
        if (msg == WM_SYSCOMMAND && (w & 0xfff0) == SC_SIZE)
            return DefWindowProcW(h, msg, w, l);
        if (msg == WM_ENTERSIZEMOVE)
            self->userSizing = true;
        if (msg == WM_EXITSIZEMOVE)
        {
            self->SaveSize();
            self->userSizing = false;
        }
        if (msg == WM_GETMINMAXINFO)
        {
            const auto result = DefSubclassProc(h, msg, w, l);
            auto limits = reinterpret_cast<MINMAXINFO*>(l);
            MONITORINFO monitor = {sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor);
            const UINT dpi = GetDpiForWindow(h);
            limits->ptMinTrackSize = {std::min<LONG>(MulDiv(760, dpi, 96), monitor.rcWork.right - monitor.rcWork.left),
                                      std::min<LONG>(MulDiv(480, dpi, 96), monitor.rcWork.bottom - monitor.rcWork.top)};
            limits->ptMaxTrackSize = {monitor.rcWork.right - monitor.rcWork.left,
                                      monitor.rcWork.bottom - monitor.rcWork.top};
            return result;
        }
        if (msg == WM_ERASEBKGND)
        {
            RECT r;
            GetClientRect(h, &r);
            FillRect(reinterpret_cast<HDC>(w), &r, self->Background());
            return 1;
        }
        if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC)
        {
            const auto palette = CurrentMenuPalette();
            SetBkColor(reinterpret_cast<HDC>(w), MenuColorRef(palette.app));
            SetTextColor(reinterpret_cast<HDC>(w), MenuColorRef(palette.textMain));
            return reinterpret_cast<LRESULT>(self->Background());
        }
        if (msg == WM_DRAWITEM)
        {
            auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (draw && draw->CtlType == ODT_BUTTON && draw->CtlID == CloseId)
            {
                HBRUSH hover = nullptr;
                if (self->closeHovered || (draw->itemState & ODS_SELECTED))
                {
                    const auto palette = CurrentMenuPalette();
                    hover = CreateSolidBrush(MenuColorRef(
                        MixColor(palette.app, palette.textMain, draw->itemState & ODS_SELECTED ? .2f : .1f)));
                }
                FillRect(draw->hDC, &draw->rcItem, hover ? hover : self->Background());
                if (hover)
                    DeleteObject(hover);
                const int dpi = int(GetDpiForWindow(h));
                const int radius = MulDiv(7, dpi, 96), cx = (draw->rcItem.left + draw->rcItem.right) / 2,
                          cy = (draw->rcItem.top + draw->rcItem.bottom) / 2;
                HPEN pen =
                    CreatePen(PS_SOLID, std::max(2, MulDiv(2, dpi, 96)), MenuColorRef(CurrentMenuPalette().textMain));
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
                FillRect(draw->hDC, &draw->rcItem, self->Background());
                const auto palette = CurrentMenuPalette();
                const bool hovered = self->hoveredButton == draw->hwndItem;
                HBRUSH fill = CreateSolidBrush(
                    MenuColorRef(pressed   ? palette.activeBg
                                 : hovered ? MixColor(palette.cardHeader, palette.accent, palette.light ? .14f : .22f)
                                           : palette.cardHeader));
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
                SetTextColor(draw->hDC, MenuColorRef(palette.textMain));
                wchar_t label[80];
                GetWindowTextW(draw->hwndItem, label, 80);
                auto font = SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0);
                auto old = SelectObject(draw->hDC, reinterpret_cast<HGDIOBJ>(font));
                auto r = draw->rcItem;
                DrawTextW(draw->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(draw->hDC, old);
                return TRUE;
            }
        }
        const auto result = DefSubclassProc(h, msg, w, l);
        // MPC-BE's property sheet also recalculates its size on page activation.
        // Keep our declared content size instead of accumulating its tab margins.
        if (!self->attaching && ((self->additionalPages && msg == WM_WINDOWPOSCHANGED) || msg == WM_DPICHANGED))
            self->SizeToContent();
        const bool resized = msg == WM_WINDOWPOSCHANGED && !(reinterpret_cast<WINDOWPOS*>(l)->flags & SWP_NOSIZE);
        if (msg == WM_SIZE || resized || msg == WM_SHOWWINDOW || msg == WM_DPICHANGED)
            self->Layout();
        if (msg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(h, Proc, 303);
            self->host = nullptr;
        }
        return result;
    }

  public:
    void BeginOpening(HWND pageWindow)
    {
        if (host || opening)
            return;
        // The player may show its property sheet before processing our posted
        // layout message. Keep that construction stage out of composition,
        // without changing its visibility, activation or OLE measurements.
        for (HWND candidate = GetParent(pageWindow); candidate; candidate = GetParent(candidate))
        {
            HWND tabs = FindWindowExW(candidate, nullptr, WC_TABCONTROLW, nullptr);
            if (!tabs || TabCtrl_GetItemCount(tabs) < 1 || !GetDlgItem(candidate, IDOK) ||
                !GetDlgItem(candidate, IDCANCEL))
                continue;
            DWORD cloaked = 0;
            if (IsWindowVisible(candidate) ||
                FAILED(DwmGetWindowAttribute(candidate, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || cloaked)
                return;
            const BOOL cloak = TRUE;
            if (SUCCEEDED(DwmSetWindowAttribute(candidate, DWMWA_CLOAK, &cloak, sizeof(cloak))))
                opening = candidate;
            return;
        }
    }

    void FinishOpening()
    {
        if (!opening)
            return;
        if (IsWindow(opening))
        {
            // Complete the first Direct2D paint while the frame is composed
            // offscreen, then reveal the finished dialog without a timer.
            RedrawWindow(opening, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            const BOOL cloak = FALSE;
            DwmSetWindowAttribute(opening, DWMWA_CLOAK, &cloak, sizeof(cloak));
        }
        opening = nullptr;
    }

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
            if (tabs && TabCtrl_GetItemCount(tabs) >= 1 && GetDlgItem(candidate, IDOK) &&
                GetDlgItem(candidate, IDCANCEL))
                break;
        }
        if (!candidate)
            return;
        attaching = true;
        host = candidate;
        preferredDpi = GetDpiForWindow(host);
        userSizing = false;
        LoadSize();
        tab = tabs;
        additionalPages = TabCtrl_GetItemCount(tab) > 1;
        rendererTab = std::max(0, TabCtrl_GetCurSel(tab));
        if (additionalPages)
            SetWindowSubclass(tab, TabProc, 305, reinterpret_cast<DWORD_PTR>(this));
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
        SetWindowLongPtrW(host, GWL_STYLE, (originalStyle & ~WS_CAPTION) | WS_THICKFRAME | WS_CLIPCHILDREN);
        for (HWND child = page; child && child != host; child = GetParent(child))
        {
            const auto style = GetWindowLongPtrW(child, GWL_STYLE);
            pageStyles.push_back({child, style});
            SetWindowLongPtrW(child, GWL_STYLE, style | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
            SetWindowSubclass(child, PageBackgroundProc, 307, reinterpret_cast<DWORD_PTR>(this));
        }
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
        titleLabel = CreateWindowExW(0,
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
        SetWindowSubclass(titleLabel, ChromeProc, 308, reinterpret_cast<DWORD_PTR>(this));
        SetWindowSubclass(closeButton, ChromeProc, 308, reinterpret_cast<DWORD_PTR>(this));
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
            SetWindowSubclass(button, ChromeProc, 308, reinterpret_cast<DWORD_PTR>(this));
            SetWindowSubclass(button, ButtonProc, 309, reinterpret_cast<DWORD_PTR>(this));
        }
        ApplyMenuFrameColors(host);
        for (int i = 0; i < int(std::size(ResizeDirections)); ++i)
        {
            HWND handle = CreateWindowExW(0,
                                          L"STATIC",
                                          L"",
                                          WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                                          0,
                                          0,
                                          20,
                                          20,
                                          host,
                                          reinterpret_cast<HMENU>(INT_PTR(0x7f11 + i)),
                                          nullptr,
                                          nullptr);
            resizeEdges.push_back(handle);
            if (handle)
                SetWindowSubclass(handle, GripProc, 306, reinterpret_cast<DWORD_PTR>(this));
        }
        resizeGrip = resizeEdges.front();
        SetWindowTextW(host, L"FrankyRenderer");
        SizeToContent();
        Layout();
        InvalidateRect(host, nullptr, TRUE);
        attaching = false;
    }

    void Detach()
    {
        FinishOpening();
        for (HWND handle : resizeEdges)
            if (IsWindow(handle))
                DestroyWindow(handle);
        resizeEdges.clear();
        resizeGrip = nullptr;
        for (const auto& [window, style] : pageStyles)
            if (IsWindow(window))
            {
                RemoveWindowSubclass(window, PageBackgroundProc, 307);
                SetWindowLongPtrW(window, GWL_STYLE, style);
            }
        pageStyles.clear();
        if (IsWindow(tab))
            RemoveWindowSubclass(tab, TabProc, 305);
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
                    RemoveWindowSubclass(b.window, ChromeProc, 308);
                    RemoveWindowSubclass(b.window, ButtonProc, 309);
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
        tab = nullptr;
        additionalPages = false;
        buttons.clear();
    }

    ~Direct2DFrame()
    {
        Detach();
        DeleteObject(background);
    }
};
