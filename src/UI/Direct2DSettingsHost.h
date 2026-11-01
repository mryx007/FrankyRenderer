#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "Settings/Settings.h"

class Direct2DSettingsHost
{
  public:
    static HWND Create(HWND parent,
                       const RECT& bounds,
                       PlaceboSettings* settings,
                       bool standalone = false,
                       std::function<HRESULT(const PlaceboSettings&)> preview = {},
                       bool displayProfiles = false);
    static void Destroy(HWND hwnd);
    static void Resize(HWND hwnd, const RECT& bounds);
    static void Save(HWND hwnd, PlaceboSettings* outSettings);
    // Every English menu text with a note for translators, in menu order.
    static std::vector<std::pair<std::wstring, std::wstring>> TranslatableTexts();
};
