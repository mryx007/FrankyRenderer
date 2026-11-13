#pragma once
#include "Settings/Shortcuts.h"
#include "UI/CustomShortcuts.h"
#include <set>

// The Shortcuts page: a tree like the menu. Renderer actions first, then each
// menu page with its sections, settings and what a key does to them (On/Off,
// Next, a value, Up, Down). A key field is clicked, then the next key
// combination is taken. Assignments apply at once to every playing video and
// are not part of Cancel.
namespace ShortcutsPage
{
// Bumped when the tree changes, so the open menu rebuilds this page.
inline int g_version = 0;
// Open branches by path, e.g. "Scaling" or "Scaling|Scaling|Filters". Menu
// state only.
inline std::set<std::wstring> g_open;
// The settings behind the key fields of the current tree; a field's shortcut
// is Shortcuts::Count + its index here.
inline std::vector<Shortcuts::Custom> g_leaves;

inline constexpr const wchar_t* MenuPages[] = {L"General", L"Processing", L"Scaling", L"Color", L"HDR", L"Shaders"};

// The menu page a settings model page is shown on.
inline std::wstring MenuPage(const std::wstring& page)
{
    if (page == L"Black Bars / Zoom")
        return L"Processing";
    if (page == L"Color Adjustment" || page == L"Color Management")
        return L"Color";
    if (page == L"HDR & Color Mapping")
        return L"HDR";
    return page;
}

// "Scaling › Image Upscaler: Next" in the menu language.
inline std::wstring CustomLabel(const Shortcuts::Custom& entry)
{
    static PlaceboSettings model;
    auto pages = BuildD2DPages(model);
    const auto* control = CustomShortcuts::Find(pages, entry);
    if (!control)
        return Tr(entry.name) + L" (" + Tr(L"not available") + L")";
    const auto where = Tr(entry.card) + L" \x203A " + Tr(control->name);
    if (entry.mode == Shortcuts::CustomMode::Toggle)
        return where;
    return where + L": " + Tr(CustomShortcuts::Describe(*control, entry));
}

inline bool Open(const std::wstring& path)
{
    // The translation template lists every text of the tree.
    return ProfilePage::g_template || g_open.count(path);
}

inline D2DControl Branch(const std::wstring& name, const std::wstring& path, int level, int count)
{
    D2DControl c;
    c.name = name;
    c.kind = ControlKind::Action;
    c.treeLevel = level;
    c.treeOpen = Open(path);
    c.treeCount = count;
    c.action = [path](HWND)
    {
        if (!g_open.erase(path))
            g_open.insert(path);
        ++g_version;
    };
    return c;
}

// A key field; help says what a key press does, %s takes the step.
inline D2DControl Leaf(const std::wstring& name,
                       int level,
                       const Shortcuts::Custom& target,
                       const wchar_t* help,
                       const std::wstring& step = {})
{
    D2DControl c;
    c.name = name;
    c.desc = help;
    if (!step.empty())
        c.detail = [help, step]
        {
            wchar_t text[256];
            swprintf_s(text, Tr(help).c_str(), step.c_str());
            return std::wstring(text);
        };
    c.kind = ControlKind::Shortcut;
    c.treeLevel = level;
    c.shortcut = Shortcuts::Count + int(g_leaves.size());
    g_leaves.push_back(target);
    c.get = [target] { return float(Shortcuts::CustomCombo(target)); };
    c.defaultValue = [] { return 0.f; };
    return c;
}

inline constexpr const wchar_t* Toggles = L"Switches on or off; each press changes it.";

inline D2DPage Build()
{
    using Shortcuts::Custom;
    using Shortcuts::CustomMode;
    D2DPage page{L"Shortcuts",
                 L"\xE765",
                 L"Shortcuts",
                 L"Keys for the playing video. Open a group, click a field and press the keys.",
                 {}};
    D2DCard tree{L"", L"", {}};
    g_leaves.clear();
    // The translation template must not pick up this PC's own keys.
    const auto assigned = ProfilePage::g_template ? std::vector<Custom>{} : Shortcuts::CachedCustom();
    const auto count = [&](auto match)
    { return int(std::count_if(assigned.begin(), assigned.end(), [&](const Custom& c) { return match(c); })); };

    // Reset Page: every key back to its default.
    D2DControl reset;
    reset.name = L"Reset Shortcuts";
    reset.kind = ControlKind::Shortcut;
    reset.hidden = true;
    reset.get = []
    {
        int changed = int(Shortcuts::CachedCustom().size());
        for (int i = 0; i < Shortcuts::Count; ++i)
            changed += Shortcuts::Cached()[i] != Shortcuts::DefaultCombo(Shortcuts::Action(i));
        return float(changed);
    };
    reset.defaultValue = [] { return 0.f; };
    reset.reset = []
    {
        Shortcuts::SaveCustom({});
        for (int i = 0; i < Shortcuts::Count; ++i)
            Shortcuts::Assign(Shortcuts::Action(i), 0);
        for (int i = 0; i < Shortcuts::Count; ++i)
            Shortcuts::Assign(Shortcuts::Action(i), Shortcuts::DefaultCombo(Shortcuts::Action(i)));
    };
    tree.controls.push_back(std::move(reset));

    // Actions of the renderer itself.
    {
        int keys = 0;
        for (int i = 0; i < Shortcuts::Count; ++i)
            keys += ProfilePage::g_template ? 0 : Shortcuts::Cached()[i] != 0;
        tree.controls.push_back(Branch(L"Renderer", L"Renderer", 0, keys));
        if (Open(L"Renderer"))
            for (int i = 0; i < Shortcuts::Count; ++i)
            {
                const auto action = Shortcuts::Action(i);
                D2DControl c;
                c.name = Shortcuts::Actions[i].name;
                c.kind = ControlKind::Shortcut;
                c.treeLevel = 1;
                c.desc = Shortcuts::Actions[i].help;
                c.shortcut = i;
                c.get = [i] { return float(Shortcuts::Cached()[i]); };
                c.defaultValue = [action] { return float(Shortcuts::DefaultCombo(action)); };
                tree.controls.push_back(std::move(c));
            }
    }

    // Every menu setting, in menu order.
    static PlaceboSettings model;
    const auto pages = BuildD2DPages(model);
    for (const wchar_t* menu : MenuPages)
    {
        std::vector<std::pair<const D2DPage*, const D2DCard*>> cards;
        for (const auto& p : pages)
            if (MenuPage(p.name) == menu)
                for (const auto& card : p.cards)
                    if (std::any_of(card.controls.begin(), card.controls.end(), CustomShortcuts::Usable))
                        cards.push_back({&p, &card});
        if (cards.empty())
            continue;
        tree.controls.push_back(
            Branch(menu, menu, 0, count([&](const Custom& c) { return MenuPage(c.page) == menu; })));
        if (!Open(menu))
            continue;
        for (const auto& [p, card] : cards)
        {
            std::vector<const D2DControl*> controls;
            for (const auto& control : card->controls)
                if (CustomShortcuts::Usable(control))
                    controls.push_back(&control);
            Custom target;
            target.page = p->name;
            target.card = card->title;
            const auto cardPath = std::wstring(menu) + L"|" + p->name + L"|" + card->title;
            // A section with a single switch is that switch.
            if (controls.size() == 1 && controls[0]->kind == ControlKind::Toggle)
            {
                target.name = controls[0]->name;
                tree.controls.push_back(Leaf(card->title, 1, target, Toggles));
                continue;
            }
            tree.controls.push_back(
                Branch(card->title,
                       cardPath,
                       1,
                       count([&](const Custom& c) { return c.page == target.page && c.card == target.card; })));
            if (!Open(cardPath))
                continue;
            for (const auto* control : controls)
            {
                target.name = control->name;
                if (control->kind == ControlKind::Toggle)
                {
                    target.mode = CustomMode::Toggle;
                    target.value = 0;
                    tree.controls.push_back(Leaf(control->name, 2, target, Toggles));
                    continue;
                }
                const auto path = cardPath + L"|" + control->name;
                tree.controls.push_back(
                    Branch(control->name,
                           path,
                           2,
                           count([&](const Custom& c)
                                 { return c.page == target.page && c.card == target.card && c.name == target.name; })));
                if (!Open(path))
                    continue;
                const auto leaf = [&](const std::wstring& name,
                                      CustomMode mode,
                                      float value,
                                      const wchar_t* help,
                                      const std::wstring& step = {})
                {
                    target.mode = mode;
                    target.value = value;
                    tree.controls.push_back(Leaf(name, 3, target, help, step));
                };
                if (control->kind == ControlKind::SliderInt || control->kind == ControlKind::SliderFloat)
                {
                    const auto step = CustomShortcuts::Number(CustomShortcuts::KeyStep(*control));
                    leaf(L"Increase", CustomMode::Up, 0, L"Raises the value by %s.", step);
                    leaf(L"Decrease", CustomMode::Down, 0, L"Lowers the value by %s.", step);
                }
                else
                {
                    leaf(L"Next", CustomMode::Next, 0, L"Next value; after the last one, the first again.");
                    for (const auto& item : control->choices)
                        leaf(item.label, CustomMode::Set, float(item.value), L"Sets exactly this value.");
                }
            }
        }
    }
    page.cards.push_back(std::move(tree));
    return page;
}
} // namespace ShortcutsPage
