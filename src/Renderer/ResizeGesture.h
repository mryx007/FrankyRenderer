#pragma once
#include <windows.h>
#include <algorithm>
#include <vector>

// Observe only this process's native move/size loop. The player window is
// neither subclassed nor paused; video delivery continues while its image holds.
namespace ResizeGesture
{
inline constexpr UINT EndMessage = WM_APP + 0x4b3;
inline constexpr UINT_PTR Timer = 0x4652535a;
inline constexpr UINT QuietMilliseconds = 50;

struct Entry
{
    HWND video;
    HWND root = nullptr;
    RECT initial = {};
    bool active = false;
    ULONGLONG changedAt = 0;
};

inline thread_local std::vector<Entry> windows;
inline thread_local HWINEVENTHOOK hook = nullptr;

inline void CALLBACK Procedure(HWINEVENTHOOK, DWORD event, HWND root, LONG, LONG, DWORD, DWORD)
{
    for (auto& entry : windows)
    {
        if (event == EVENT_SYSTEM_MOVESIZESTART && GetAncestor(entry.video, GA_ROOT) == root)
        {
            entry.root = root;
            entry.active = GetClientRect(entry.video, &entry.initial) != FALSE;
        }
        else if (event == EVENT_SYSTEM_MOVESIZEEND && entry.active && entry.root == root)
        {
            entry.active = false;
            PostMessageW(entry.video, EndMessage, 0, 0);
        }
    }
}

inline bool Resizing(HWND video)
{
    for (auto& entry : windows)
    {
        RECT current = {};
        if (entry.video == video && entry.active && GetAncestor(video, GA_ROOT) == entry.root &&
            GetClientRect(video, &current))
        {
            if (current.right == entry.initial.right && current.bottom == entry.initial.bottom)
                return false;
            entry.initial = current;
            entry.changedAt = GetTickCount64();
            return true;
        }
    }
    return false;
}

inline UINT Remaining(HWND video)
{
    for (const auto& entry : windows)
    {
        if (entry.video == video && entry.active)
        {
            const auto elapsed = GetTickCount64() - entry.changedAt;
            return elapsed < QuietMilliseconds ? UINT(QuietMilliseconds - elapsed) : 0;
        }
    }
    return 0;
}

inline void Attach(HWND video)
{
    if (GetWindowThreadProcessId(video, nullptr) != GetCurrentThreadId())
        return;
    if (std::any_of(windows.begin(), windows.end(), [&](const Entry& e) { return e.video == video; }))
        return;
    if (!hook)
        hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART,
                               EVENT_SYSTEM_MOVESIZEEND,
                               nullptr,
                               Procedure,
                               GetCurrentProcessId(),
                               0,
                               WINEVENT_OUTOFCONTEXT);
    if (hook)
        windows.push_back({video});
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
