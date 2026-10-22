#include "Direct2DSettingsHost.cpp"
#include <stdexcept>

static void Require(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}

int wmain()
{
    try
    {
        PlaceboSettings s;
        auto pages = BuildD2DPages(s);
        int controls = 0, parameters = 0;
        for (const auto& info : ShaderCatalog)
            parameters += info.count;
        Require(pages.size() == 8, "all sections");
        Require(MatchesFilter(L"HDR Highlights Exposure", L"  exposure HDR "),
                "search ignores case and outer spaces; all words match");
        Require(!MatchesFilter(L"HDR Highlights Exposure", L"exposure Gamma"), "search excludes missing terms");
        Require(MatchesFilter(L"Anything", L" \t"), "empty search");
        auto& hdrControls = pages[5].cards[0].controls;
        bool hasProtection = false;
        for (auto& control : hdrControls)
        {
            if (control.name == L"HDR Mode")
                Require(control.choices.size() == 2, "protection is not an HDR mode");
            if (control.name == L"Clipping Protection")
            {
                hasProtection = true;
                control.set(1);
                Require(s.clippingProtection && s.hdrMode == 2, "switch does not change HDR mode");
                s.hdrMode = 1;
                Require(!control.enabled(), "pure passthrough bypasses protection");
                s.hdrMode = 2;
                control.set(0);
            }
        }
        Require(hasProtection, "independent protection switch exists");
        int exposureHits = 0, combinedHits = 0;
        for (const auto& p : pages)
            for (const auto& card : p.cards)
                for (const auto& c : card.controls)
                {
                    if (ControlMatches(c, card, p, L"exposure"))
                        exposureHits++;
                    if (ControlMatches(c, card, p, L"highlights exposure"))
                        combinedHits++;
                }
        Require(exposureHits >= 4 && combinedHits == 1, "search across sections and group names");
        for (auto& p : pages)
            for (auto& card : p.cards)
                for (auto& c : card.controls)
                {
                    controls++;
                    if (c.get)
                        Require(std::isfinite(c.Value()), "finite displayed default");
                    if (c.desc.empty())
                    {
                        fwprintf(stderr,
                                 L"Missing explanation: %ls / %ls / %ls\n",
                                 p.name.c_str(),
                                 card.title.c_str(),
                                 c.name.c_str());
                        Require(false, "every control has an explanation");
                    }
                    if (c.kind == ControlKind::SliderInt || c.kind == ControlKind::SliderFloat)
                    {
                        Require(bool(c.defaultValue), "every slider has a real default");
                        if (c.Default() < c.minVal || c.Default() > c.maxVal)
                        {
                            fwprintf(stderr,
                                     L"Default outside range: %ls / %ls: %g [%g, %g]\n",
                                     p.name.c_str(),
                                     c.name.c_str(),
                                     double(c.Default()),
                                     double(c.minVal),
                                     double(c.maxVal));
                            Require(false, "default inside supported range");
                        }
                        if (c.Default() == c.minVal || c.Default() == c.maxVal)
                        {
                            Require(SliderFraction(c, c.minVal) == 0 && SliderFraction(c, c.maxVal) == 1,
                                    "boundary defaults map to slider ends");
                            Require(SliderValue(c, 0) == c.minVal && SliderValue(c, 1) == c.maxVal,
                                    "boundary slider reaches both bounds");
                        }
                        else
                            Require(SliderFraction(c, c.Default()) == .5f && SliderValue(c, .5f) == c.Default(),
                                    "interior defaults stay centered, including positive-only ranges");
                        for (float f : {0.f, .1f, .25f, .5f, .75f, .9f, 1.f})
                        {
                            float v = SliderValue(c, f);
                            Require(std::isfinite(v) && v >= c.minVal && v <= c.maxVal, "slider stays in range");
                        }
                    }
                }
        auto find = [&](const wchar_t* page, const wchar_t* card, const wchar_t* name) -> D2DControl&
        {
            for (auto& p : pages)
                if (p.name == page)
                    for (auto& g : p.cards)
                        if (g.title == card)
                            for (auto& c : g.controls)
                                if (c.name == name)
                                    return c;
            throw std::runtime_error("missing control");
        };
        auto& algorithm = find(L"Processing", L"Deinterlacing", L"Algorithm");
        Require(algorithm.Value() == 1 && algorithm.choices[1].label == L"Bwdif", "Bwdif index");
        algorithm.set(2);
        Require(s.deinterlacing == 2, "Yadif index");
        algorithm.reset();
        auto& temp = find(L"Color Adjustment", L"Picture Controls", L"Temperature (K)");
        Require(temp.maxVal == 6500, "temperature capped at neutral white");
        temp.set(3000);
        Require(std::abs(s.temperature + 1) < 1e-6, "Kelvin conversion");
        Require(temp.Value() == 3000, "Kelvin readback");
        temp.reset();
        auto& tone = find(L"HDR & Color Mapping", L"HDR Output", L"Tone Mapping Function");
        tone.set(1);
        Require(s.toneMapping == 1 && tone.choices[1].label == L"Spline", "Spline index");
        tone.reset();
        auto& scaler = find(L"Scaling", L"Filters", L"Image Upscaler");
        bool documentedSupported = true;
        const int expectedCounts[] = {13, 11, 13, 11, 7};
        for (int role = 0; role < 5; ++role)
        {
            const auto names = D2DScalerNames(role, true, "auto");
            Require(int(names.size()) == expectedCounts[role], "curated scaling preset count");
            for (const auto& name : names)
            {
                Require(!D2DScalerDescription(role, name.c_str()).empty(), "each scaling preset has a description");
                if (name == "custom")
                    continue;
                ScalingSettings test;
                strcpy_s(test.filters[role].filter, name.c_str());
                if (!ValidScaling(test))
                    fprintf(stderr, "Unsupported documented filter: role=%d name=%s\n", role, name.c_str());
                documentedSupported &= ValidScaling(test);
            }
        }
        Require(documentedSupported, "documented filters supported by bundled runtime");
        for (int role : {1, 3})
        {
            ScalingSettings test;
            strcpy_s(test.filters[role].filter, "box");
            auto options = pl_options_alloc(nullptr);
            Require(ApplyScaling(options, test) && ScalingFilter(options, role) == &pl_filter_box,
                    "box selects the actual built-in averaging filter");
            strcpy_s(test.filters[role].filter, "custom");
            strcpy_s(test.filters[role].preset, "box");
            Require(ApplyScaling(options, test) && ScalingFilter(options, role)->kernel == pl_filter_box.kernel,
                    "custom filter can inherit box averaging");
            pl_options_free(&options);
        }
        const auto savedNames = D2DScalerNames(0, true, "spline36");
        Require(savedNames.back() == "spline36", "existing non-curated setting remains represented");
        scaler.set(2);
        Require(!strcmp(s.scaling.filters[0].filter, "custom"), "canonical scaling");
        auto& blur = find(L"Scaling", L"Custom: Image Upscaler", L"Blur");
        Require(blur.Enabled(), "custom editing enabled");
        blur.set(1.1f);
        Require((s.scaling.filters[0].overrides & 4) && std::abs(s.scaling.filters[0].values[2] - 1.1f) < 1e-6,
                "scaling override");
        Require(SUCCEEDED(ValidateSettings(s)), "valid custom scaling");
        scaler.reset();
        Require(!blur.Enabled(), "preset disables editor");
        auto& exposure = find(L"HDR & Color Mapping", L"Highlights", L"Exposure");
        exposure.set(-.001f);
        Require(s.tonal.exposure[2] == -.001f && exposure.stepVal == .001f, "fine highlight adjustment");
        auto& order = find(L"Shaders", L"AMD CAS", L"Chain Position");
        order.set(20);
        Require(s.userShaders.order[20] == 0 && ValidUserShaders(s.userShaders), "shader reorder permutation");
        order.reset();
        auto& recovery = find(L"HDR & Color Mapping", L"Contrast Recovery", L"Contrast Recovery");
        s.contrastRecovery = .5f;
        Require(recovery.Value() == .5f, "preserve legacy values");
        recovery.set(.7f);
        Require(std::abs(D2DMappingValue(s, MappingIndex("contrast_recovery")) - .7f) < 1e-6, "mapping override");
        for (auto& p : pages)
            for (auto& card : p.cards)
                for (auto& c : card.controls)
                    if (c.reset)
                        c.reset();
        Require(SUCCEEDED(ValidateSettings(s)), "reset entire menu valid");
        Require(s.colorMapping.values[MappingIndex("contrast_recovery")][0] == 0 && s.contrastRecovery == 0,
                "reset override and legacy");
        Require(parameters > 60 && controls > 200, "shader parameters and advanced options covered");
        HWND parent = CreateWindowExW(0,
                                      L"STATIC",
                                      L"Hidden menu rendering test",
                                      WS_POPUP,
                                      -10000,
                                      -10000,
                                      1100,
                                      850,
                                      nullptr,
                                      nullptr,
                                      GetModuleHandleW(nullptr),
                                      nullptr);
        Require(parent != nullptr, "hidden rendering parent");
        RECT bounds = {0, 0, 1100, 850};
        HWND view = Direct2DSettingsHost::Create(parent, bounds, &s);
        Require(view != nullptr, "rendering view");
        auto host = reinterpret_cast<Direct2DHostWindow*>(GetWindowLongPtrW(view, GWLP_USERDATA));
        ChoicePopup popup;
        for (int i = 0; i < 60; ++i)
            popup.labels.push_back(L"Choice");
        popup.Move(37);
        Require(popup.active == 37 && popup.first == 26, "selected choice remains visible");
        popup.Scroll(100);
        Require(popup.first == 48, "scroll stops at final choice");
        popup.Scroll(-100);
        Require(popup.first == 0, "scroll stops at first choice");
        HWND popupWindow = CreateWindowExW(0,
                                           L"STATIC",
                                           L"",
                                           WS_POPUP,
                                           -10000,
                                           -10000,
                                           popup.width,
                                           popup.rows * popup.rowHeight + 2,
                                           parent,
                                           nullptr,
                                           GetModuleHandleW(nullptr),
                                           nullptr);
        Require(popupWindow != nullptr, "hidden selection input window");
        popup.window = popupWindow;
        SetWindowLongPtrW(popupWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&popup));
        SetWindowLongPtrW(popupWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ChoicePopup::Proc));
        SendMessageW(popupWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        Require(popup.first == 3, "mouse wheel scrolls choices");
        SendMessageW(popupWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), 0);
        Require(popup.first == 3, "partial wheel delta is accumulated");
        SendMessageW(popupWindow, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2), 0);
        Require(popup.first == 0, "accumulated wheel delta scrolls choices");
        SendMessageW(popupWindow, WM_KEYDOWN, VK_END, 0);
        Require(popup.active == 59 && popup.first == 48, "keyboard reaches final choice");
        SendMessageW(popupWindow, WM_KEYDOWN, VK_RETURN, 0);
        Require(popup.done && popup.selected == 59, "keyboard confirms choice");
        popup.done = false;
        SendMessageW(popupWindow, WM_KEYDOWN, VK_ESCAPE, 0);
        Require(popup.done && popup.selected == -1, "escape cancels choice");
        popup.done = false;
        popup.Scroll(12);
        RECT thumb = popup.Thumb();
        SendMessageW(popupWindow, WM_LBUTTONDOWN, 0, MAKELPARAM(thumb.left + 1, thumb.top + 1));
        SendMessageW(popupWindow, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(thumb.left + 1, 1000));
        SendMessageW(popupWindow, WM_LBUTTONUP, 0, 0);
        Require(popup.first == 48 && popup.dragOffset == -1, "scrollbar dragging reaches last choice");
        SendMessageW(popupWindow, WM_LBUTTONDOWN, 0, MAKELPARAM(40, 16));
        Require(popup.done && popup.selected == 48, "mouse selects visible choice after scrolling");
        popup.done = false;
        SendMessageW(popupWindow, WM_LBUTTONDOWN, 0, MAKELPARAM(-1, 16));
        Require(popup.done && popup.selected == -1, "outside click cancels selection");
        Require(!(GetWindowLongPtrW(popupWindow, GWL_STYLE) & (WS_BORDER | WS_VSCROLL)),
                "popup has no native border or arrow scrollbar");
        DestroyWindow(popupWindow);
        popup.window = nullptr;
        host->Render();
        Require(host->m_pRenderTarget != nullptr, "Direct2D render target");
        host->Render();
        Require(host->m_fmtNav->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_LEADING,
                "navigation remains left aligned after repeated paint");
        Require(host->m_fmtDesc->GetFontWeight() >= DWRITE_FONT_WEIGHT_MEDIUM, "descriptions use stronger font weight");
        bool resetInContent = false;
        for (const auto& hit : host->m_hitRegions)
            if (hit.type == HitRegion::ResetBtn)
                resetInContent = hit.rect.left > 240;
        Require(resetInContent, "reset belongs to content header");
        bool scrollbar = false, apply = false;
        for (const auto& hit : host->m_hitRegions)
        {
            scrollbar |= hit.type == HitRegion::Scrollbar && hit.pageIndex == -1;
            apply |= hit.type == HitRegion::ApplyBtn;
        }
        Require(scrollbar && !apply, "visible page scrollbar and no live preview button");
        int transferButtons = 0;
        for (const auto& hit : host->m_hitRegions)
            if (hit.type == HitRegion::ImportBtn || hit.type == HitRegion::ExportBtn)
            {
                ++transferButtons;
                Require(hit.rect.left >= 16 && hit.rect.right <= 224 && hit.rect.bottom <= 850,
                        "import/export fit inside sidebar");
            }
        Require(transferButtons == 2, "import and export buttons visible");
        const auto beforeImport = s;
        auto imported = SettingsIni::Parse(SettingsIni::Serialize(beforeImport));
        imported.brightness = .1875f;
        unsigned previews = 0;
        host->m_preview = [&](const PlaceboSettings&)
        {
            ++previews;
            return E_FAIL;
        };
        Require(FAILED(host->ApplyImported(imported)) && previews == 1 &&
                    SettingsIni::Serialize(s) == SettingsIni::Serialize(beforeImport),
                "failed imported preview preserves draft");
        host->m_preview = [&](const PlaceboSettings&)
        {
            ++previews;
            return S_OK;
        };
        Require(SUCCEEDED(host->ApplyImported(imported)) && s.brightness == imported.brightness &&
                    host->m_applied.brightness == imported.brightness && !host->m_pending,
                "INI import applies once and refreshes accepted settings");
        Require(SUCCEEDED(host->ApplyImported(beforeImport)), "restore before import test");
        host->m_preview = {};
        for (int index : {3, 4, 6})
        {
            host->m_activePage = index;
            host->m_scrollY = 0;
            host->Render();
            Require(host->m_maxScrollY == 0, "short sections fit without unnecessary scrolling");
        }
        host->m_activePage = 7;
        host->Render();
        int shaderRows = 0, enabledSwitches = 0;
        bool shaderScrollbar = false;
        for (const auto& hit : host->m_hitRegions)
        {
            shaderRows += hit.type == HitRegion::ShaderItem;
            enabledSwitches += hit.type == HitRegion::Toggle;
            shaderScrollbar |= hit.type == HitRegion::Scrollbar && hit.pageIndex == -2;
        }
        Require(shaderRows == 7 && enabledSwitches == 1 && shaderScrollbar,
                "compact shader list and only selected shader editor");
        host->m_shaderOffset = ShaderCount - 7;
        host->m_selectedShader = 20;
        host->Render();
        Require(host->m_shaderList.right > host->m_shaderList.left, "last shader reachable");
        if (GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0))
        {
            const auto aId = DisplayProfiles::Hash(L"ui:monitor"), bId = DisplayProfiles::Hash(L"ui:tv");
            auto a = s, b = s;
            wcscpy_s(a.displayProfile, aId.c_str());
            a.brightness = .21f;
            wcscpy_s(b.displayProfile, bId.c_str());
            b.brightness = -.31f;
            Require(SUCCEEDED(SaveSettings(a)) && SUCCEEDED(SaveSettings(b)), "UI profile fixtures");
            host->m_displayProfiles = true;
            host->m_devices = {{aId, L"Monitor", L"ui:monitor", nullptr, true}, {bId, L"TV", L"ui:tv", nullptr, true}};
            host->m_preview = {};
            host->AdoptDisplayProfile(aId);
            Require(s.brightness == .21f && s.displayProfile == aId, "select monitor draft");
            host->AdoptDisplayProfile(bId);
            Require(s.brightness == -.31f && s.displayProfile == bId,
                    "select TV draft without applying monitor values");
            auto portable = SettingsIni::Parse(SettingsIni::Serialize(a));
            std::wstring importedId;
            host->m_preview = [&](const PlaceboSettings& next)
            {
                importedId = next.displayProfile;
                return S_OK;
            };
            Require(SUCCEEDED(host->ApplyImported(portable)) && importedId == bId && s.displayProfile == bId,
                    "import targets selected device and cannot change profile identity");
            host->m_activePage = 0;
            host->Render();
            int displayHits = 0, renameHits = 0;
            for (const auto& hit : host->m_hitRegions)
            {
                if (hit.type == HitRegion::DisplayProfile)
                {
                    ++displayHits;
                    Require(hit.rect.top > 430 && hit.rect.right <= 224, "device selector fits below navigation");
                }
                if (hit.type == HitRegion::RenameDisplay)
                    ++renameHits;
            }
            Require(displayHits == 1 && renameHits == 1, "device selector and rename are visible");
            auto rect = host->m_displayRect;
            rect.left += 10;
            rect.right -= 10;
            rect.top += 6;
            rect.bottom -= 5;
            host->BeginEdit(nullptr, rect, true);
            SetWindowTextW(host->m_edit, L"Wohnzimmer-TV");
            SendMessageW(host->m_edit, WM_KEYDOWN, VK_RETURN, 0);
            HKEY key;
            Require(RegOpenKeyExW(HKEY_CURRENT_USER, DisplayProfiles::Key(bId).c_str(), 0, KEY_READ, &key) ==
                        ERROR_SUCCESS,
                    "renamed key");
            Require(DisplayProfiles::ReadString(key, L"DisplayName") == L"Wohnzimmer-TV",
                    "inline rename persists with Enter");
            RegCloseKey(key);
            host->BeginEdit(nullptr, rect, true);
            SetWindowTextW(host->m_edit, L"Discard this");
            SendMessageW(host->m_edit, WM_KEYDOWN, VK_ESCAPE, 0);
            Require(RegOpenKeyExW(HKEY_CURRENT_USER, DisplayProfiles::Key(bId).c_str(), 0, KEY_READ, &key) ==
                        ERROR_SUCCESS,
                    "rename cancel key");
            Require(DisplayProfiles::ReadString(key, L"DisplayName") == L"Wohnzimmer-TV", "Escape cancels rename");
            RegCloseKey(key);
            host->BeginEdit(nullptr, rect, true);
            SetWindowTextW(host->m_edit, L"TV after click");
            host->OnLButtonDown(260, 100);
            Require(RegOpenKeyExW(HKEY_CURRENT_USER, DisplayProfiles::Key(bId).c_str(), 0, KEY_READ, &key) ==
                        ERROR_SUCCESS,
                    "rename click key");
            Require(DisplayProfiles::ReadString(key, L"DisplayName") == L"TV after click",
                    "click outside saves rename without Enter");
            RegCloseKey(key);
            host->BeginEdit(nullptr, rect, true);
            SetWindowTextW(host->m_edit, L"TV after OK focus");
            SetFocus(parent);
            Require(!host->m_edit, "leaving edit for parent button ends rename");
            Require(RegOpenKeyExW(HKEY_CURRENT_USER, DisplayProfiles::Key(bId).c_str(), 0, KEY_READ, &key) ==
                        ERROR_SUCCESS,
                    "rename focus key");
            Require(DisplayProfiles::ReadString(key, L"DisplayName") == L"TV after OK focus",
                    "focus loss saves rename without Enter");
            RegCloseKey(key);
            host->m_preview = {};
        }
        Direct2DSettingsHost::Destroy(view);
        DestroyWindow(parent);
        printf(
            "Direct2D model: %d controls, %d shader parameters; search, explanations, slider ranges, bindings, reset and ordering passed.\n",
            controls,
            parameters);
        return 0;
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "Direct2D model FAILED: %s\n", e.what());
        return 1;
    }
}
