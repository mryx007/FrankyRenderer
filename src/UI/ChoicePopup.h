#pragma once

#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "UI/MenuTheme.h"

// A bounded selection window; native popup menus do not support wheel scrolling.
class ChoicePopup
{
  public:
    std::vector<std::wstring> labels, descriptions;
    HWND window = nullptr;
    int selected = -1, active = 0, first = 0, rows = 12, rowHeight = 32;
    int width = 320, wheel = 0, dragOffset = -1;
    bool done = false, tracking = false;
    HFONT font = nullptr, descriptionFont = nullptr;
    bool described = false;

    void Scroll(int amount)
    {
        first = std::clamp(first + amount, 0, std::max(0, int(labels.size()) - rows));
        if (window)
            InvalidateRect(window, nullptr, FALSE);
    }

    void Move(int index)
    {
        active = std::clamp(index, 0, int(labels.size()) - 1);
        if (active < first)
            first = active;
        if (active >= first + rows)
            first = active - rows + 1;
        Scroll(0);
    }

    RECT Thumb() const
    {
        const int track = rows * rowHeight;
        const int height = std::max(rowHeight, track * rows / int(labels.size()));
        const int top = 1 + (track - height) * first / std::max(1, int(labels.size()) - rows);
        return {width - 10, top, width - 4, top + height};
    }

    void Paint()
    {
        PAINTSTRUCT ps;
        HDC target = BeginPaint(window, &ps);
        RECT bounds;
        GetClientRect(window, &bounds);
        // Compose a complete frame offscreen so hover never exposes the cleared background.
        HDC buffer = CreateCompatibleDC(target);
        HBITMAP bitmap = buffer ? CreateCompatibleBitmap(target, bounds.right, bounds.bottom) : nullptr;
        HGDIOBJ oldBitmap = bitmap ? SelectObject(buffer, bitmap) : nullptr;
        HDC dc = bitmap ? buffer : target;
        auto fill = [&](RECT rect, COLORREF color)
        {
            HBRUSH brush = CreateSolidBrush(color);
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
        };
        const auto palette = CurrentMenuPalette();
        fill(bounds, MenuColorRef(palette.border));
        InflateRect(&bounds, -1, -1);
        fill(bounds, MenuColorRef(palette.light ? palette.card : palette.input));
        auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, MenuColorRef(palette.textMain));
        for (int row = 0; row < rows && first + row < int(labels.size()); ++row)
        {
            const int index = first + row;
            // Leave room for the scroll thumb only when the list scrolls.
            RECT rect = {
                1, 1 + row * rowHeight, width - (int(labels.size()) > rows ? 13 : 1), 1 + (row + 1) * rowHeight};
            if (index == active)
                fill(rect, MenuColorRef(palette.activeBg));
            RECT text = rect;
            text.left += rowHeight;
            if (described)
            {
                RECT title = text;
                title.bottom = title.top + rowHeight / 2;
                DrawTextW(
                    dc, labels[index].c_str(), -1, &title, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                text.top += rowHeight / 2;
                SelectObject(dc, descriptionFont);
                SetTextColor(dc, MenuColorRef(palette.textMuted));
                DrawTextW(dc, descriptions[index].c_str(), -1, &text, DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
                SelectObject(dc, font);
                SetTextColor(dc, MenuColorRef(palette.textMain));
            }
            else
                DrawTextW(
                    dc, labels[index].c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (index == selected)
            {
                rect.right = rect.left + rowHeight;
                DrawTextW(dc, L"✓", 1, &rect, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
        }
        if (int(labels.size()) > rows)
            fill(Thumb(), MenuColorRef(palette.toggleOff));
        SelectObject(dc, old);
        if (bitmap)
        {
            RECT client;
            GetClientRect(window, &client);
            BitBlt(target, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, oldBitmap);
            DeleteObject(bitmap);
        }
        if (buffer)
            DeleteDC(buffer);
        EndPaint(window, &ps);
    }

    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto self = reinterpret_cast<ChoicePopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE)
        {
            self = static_cast<ChoicePopup*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg)
        {
        case WM_PAINT:
            self->Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEWHEEL:
            self->wheel += GET_WHEEL_DELTA_WPARAM(wp);
            while (std::abs(self->wheel) >= WHEEL_DELTA)
            {
                const int direction = self->wheel > 0 ? 1 : -1;
                self->Scroll(-direction * 3);
                self->wheel -= direction * WHEEL_DELTA;
            }
            return 0;
        case WM_KEYDOWN:
            switch (wp)
            {
            case VK_ESCAPE:
                self->selected = -1;
                self->done = true;
                break;
            case VK_RETURN:
                self->selected = self->active;
                self->done = true;
                break;
            case VK_UP:
                self->Move(self->active - 1);
                break;
            case VK_DOWN:
                self->Move(self->active + 1);
                break;
            case VK_HOME:
                self->Move(0);
                break;
            case VK_END:
                self->Move(int(self->labels.size()) - 1);
                break;
            case VK_PRIOR:
                self->Move(self->active - self->rows);
                break;
            case VK_NEXT:
                self->Move(self->active + self->rows);
                break;
            }
            return 0;
        case WM_LBUTTONDOWN:
        {
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT client;
            GetClientRect(hwnd, &client);
            if (!PtInRect(&client, point))
            {
                self->selected = -1;
                self->done = true;
            }
            else if (point.x >= self->width - 13 && int(self->labels.size()) > self->rows)
            {
                RECT thumb = self->Thumb();
                if (PtInRect(&thumb, point))
                    self->dragOffset = point.y - thumb.top;
                else
                    self->Scroll(point.y < thumb.top ? -self->rows : self->rows);
            }
            else
            {
                self->selected =
                    std::min(int(self->labels.size()) - 1, self->first + int(point.y - 1) / self->rowHeight);
                self->done = true;
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            const int y = GET_Y_LPARAM(lp), x = GET_X_LPARAM(lp);
            if (self->dragOffset >= 0)
            {
                RECT thumb = self->Thumb();
                const int travel = self->rows * self->rowHeight - (thumb.bottom - thumb.top);
                const int index =
                    MulDiv(y - 1 - self->dragOffset, int(self->labels.size()) - self->rows, std::max(1, travel));
                self->Scroll(index - self->first);
            }
            else if (x > 0 && x < self->width - (int(self->labels.size()) > self->rows ? 13 : 1) && y > 0 &&
                     y < 1 + self->rows * self->rowHeight)
            {
                const int hovered = std::min(int(self->labels.size()) - 1, self->first + (y - 1) / self->rowHeight);
                if (hovered != self->active)
                {
                    self->active = hovered;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_LBUTTONUP:
            self->dragOffset = -1;
            return 0;
        case WM_CAPTURECHANGED:
        case WM_KILLFOCUS:
            // Activating a popup can transfer focus twice before SetFocus returns.
            // Only a focus/capture loss after opening is a user cancellation.
            if (!self->tracking)
                return 0;
            [[fallthrough]];
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_CLOSE:
            self->selected = -1;
            self->done = true;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    int Show(HWND owner, RECT anchor, float scale)
    {
        if (labels.empty())
            return -1;
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Proc),
                           &module);
        WNDCLASSEXW wc = {sizeof(wc)};
        wc.lpfnWndProc = Proc;
        wc.hInstance = module;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"FrankyChoicePopup";
        const ATOM registered = RegisterClassExW(&wc);
        if (!registered)
            return -1;
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
        described =
            descriptions.size() == labels.size() &&
            std::any_of(descriptions.begin(), descriptions.end(), [](const auto& text) { return !text.empty(); });
        rowHeight = std::max(1, int((described ? 56 : 32) * scale));
        rows = std::max(1,
                        std::min({described ? 8 : 12,
                                  int(labels.size()),
                                  int(monitor.rcWork.bottom - monitor.rcWork.top - 16) / rowHeight}));
        font = CreateFontW(-int(14 * scale),
                           0,
                           0,
                           0,
                           FW_MEDIUM,
                           FALSE,
                           FALSE,
                           FALSE,
                           DEFAULT_CHARSET,
                           0,
                           0,
                           CLEARTYPE_QUALITY,
                           0,
                           L"Segoe UI");
        descriptionFont = CreateFontW(-int(12 * scale),
                                      0,
                                      0,
                                      0,
                                      FW_MEDIUM,
                                      FALSE,
                                      FALSE,
                                      FALSE,
                                      DEFAULT_CHARSET,
                                      0,
                                      0,
                                      CLEARTYPE_QUALITY,
                                      0,
                                      L"Segoe UI");
        // As wide as the longest entry, at least the field; grow leftwards and
        // stay inside the settings window (and the monitor).
        int longest = 0;
        if (HDC dc = GetDC(owner))
        {
            const auto old = SelectObject(dc, font);
            for (const auto& label : labels)
            {
                SIZE extent = {};
                GetTextExtentPoint32W(dc, label.c_str(), int(label.size()), &extent);
                longest = std::max(longest, int(extent.cx));
            }
            SelectObject(dc, old);
            ReleaseDC(owner, dc);
        }
        RECT area = monitor.rcWork;
        RECT host = {};
        if (GetWindowRect(GetAncestor(owner, GA_ROOT), &host))
            IntersectRect(&area, &area, &host);
        const int needed = longest + rowHeight + int(28 * scale);
        width = std::min(std::max({int(anchor.right - anchor.left), needed, described ? int(480 * scale) : 0}),
                         int(area.right - area.left));
        const int height = rows * rowHeight + 2;
        const int x = std::clamp(int(anchor.right) - width, int(area.left), int(area.right) - width);
        int y = anchor.bottom + int(4 * scale);
        if (y + height > monitor.rcWork.bottom)
            y = std::max(int(monitor.rcWork.top), int(anchor.top) - height - int(4 * scale));
        active = std::max(0, selected);
        Move(active);
        HWND previousFocus = GetFocus();
        window = CreateWindowExW(
            WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, x, y, width, height, owner, nullptr, module, this);
        if (window)
        {
            // Windows 11 rounds the list's corners; the border follows the theme.
            const int corners = 3; // DWMWCP_ROUNDSMALL
            DwmSetWindowAttribute(window, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof(corners));
            const COLORREF border = MenuColorRef(CurrentMenuPalette().border);
            DwmSetWindowAttribute(window, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
            ShowWindow(window, SW_SHOWNOACTIVATE);
            SetFocus(window);
            SetCapture(window);
            tracking = true;
            MSG message;
            while (!done && IsWindow(window) && IsWindow(owner))
            {
                const BOOL result = GetMessageW(&message, nullptr, 0, 0);
                if (result <= 0)
                {
                    selected = -1;
                    if (!result)
                        PostQuitMessage(int(message.wParam));
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            const int result = selected;
            tracking = false;
            if (GetCapture() == window)
                ReleaseCapture();
            DestroyWindow(window);
            window = nullptr;
            selected = result;
            if (IsWindow(previousFocus))
                SetFocus(previousFocus);
        }
        else
            selected = -1;
        DeleteObject(font);
        font = nullptr;
        DeleteObject(descriptionFont);
        descriptionFont = nullptr;
        UnregisterClassW(wc.lpszClassName, module);
        return selected;
    }
};
