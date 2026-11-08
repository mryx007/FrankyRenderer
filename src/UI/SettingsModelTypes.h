#pragma once
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The settings model: pages, cards and controls bound to a PlaceboSettings.
// The menu draws it; the renderer uses it to run custom keyboard shortcuts.
enum class ControlKind
{
    Toggle,
    SliderInt,
    SliderFloat,
    Choice,
    Action,
    Text,
    Shortcut, // a key field: click, then press the keys
    Tabs      // choices as a row of tabs across the card
};

struct ChoiceItem
{
    int value;
    std::wstring label;
    std::wstring description;
};

struct D2DControl
{
    std::wstring id; // English name; name is translated for display
    std::wstring name;
    std::wstring desc;
    ControlKind kind;
    float minVal = 0.0f;
    float maxVal = 100.0f;
    float stepVal = 1.0f;
    std::vector<ChoiceItem> choices;

    std::function<float()> get;
    std::function<float()> defaultValue;
    std::function<void(float)> set;
    std::function<void()> reset;
    std::function<bool()> enabled;
    std::function<void(HWND)> action;
    std::function<std::wstring()> detail;
    // Switch drawn before the name, e.g. whether a setting belongs to a profile.
    std::shared_ptr<D2DControl> include;
    // Text controls; setText rejects invalid input.
    std::function<std::wstring()> getText;
    std::function<bool(const std::wstring&)> setText;
    std::wstring placeholder, invalidText;
    std::wstring actionLabel;
    bool headerToggle = false;
    bool logarithmic = false;
    int shortcut = -1; // key field: a Shortcuts::Action, or Count + a tree row
    // Not drawn on its page; keyboard shortcuts use it.
    bool hidden = false;
    // A hidden control the search still lists when its card's name matches.
    bool searchByCard = false;
    // A key changes a number by this much; 0 picks a step from the range.
    float keyStep = 0;
    // A row of the shortcut tree: its depth, whether it is open, and how many
    // keys are assigned below it.
    int treeLevel = -1;
    bool treeOpen = false;
    int treeCount = 0;

    bool Enabled() const
    {
        return !enabled || enabled();
    }

    float Value() const
    {
        return get ? get() : 0;
    }

    float Default() const
    {
        return defaultValue ? defaultValue() : 0;
    }
};

struct D2DCard
{
    std::wstring title;
    std::wstring subtitle;
    std::vector<D2DControl> controls;
    std::wstring tab; // explicit tab, e.g. a profile name; otherwise TabGroup decides
};

struct D2DPage
{
    std::wstring name;
    std::wstring icon;
    std::wstring title;
    std::wstring subtitle;
    std::vector<D2DCard> cards;
};
