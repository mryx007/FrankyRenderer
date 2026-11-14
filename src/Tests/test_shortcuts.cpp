#include "Renderer/StatisticsKey.h"
#include "UI/CustomShortcuts.h"
#include <cstdio>
#include <cstdlib>

// Keyboard shortcuts: names, defaults, conflicts, the key hook lookup and the
// setting changes behind each action. Uses a disposable registry root.
static void Require(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

int main()
{
    using namespace Shortcuts;
    wchar_t root[128];
    swprintf_s(root, L"Software\\PlaceboRendererShortcutTest%lu", GetCurrentProcessId());
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root);
    RegDeleteTreeW(HKEY_CURRENT_USER, root);

    Require(Format(Combo('J', true, false, false)) == L"Ctrl+J", "Ctrl+J name");
    Require(Format(Combo(VK_F5, true, true, true)) == L"Ctrl+Alt+Shift+F5", "modifier order and F keys");
    Require(Format(Combo(VK_NUMPAD3, false, false, false)) == L"Num 3" && Format(0).empty(), "numpad and none");
    Require(Format(Combo(VK_PRIOR, true, false, true),
                   [](const std::wstring& name)
                   { return name == L"Ctrl" ? std::wstring(L"Strg") : name; }) == L"Strg+Shift+Page Up",
            "key names in the menu language");
    Require(ModifierKey(VK_SHIFT) && ModifierKey(VK_LCONTROL) && !ModifierKey('A'), "modifier keys");

    auto table = Load();
    Require(table[int(Action::Statistics)] == Combo('J', true, false, false), "Ctrl+J is the default");
    for (int i = 1; i < Count; ++i)
        Require(!table[i], "other actions start without a key");

    const DWORD altS = Combo('S', false, true, false);
    Require(Assign(Action::Quality, altS) < 0, "assign Alt+S");
    Require(Assign(Action::Profile, altS) == int(Action::Quality), "a used combination is refused");
    Require(Assign(Action::Profile, Combo('J', true, false, false)) == int(Action::Statistics),
            "Ctrl+J stays with the statistics");
    Require(Load()[int(Action::Quality)] == altS && !Load()[int(Action::Profile)], "refusal saves nothing");
    Require(Cached()[int(Action::Quality)] == altS, "assigning refreshes the cache");
    Require(StatisticsKey::ActionFor(WM_SYSKEYDOWN, 'S', false, true, false) == int(Action::Quality),
            "Alt combinations arrive as system keys");
    Require(StatisticsKey::ActionFor(WM_KEYDOWN, 'S', false, false, false) < 0, "plain S is not Alt+S");
    Require(StatisticsKey::ActionFor(WM_KEYDOWN, VK_CONTROL, true, false, false) < 0, "a modifier alone does nothing");
    Require(StatisticsKey::ActionFor(WM_KEYDOWN, 'J', true, false, false) == int(Action::Statistics), "Ctrl+J");
    Require(Assign(Action::Statistics, 0) < 0 && !Load()[int(Action::Statistics)], "Ctrl+J can be removed");
    Require(StatisticsKey::ActionFor(WM_KEYDOWN, 'J', true, false, false) < 0, "removed Ctrl+J is free again");

    PlaceboSettings s;
    s.brightness = .5f;
    s.gamma = 2;
    ResetPicture(s);
    Require(s.brightness == 0 && s.gamma == 1 && s.contrast == 1 && s.saturation == 1, "picture controls reset");
    Require(SUCCEEDED(ValidateSettings(s)), "settings stay valid");

    // Custom entries: any menu setting through the settings model.
    {
        PlaceboSettings model;
        auto pages = BuildD2DPages(model);
        Custom toggle, choice, number;
        for (const auto& page : pages)
            for (const auto& card : page.cards)
                for (const auto& c : card.controls)
                {
                    if (!CustomShortcuts::Usable(c) || !c.Enabled())
                        continue;
                    auto& slot = c.kind == ControlKind::Toggle                               ? toggle
                                 : c.kind == ControlKind::Choice && c.choices.size() > 2     ? choice
                                 : c.kind == ControlKind::SliderFloat && c.maxVal > c.minVal ? number
                                                                                             : toggle;
                    if (slot.name.empty())
                        slot = {0, CustomMode::Toggle, 0, page.name, card.title, c.name};
                }
        Require(!toggle.name.empty() && !choice.name.empty() && !number.name.empty(), "model offers every kind");
        choice.mode = CustomMode::Next;
        number.mode = CustomMode::Up;
        toggle.combo = Combo(VK_F7, true, false, false);
        choice.combo = Combo(VK_F8, true, false, false);
        Require(AssignTarget(toggle, toggle.combo) < 0 && AssignTarget(choice, choice.combo) < 0 &&
                    AssignTarget(number, Combo(VK_F6, true, false, false)) < 0 && LoadCustom().size() == 3,
                "keys for settings saved");
        Require(AssignTarget(number, 0) < 0 && LoadCustom().size() == 2, "removing keys drops the entry");
        Require(AssignTarget(number, 0) < 0 && AssignTarget(number, Combo(VK_F6, true, false, false)) < 0 &&
                    LoadCustom().size() == 3 && CustomCombo(number) == Combo(VK_F6, true, false, false),
                "keys added again");
        Custom other = choice;
        other.mode = CustomMode::Set;
        Require(AssignTarget(other, choice.combo) == Count + 1, "another effect cannot take the same keys");
        const auto loaded = LoadCustom();
        Require(loaded[1].page == choice.page && loaded[1].card == choice.card && loaded[1].name == choice.name &&
                    loaded[1].mode == CustomMode::Next && loaded[1].combo == choice.combo,
                "custom entry round trip");
        Require(FindAny(choice.combo) == Count + 1, "custom keys found after the fixed actions");
        Require(StatisticsKey::ActionFor(WM_KEYDOWN, VK_F8, true, false, false) == Count + 1,
                "key hook finds custom keys");
        Require(Assign(Action::Profile, toggle.combo) == Count, "fixed action cannot take a setting's key");
        Require(AssignTarget(number, altS) == int(Action::Quality), "a setting cannot take a fixed key");
        Require(AssignTarget(number, Combo(VK_F9, true, false, false)) < 0 &&
                    LoadCustom()[2].combo == Combo(VK_F9, true, false, false),
                "keys replaced");
        PlaceboSettings s;
        auto edit = BuildD2DPages(s);
        auto* c = CustomShortcuts::Find(edit, toggle);
        const float before = c->Value();
        Require(CustomShortcuts::Apply(edit, toggle) && c->Value() != before, "toggle changes its switch");
        c = CustomShortcuts::Find(edit, choice);
        const float first = c->Value();
        Require(CustomShortcuts::Apply(edit, loaded[1]) && c->Value() != first, "next value of a choice");
        choice.mode = CustomMode::Set;
        choice.value = float(c->choices.back().value);
        Require(CustomShortcuts::Apply(edit, choice) && c->Value() == choice.value, "a fixed choice value");
        c = CustomShortcuts::Find(edit, number);
        const float low = c->Value();
        CustomShortcuts::Apply(edit, number);
        Require(c->Value() > low || low >= c->maxVal, "number steps up");
        Require(SUCCEEDED(ValidateSettings(s)), "custom changes keep valid settings");
        Shortcuts::Custom missing = toggle;
        missing.name = L"No Such Setting";
        Require(!CustomShortcuts::Apply(edit, missing), "a removed setting does nothing");
        // Every shader can be switched by a key; the menu does not draw that switch.
        Custom shader{0, CustomMode::Toggle, 0, L"Shaders", ShaderCatalog[0].label, L"On/Off"};
        c = CustomShortcuts::Find(edit, shader);
        Require(c && c->hidden, "hidden shader switch");
        const bool was = s.userShaders.shaders[0].enabled != 0;
        CustomShortcuts::Apply(edit, shader);
        Require((s.userShaders.shaders[0].enabled != 0) != was, "key switches the shader");
        Custom brightness{0, CustomMode::Up, 0, L"Color Adjustment", L"Picture Controls", L"Brightness"};
        s.colorAdjustment = TRUE;
        s.brightness = 0;
        CustomShortcuts::Apply(edit, brightness);
        Require(std::abs(s.brightness - .02f) < 1e-6f, "brightness keeps its key step");
        Custom position{0, CustomMode::Next, 0, L"Shaders", ShaderCatalog[0].label, L"Chain Position"};
        Require(!CustomShortcuts::Find(edit, position), "chain position has no keys");
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, root);
    puts("PASS shortcuts: names, Ctrl+J default, conflicts, removal, key hook lookup and setting actions");
}
