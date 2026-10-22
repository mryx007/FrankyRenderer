#define NOMINMAX
#include "ChoicePopup.h"
#include <cstdio>

static bool timerRan = false, inputPassed = false;
static ChoicePopup* current = nullptr;
static HWND page = nullptr;
static int scenario = 0;

static void CALLBACK Exercise(HWND, UINT, UINT_PTR timer, DWORD)
{
    KillTimer(nullptr, timer);
    timerRan = true;
    HWND popup = FindWindowW(L"FrankyChoicePopup", nullptr);
    if (!popup)
    {
        fprintf(stderr, "Popup missing in modal loop\n");
        PostQuitMessage(1);
        return;
    }
    inputPassed = IsWindowVisible(popup) && GetFocus() == popup && GetCapture() == popup && !current->done;
    SendMessageW(popup, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    inputPassed &= current->first == 3;
    switch (scenario)
    {
    case 1:
        SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
        break;
    case 2:
        SendMessageW(popup, WM_LBUTTONDOWN, 0, MAKELPARAM(-1, 10));
        break;
    case 3:
        SetFocus(page);
        break;
    case 4:
        SendMessageW(popup, WM_LBUTTONDOWN, 0, MAKELPARAM(40, current->rowHeight / 2));
        break;
    case 5:
        ReleaseCapture();
        break;
    default:
        SendMessageW(popup, WM_KEYDOWN, VK_END, 0);
        SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
        break;
    }
}

int main()
{
    // Exercise actual creation, activation and modal input on an invisible desktop.
    // The normal desktop and the user's applications are never switched or operated.
    HDESK original = GetThreadDesktop(GetCurrentThreadId());
    wchar_t name[80];
    swprintf_s(name, L"FrankyPopupTest-%lu", GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop))
    {
        fprintf(stderr, "Private test desktop unavailable: %lu\n", GetLastError());
        if (desktop)
            CloseDesktop(desktop);
        return 1;
    }
    HWND owner = CreateWindowExW(0,
                                 L"STATIC",
                                 L"Test owner",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 0,
                                 0,
                                 800,
                                 700,
                                 nullptr,
                                 nullptr,
                                 GetModuleHandleW(nullptr),
                                 nullptr);
    page = CreateWindowExW(0,
                           L"STATIC",
                           L"Test page",
                           WS_CHILD | WS_VISIBLE,
                           0,
                           0,
                           700,
                           600,
                           owner,
                           nullptr,
                           GetModuleHandleW(nullptr),
                           nullptr);
    SetFocus(page);
    int status = 0;
    for (scenario = 0; scenario < 9; ++scenario)
    {
        ChoicePopup popup;
        for (int i = 0; i < 30; ++i)
        {
            popup.labels.push_back(L"Choice " + std::to_wstring(i));
            if (scenario >= 6)
                popup.descriptions.push_back(L"A description of the scaling filter.");
        }
        popup.selected = 5;
        current = &popup;
        timerRan = inputPassed = false;
        const UINT_PTR timer = SetTimer(nullptr, 0, 100, Exercise);
        const float scale = scenario == 7 ? 1.5f : scenario == 8 ? 2.f : 1.f;
        const int result = popup.Show(page, {80, 80, 300, 110}, scale);
        KillTimer(nullptr, timer);
        const int expected = scenario == 4 ? 3 : scenario >= 1 && scenario <= 5 ? -1 : 29;
        WNDCLASSEXW wc = {sizeof(wc)};
        const bool leakedClass = GetClassInfoExW(GetModuleHandleW(nullptr), L"FrankyChoicePopup", &wc) != FALSE;
        if (result != expected || !timerRan || !inputPassed || GetFocus() != page || GetCapture() || popup.window ||
            leakedClass)
        {
            fprintf(stderr,
                    "FAIL popup scenario=%d result=%d expected=%d timer=%d input=%d\n",
                    scenario,
                    result,
                    expected,
                    timerRan,
                    inputPassed);
            status = 1;
            break;
        }
    }
    DestroyWindow(owner);
    SetThreadDesktop(original);
    CloseDesktop(desktop);
    if (!status)
        puts(
            "PASS real popup activation, wheel, keyboard/mouse selection, cancellation, focus/capture restoration, DPI and repeated class cleanup");
    return status;
}
