#pragma once
#include "Settings.h"
#include <commctrl.h>
#include <functional>
#include <map>
#include <cwctype>
#pragma comment(lib, "comctl32.lib")

// Event-driven live preview; no timer or playback polling. The host's OK
// commits the session, Cancel / close restores its opening snapshot.
namespace LiveSettings
{
inline thread_local bool silent = false;

struct Quiet
{
    bool previous = silent;

    Quiet()
    {
        silent = true;
    }

    ~Quiet()
    {
        silent = previous;
    }
};

inline int Message(HWND h, LPCWSTR text, LPCWSTR title, UINT flags)
{
    return silent ? IDOK : MessageBoxW(h, text, title, flags);
}

inline HWND Focus(HWND h)
{
    return silent ? nullptr : SetFocus(h);
}

inline bool Enabled()
{
    wchar_t root[2], defer[2];
    return !(GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root, 2) &&
             GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_DEFER_APPLY", defer, 2));
}

inline constexpr UINT ApplyMessage = WM_APP + 219;

struct Session
{
    IPlaceboSettings* config;
    PlaceboSettings opening;
    bool commit = false;
    std::map<std::wstring, PlaceboSettings> displayOpenings;
    std::map<std::wstring, DisplayProfiles::Metadata> metadataOpenings;
};

inline thread_local std::map<HWND, Session*> sessions;

inline BOOL CALLBACK HideApply(HWND h, LPARAM)
{
    wchar_t type[32], text[80];
    GetClassNameW(h, type, 32);
    GetWindowTextW(h, text, 80);
    if (!_wcsicmp(type, L"Button") &&
        (GetDlgCtrlID(h) == 0x3021 || !wcscmp(text, L"Apply") || !wcscmp(text, L"\u00dcbernehmen")))
        ShowWindow(h, SW_HIDE);
    return TRUE;
}

inline LRESULT CALLBACK Host(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    auto s = reinterpret_cast<Session*>(data);
    if (msg == WM_COMMAND)
    {
        if (LOWORD(w) == IDOK)
            s->commit = true;
        if (LOWORD(w) == IDCANCEL)
            s->commit = false;
    }
    if (msg == WM_CLOSE)
        s->commit = false;
    if (msg == WM_SHOWWINDOW || msg == WM_WINDOWPOSCHANGED)
        EnumChildWindows(h, HideApply, 0);
    if (msg == WM_NCDESTROY)
    {
        if (!s->commit)
        {
            if (s->displayOpenings.empty())
                s->config->SetSettings(&s->opening);
            else
                for (const auto& [profileId, opening] : s->displayOpenings)
                    s->config->SetSettings(&opening);
            for (const auto& [profileId, metadata] : s->metadataOpenings)
                DisplayProfiles::WriteMetadata(profileId, metadata);
        }
        sessions.erase(h);
        s->config->Release();
        RemoveWindowSubclass(h, Host, id);
        delete s;
    }
    return DefSubclassProc(h, msg, w, l);
}

struct Page
{
    std::function<HRESULT()> apply;
    bool pending = false, running = false;
};

inline bool Incomplete(HWND h)
{
    wchar_t cls[32] = {};
    GetClassNameW(h, cls, 32);
    if (_wcsicmp(cls, L"Edit") && _wcsicmp(cls, L"ComboBox"))
        return false;
    wchar_t text[1024] = {};
    GetWindowTextW(h, text, 1024);
    size_t n = wcslen(text);
    return !n || !wcscmp(text, L"-") || !wcscmp(text, L"+") || text[n - 1] == L'.' || text[n - 1] == L',';
}

inline LRESULT CALLBACK PageProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data)
{
    auto p = reinterpret_cast<Page*>(data);
    if (msg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(h, PageProc, id);
        auto result = DefSubclassProc(h, msg, w, l);
        delete p;
        return result;
    }
    if (msg == ApplyMessage)
    {
        p->pending = false;
        if (!p->running && !Incomplete(GetFocus()))
        {
            p->running = true;
            {
                Quiet quiet;
                p->apply();
            }
            p->running = false;
        }
        return 0;
    }
    auto result = DefSubclassProc(h, msg, w, l);
    DWORD_PTR current = 0;
    if (!GetWindowSubclass(h, PageProc, id, &current) || current != data)
        return result;
    if (msg == WM_COMMAND && !p->running)
    {
        const auto e = HIWORD(w);
        if (e == EN_CHANGE || e == CBN_SELCHANGE || e == CBN_EDITCHANGE || e == BN_CLICKED || e == LBN_DBLCLK)
        {
            if (!p->pending)
            {
                p->pending = true;
                PostMessageW(h, ApplyMessage, 0, 0);
            }
        }
    }
    return result;
}

inline void AttachEditor(HWND page, std::function<HRESULT()> apply)
{
    if (!Enabled())
        return;
    DWORD_PTR existing = 0;
    if (!GetWindowSubclass(page, PageProc, 219, &existing))
    {
        auto p = new Page{std::move(apply)};
        if (!SetWindowSubclass(page, PageProc, 219, reinterpret_cast<DWORD_PTR>(p)))
            delete p;
    }
}

inline void Attach(HWND page, IPlaceboSettings* config, std::function<HRESULT()> apply)
{
    if (!Enabled())
        return;
    const HWND host = GetAncestor(page, GA_ROOT);
    if (!sessions.count(host))
    {
        auto s = new Session{config};
        if (FAILED(config->GetSettings(&s->opening)))
        {
            delete s;
            return;
        }
        config->AddRef();
        s->displayOpenings.emplace(s->opening.displayProfile, s->opening);
        if (!SetWindowSubclass(host, Host, 219, reinterpret_cast<DWORD_PTR>(s)))
        {
            config->Release();
            delete s;
            return;
        }
        sessions[host] = s;
    }
    AttachEditor(page, std::move(apply));
    EnumChildWindows(host, HideApply, 0);
}

inline void RememberMetadata(HWND page, const std::wstring& id)
{
    const auto found = sessions.find(GetAncestor(page, GA_ROOT));
    if (found != sessions.end() && !found->second->metadataOpenings.count(id))
        found->second->metadataOpenings.emplace(id, DisplayProfiles::ReadMetadata(id));
}

inline void RememberDisplay(HWND page, const PlaceboSettings& next)
{
    const auto found = sessions.find(GetAncestor(page, GA_ROOT));
    if (found == sessions.end() || !next.displayProfile[0])
        return;
    auto& openings = found->second->displayOpenings;
    if (!openings.count(next.displayProfile))
        openings.emplace(next.displayProfile, LoadSettings(next.displayProfile));
}
}
