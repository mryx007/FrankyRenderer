#pragma once
#include "UI/Direct2DSettingsModel.h"
#include "Settings/Shortcuts.h"
#include <cmath>
#include <cstdio>

// Custom shortcuts act on the settings model: the same controls the menu
// shows, found by page, card and English name, so every setting works
// without a list of its own.
namespace CustomShortcuts
{
// Controls a key can change: switches, choices and numbers. The shader chain
// order is arranged in the menu, not by keys.
inline bool Usable(const D2DControl& c)
{
    return c.set && c.get && c.name != L"Chain Position" &&
           (c.kind == ControlKind::Toggle || c.kind == ControlKind::Choice || c.kind == ControlKind::Tabs ||
            c.kind == ControlKind::SliderInt || c.kind == ControlKind::SliderFloat);
}

inline D2DControl* Find(std::vector<D2DPage>& pages, const Shortcuts::Custom& entry)
{
    for (auto& page : pages)
        if (page.name == entry.page)
            for (auto& card : page.cards)
                if (card.title == entry.card)
                    for (auto& control : card.controls)
                        if (control.name == entry.name && Usable(control))
                            return &control;
    return nullptr;
}

inline const ChoiceItem* Choice(const D2DControl& c, float value)
{
    for (const auto& item : c.choices)
        if (float(item.value) == value)
            return &item;
    return nullptr;
}

inline std::wstring Number(float value)
{
    wchar_t text[32];
    swprintf_s(text, L"%.3g", double(value));
    return text;
}

// The value as the menu shows it: On/Off, the choice's label, or the number.
// Labels are English; the caller translates them.
inline std::wstring ValueText(const D2DControl& c, float value)
{
    if (c.kind == ControlKind::Toggle)
        return value ? L"On" : L"Off";
    if (const auto* item = Choice(c, value))
        return item->label;
    return Number(value);
}

// What the key does, for the row on the Shortcuts page (English).
inline std::wstring Describe(const D2DControl& c, const Shortcuts::Custom& entry)
{
    using Shortcuts::CustomMode;
    switch (entry.mode)
    {
    case CustomMode::Toggle:
        return L"On/Off";
    case CustomMode::Next:
        return L"Next";
    case CustomMode::Set:
        return ValueText(c, entry.value);
    case CustomMode::Up:
        return L"Increase";
    case CustomMode::Down:
        return L"Decrease";
    }
    return {};
}

// One key press: the control's own step, else about a hundredth of the
// range, never finer than the menu's step.
inline float KeyStep(const D2DControl& c)
{
    if (c.keyStep > 0)
        return c.keyStep;
    const float fine = c.stepVal > 0 ? c.stepVal : 1.f;
    if (c.kind == ControlKind::SliderInt || c.maxVal <= c.minVal)
        return fine;
    return std::max(fine, std::pow(10.f, std::floor(std::log10(c.maxVal - c.minVal)) - 2));
}

// Applies an entry to the settings. Returns the control so the caller can
// show "name: value", or nullptr when the setting no longer exists.
inline D2DControl* Apply(std::vector<D2DPage>& pages, const Shortcuts::Custom& entry)
{
    using Shortcuts::CustomMode;
    auto* c = Find(pages, entry);
    // A setting the menu shows greyed out has no effect now.
    if (!c || !c->Enabled())
        return nullptr;
    const float current = c->Value();
    switch (entry.mode)
    {
    case CustomMode::Toggle:
        c->set(current ? 0.f : 1.f);
        break;
    case CustomMode::Next:
        if (!c->choices.empty())
        {
            size_t at = 0;
            while (at < c->choices.size() && float(c->choices[at].value) != current)
                ++at;
            c->set(float(c->choices[at >= c->choices.size() ? 0 : (at + 1) % c->choices.size()].value));
        }
        break;
    case CustomMode::Set:
        c->set(entry.value);
        break;
    case CustomMode::Up:
    case CustomMode::Down:
    {
        const float step = KeyStep(*c);
        float next = current + (entry.mode == CustomMode::Up ? step : -step);
        next = std::round(next / step) * step;
        if (c->maxVal > c->minVal)
            next = std::clamp(next, c->minVal, c->maxVal);
        c->set(next);
        break;
    }
    }
    return c;
}
} // namespace CustomShortcuts
