#include "Renderer/WindowsHdrSwitch.h"
#include <cstdio>
#include <cstdlib>

using namespace WindowsHdr;

static void Check(bool ok, const char* message)
{
    if (ok)
        return;
    printf("FAIL %s\n", message);
    exit(1);
}

static Action Case(int toggle, bool hdr, bool fullscreen, bool enabled, bool atStart, bool rtx = false)
{
    return Decide({toggle, hdr, rtx, fullscreen, enabled, atStart});
}

int main()
{
    Check(Case(Disabled, true, true, false, false) == Action::Keep, "disabled leaves HDR video alone");
    Check(Case(Disabled, false, true, true, true) == Action::Keep, "disabled leaves SDR video alone");

    Check(Case(OnFullscreen, true, false, false, false) == Action::Keep, "fullscreen stage waits for fullscreen");
    Check(Case(OnFullscreen, true, true, false, false) == Action::TurnOn, "fullscreen stage turns on in fullscreen");
    Check(Case(On, true, false, false, false) == Action::TurnOn, "turn on works in a window");
    Check(Case(OnOff, true, false, false, false) == Action::TurnOn, "turn on/off works in a window");
    Check(Case(OnOffFullscreen, true, false, true, false) == Action::TurnOff,
          "leaving fullscreen turns our HDR off again");
    Check(Case(OnOffFullscreen, true, false, true, true) == Action::Keep, "HDR the user had on stays on");

    Check(Case(OnOff, false, false, true, true) == Action::TurnOff, "turn on/off: SDR video turns HDR off");
    Check(Case(On, false, false, true, true) == Action::Keep, "turn on never turns the user's HDR off");
    Check(Case(On, false, false, true, false) == Action::TurnOff, "SDR after HDR drops the HDR we turned on");
    Check(Case(OnOffFullscreen, false, true, true, true) == Action::TurnOff, "fullscreen SDR turns HDR off");
    Check(Case(OnOffFullscreen, false, false, true, true) == Action::Keep, "windowed SDR keeps the user's HDR");
    Check(Case(OnOffFullscreen, false, false, false, true) == Action::TurnOn, "leaving fullscreen gives HDR back");
    Check(Case(OnOff, false, false, false, true) == Action::Keep, "turn on/off keeps SDR video in SDR");
    Check(Case(OnOff, false, true, true, true, true) == Action::Keep, "RTX Video HDR keeps Windows HDR on");
    Check(Case(On, false, true, true, false, true) == Action::Keep, "RTX Video HDR keeps the HDR we turned on");
    puts("PASS Windows HDR decisions");

    Display display;
    display.supported = true;
    int sets = 0;
    bool lastSet = false;
    {
        Switcher switcher([&](const wchar_t*) { return std::optional<Display>(display); },
                          [&](const wchar_t*, const Display&, bool enable)
                          {
                              ++sets;
                              lastSet = enable;
                              display.enabled = enable;
                              return true;
                          });
        auto r = switcher.Update(L"D", OnOff, false, true, false, true);
        Check(r.known && !r.switched && !sets, "nothing happens before the first picture");
        r = switcher.Update(L"D", OnOff, true, true, false, true);
        Check(r.switched && r.enabled && sets == 1 && lastSet && switcher.Active(), "HDR video turns HDR on");
        r = switcher.Update(L"D", OnOff, true, true, false, true);
        Check(!r.switched && r.enabled && sets == 1, "no second switch");
        switcher.Restore();
        Check(sets == 2 && !lastSet && !display.enabled && !switcher.Active(), "restore goes back to off");

        display.enabled = true;
        sets = 0;
        r = switcher.Update(L"D", OnOff, true, false, false, true);
        Check(r.switched && !r.enabled && sets == 1 && !lastSet, "SDR video turns the user's HDR off");
    }
    Check(display.enabled && sets == 2 && lastSet, "destructor gives the user's HDR back");

    Display unsupported;
    sets = 0;
    {
        Switcher switcher([&](const wchar_t*) { return std::optional<Display>(unsupported); },
                          [&](const wchar_t*, const Display&, bool)
                          {
                              ++sets;
                              return true;
                          });
        Check(!switcher.Update(L"D", On, true, true, false, true).known && !sets, "SDR-only display is left alone");
    }

    display = {};
    display.supported = true;
    {
        Switcher switcher([&](const wchar_t*) { return std::optional<Display>(display); },
                          [&](const wchar_t*, const Display&, bool) { return false; });
        const auto r = switcher.Update(L"D", On, true, true, false, true);
        Check(r.known && !r.switched && !r.enabled && !switcher.Active(), "a refused switch changes nothing");
    }
    puts("PASS Windows HDR switcher: start state, restore, unsupported display, refused switch");
    return 0;
}
