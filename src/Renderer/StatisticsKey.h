#pragma once
#include <windows.h>
#include <mutex>
#include <vector>
#include <algorithm>

// Thread-local message hooks only in this process, never a global keyboard hook.
// Consume Ctrl+J before the host's accelerator handling, and post to the
// renderer window. No renderer pointers are retained by the hook callback.
namespace StatisticsKey
{
inline constexpr UINT ToggleMessage = WM_APP + 0x4b1;

struct Entry
{
    HWND window;
    DWORD thread;
};

struct Hook
{
    DWORD thread;
    HHOOK handle;
};

inline std::mutex lock;
inline std::vector<Entry> windows;
inline std::vector<Hook> hooks;

inline bool Handle(MSG& message, bool control, bool alt, bool shift, HWND foreground)
{
    if (message.message != WM_KEYDOWN || message.wParam != 'J' || !control || alt || shift)
        return false;
    HWND target = nullptr;
    {
        std::lock_guard guard(lock);
        for (const auto& entry : windows)
            if (entry.thread == GetCurrentThreadId() && GetAncestor(entry.window, GA_ROOT) == foreground)
            {
                target = entry.window;
                break;
            }
    }
    if (!target)
        return false;
    if (!(message.lParam & (1LL << 30)))
        PostMessageW(target, ToggleMessage, 0, 0);
    message.message = WM_NULL;
    message.wParam = message.lParam = 0;
    return true;
}

inline LRESULT CALLBACK Procedure(int code, WPARAM w, LPARAM l)
{
    if (code >= 0 && w == PM_REMOVE)
        Handle(*reinterpret_cast<MSG*>(l),
               !!(GetKeyState(VK_CONTROL) & 0x8000),
               !!(GetKeyState(VK_MENU) & 0x8000),
               !!(GetKeyState(VK_SHIFT) & 0x8000),
               GetAncestor(GetForegroundWindow(), GA_ROOT));
    return CallNextHookEx(nullptr, code, w, l);
}

inline void Detach(HWND window)
{
    std::lock_guard guard(lock);
    windows.erase(std::remove_if(windows.begin(), windows.end(), [&](const Entry& e) { return e.window == window; }),
                  windows.end());
    for (auto it = hooks.begin(); it != hooks.end();)
        if (std::none_of(windows.begin(), windows.end(), [&](const Entry& e) { return e.thread == it->thread; }))
        {
            UnhookWindowsHookEx(it->handle);
            it = hooks.erase(it);
        }
        else
            ++it;
}

inline void Attach(HWND window)
{
    const HWND root = GetAncestor(window, GA_ROOT);
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(root, &process);
    if (!thread || process != GetCurrentProcessId())
        return;
    std::lock_guard guard(lock);
    for (const auto& entry : windows)
        if (entry.window == window && entry.thread == thread)
            return;
    windows.erase(std::remove_if(windows.begin(), windows.end(), [&](const Entry& e) { return e.window == window; }),
                  windows.end());
    if (std::none_of(hooks.begin(), hooks.end(), [&](const Hook& h) { return h.thread == thread; }))
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Procedure),
                           &module);
        HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, Procedure, module, thread);
        if (!hook)
            return;
        hooks.push_back({thread, hook});
    }
    windows.push_back({window, thread});
}
}
