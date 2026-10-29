#pragma once
#include <windows.h>
#include <algorithm>
#include <vector>

// Moving a host does not necessarily move/repaint its child video window.
// Listen only to location events in this process, on the video's UI thread.
// No polling, host subclass, renderer pointer or cross-process injection.
namespace MonitorChange
{
inline constexpr UINT Message = WM_APP + 0x4b2;

struct Entry
{
    HWND video;
    HMONITOR monitor;
};

inline thread_local std::vector<Entry> windows;
inline thread_local HWINEVENTHOOK hook = nullptr;

inline void Observe(HWND changed)
{
    for (auto& entry : windows)
    {
        if (changed != entry.video && changed != GetAncestor(entry.video, GA_ROOT))
            continue;
        const auto next = MonitorFromWindow(entry.video, MONITOR_DEFAULTTONEAREST);
        if (next && next != entry.monitor)
        {
            entry.monitor = next;
            PostMessageW(entry.video, Message, 0, 0);
        }
    }
}

inline void CALLBACK Procedure(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG child, DWORD, DWORD)
{
    if (event == EVENT_OBJECT_LOCATIONCHANGE && object == OBJID_WINDOW && child == CHILDID_SELF)
        Observe(hwnd);
}

inline void Attach(HWND video)
{
    if (GetWindowThreadProcessId(video, nullptr) != GetCurrentThreadId())
        return;
    if (std::any_of(windows.begin(), windows.end(), [&](const Entry& e) { return e.video == video; }))
        return;
    if (!hook)
        hook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,
                               EVENT_OBJECT_LOCATIONCHANGE,
                               nullptr,
                               Procedure,
                               GetCurrentProcessId(),
                               0,
                               WINEVENT_OUTOFCONTEXT);
    if (hook)
        windows.push_back({video, MonitorFromWindow(video, MONITOR_DEFAULTTONEAREST)});
}

inline void Detach(HWND video)
{
    windows.erase(std::remove_if(windows.begin(), windows.end(), [&](const Entry& e) { return e.video == video; }),
                  windows.end());
    if (windows.empty() && hook)
    {
        UnhookWinEvent(hook);
        hook = nullptr;
    }
}
}
