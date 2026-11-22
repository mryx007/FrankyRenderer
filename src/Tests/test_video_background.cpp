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
            // The renderer leaving a graph-thread owner (MPC-HC, MPC-BE):
            // no background child, the exposed area is painted black.
            VideoBackground::Release(parent);
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

    // Issue #48: after this renderer leaves, another renderer's child window
    // must not be covered by the background.
    if (!result)
    {
        const HWND background = VideoBackground::Ensure(parent);
        if (!background || !IsWindowVisible(background))
            result = 7, puts("FAIL owner thread creates a visible background");
        std::thread leave([&] { VideoBackground::Release(parent); });
        leave.join();
        MSG msg;
        for (int i = 0; i < 50 && IsWindowVisible(background); ++i)
        {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
            Sleep(10);
        }
        if (!result && IsWindowVisible(background))
            result = 8, puts("FAIL background stays visible after the renderer left");
        HWND foreign = CreateWindowExW(0,
                                       L"STATIC",
                                       L"",
                                       WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                       0,
                                       0,
                                       300,
                                       200,
                                       parent,
                                       nullptr,
                                       GetModuleHandleW(nullptr),
                                       nullptr);
        SetWindowPos(foreign, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(foreign);
        Paint(foreign, RGB(30, 60, 220));
        if (!result && Pixel(parent, 50, 50) != RGB(30, 60, 220))
            result = 9, puts("FAIL another renderer's window is covered after this renderer left");
        // This renderer returns: the background shows again, below the video.
        VideoBackground::Ensure(parent);
        if (!result && !IsWindowVisible(background))
            result = 10, puts("FAIL background returns with this renderer");
        DestroyWindow(foreign);
        if (!result)
            puts("PASS background hides when the renderer leaves, another renderer stays visible, it returns with "
                 "this renderer");
    }
    VideoBackground::Repaint(nullptr);
    DestroyWindow(parent);
    return result;
}
