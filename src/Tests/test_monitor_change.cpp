#define NOMINMAX
#include "Renderer/MonitorChange.h"
#include <cstdio>
#include <stdexcept>

static int redraws = 0, childMoves = 0;

static void Check(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}

static LRESULT CALLBACK Video(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_CREATE)
        MonitorChange::Attach(hwnd);
    if (msg == WM_NCDESTROY)
        MonitorChange::Detach(hwnd);
    if (msg == WM_MOVE)
        ++childMoves;
    if (msg == MonitorChange::Message)
    {
        ++redraws;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

static void Pump()
{
    const auto end = GetTickCount64() + 150;
    do
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}

int main()
{
    try
    {
        WNDCLASSW cls = {};
        cls.lpfnWndProc = Video;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"FrankyMonitorChangeTest";
        Check(RegisterClassW(&cls) != 0, "register test video");
        HWND host = CreateWindowW(
            L"STATIC", L"host", WS_OVERLAPPEDWINDOW, 20, 20, 200, 200, nullptr, nullptr, cls.hInstance, nullptr);
        HWND unrelated =
            CreateWindowW(L"STATIC", L"other", WS_POPUP, 0, 0, 20, 20, nullptr, nullptr, cls.hInstance, nullptr);
        HWND video = CreateWindowW(cls.lpszClassName,
                                   L"paused video",
                                   WS_CHILD | WS_VISIBLE,
                                   0,
                                   0,
                                   100,
                                   100,
                                   host,
                                   nullptr,
                                   cls.hInstance,
                                   nullptr);
        Check(host && unrelated && video && MonitorChange::hook && MonitorChange::windows.size() == 1,
              "hook attached on video UI thread");
        MonitorChange::Attach(video);
        Check(MonitorChange::windows.size() == 1, "duplicate attachment ignored");
        ShowWindow(host, SW_SHOWNOACTIVATE);
        Pump();
        redraws = childMoves = 0;
        // Simulate the previous monitor; actual events and repaint delivery are real.
        MonitorChange::windows[0].monitor = reinterpret_cast<HMONITOR>(1);
        NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE, unrelated, OBJID_WINDOW, CHILDID_SELF);
        Pump();
        Check(!redraws, "other player windows ignored");
        Check(SetWindowPos(host, nullptr, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER), "move host only");
        Pump();
        Check(!childMoves, "moving parent does not send child WM_MOVE");
        Check(redraws == 1, "parent location event schedules paused redraw without playback");
        NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE, host, OBJID_WINDOW, CHILDID_SELF);
        Pump();
        Check(redraws == 1, "same-monitor movements do not request repeated redraws");
        DestroyWindow(video);
        Check(!MonitorChange::hook && MonitorChange::windows.empty(), "window destruction removes hook and references");
        NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE, host, OBJID_WINDOW, CHILDID_SELF);
        Pump();
        Check(redraws == 1, "no callback work after detach");
        DestroyWindow(host);
        DestroyWindow(unrelated);
        Check(UnregisterClassW(cls.lpszClassName, cls.hInstance), "class unload");
        puts(
            "PASS monitor change: real parent move, no child WM_MOVE, paused repaint, unrelated-window filtering, same-monitor suppression, UI-thread hook teardown");
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("FAIL monitor change: %s\n", e.what());
        return 1;
    }
}
