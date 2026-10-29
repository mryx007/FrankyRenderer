#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <thread>
#include "Renderer/VideoBackground.h"

static void Paint(HWND window, COLORREF color)
{
    RECT rect = {};
    GetClientRect(window, &rect);
    HDC dc = GetDC(window);
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    GdiFlush();
    DeleteObject(brush);
    ReleaseDC(window, dc);
}

static COLORREF Pixel(HWND window, int x, int y)
{
    HDC dc = GetDC(window);
    COLORREF pixel = GetPixel(dc, x, y);
    ReleaseDC(window, dc);
    return pixel;
}

int main()
{
    HWND parent = CreateWindowExW(0,
                                  L"STATIC",
                                  L"Background test",
                                  WS_POPUP | WS_VISIBLE,
                                  0,
                                  0,
                                  512,
                                  256,
                                  nullptr,
                                  nullptr,
                                  GetModuleHandleW(nullptr),
                                  nullptr);
    if (!parent)
        return 2;
    HWND control = CreateWindowExW(0,
                                   L"STATIC",
                                   L"",
                                   WS_CHILD | WS_VISIBLE,
                                   400,
                                   100,
                                   80,
                                   80,
                                   parent,
                                   nullptr,
                                   GetModuleHandleW(nullptr),
                                   nullptr);
    UpdateWindow(parent);
    UpdateWindow(control);
    Paint(parent, RGB(200, 40, 20));
    Paint(control, RGB(20, 200, 40));
    if (Pixel(parent, 16, 16) != RGB(200, 40, 20) || Pixel(control, 16, 16) != RGB(20, 200, 40))
        return 3;
    bool created = false;
    std::thread graph(
        [&]()
        {
            created = VideoBackground::Ensure(parent) != nullptr;
            VideoBackground::Repaint(parent);
        });
    graph.join();
    int result = 0;
    if (created || VideoBackground::Find(parent))
        result = 4;
    else if (Pixel(parent, 16, 16) != RGB(0, 0, 0))
    {
        puts("FAIL exposed video owner retains stale pixels without a background child");
        result = 5;
    }
    else if (Pixel(control, 16, 16) != RGB(20, 200, 40))
    {
        puts("FAIL repaint overwrites an existing visible child");
        result = 6;
    }
    else
        puts("PASS graph-thread repaint clears exposed owner pixels and preserves visible children");
    VideoBackground::Repaint(nullptr);
    DestroyWindow(parent);
    return result;
}
