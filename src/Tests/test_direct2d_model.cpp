#include "UI/Direct2DSettingsHost.cpp"
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
        bool analysis = false;
        for (auto& card : pages[0].cards)
            for (auto& c : card.controls)
                if (c.name == L"Image Analysis")
                {
                    Require(!s.imageAnalysis, "analysis starts disabled");
                    c.set(1);
                    Require(s.imageAnalysis, "one toggle enables complete image analysis");
                    c.reset();
                    Require(!s.imageAnalysis, "analysis resets disabled");
                    analysis = true;
                }
        Require(analysis, "single analysis switch exists");
        Require(MatchesFilter(L"HDR Highlights Exposure", L"  exposure HDR "),
                "search ignores case and outer spaces; all words match");
        Require(!MatchesFilter(L"HDR Highlights Exposure", L"exposure Gamma"), "search excludes missing terms");
        Require(MatchesFilter(L"Anything", L" \t"), "empty search");
        auto& hdrControls = pages[5].cards[0].controls;
        bool hasProtection = false;
        for (auto& control : hdrControls)
        {
            if (control.name == L"HDR Mode")
            {
                Require(control.choices.size() == 3, "passthrough, display peak and SDR HDR modes");
                for (const auto& choice : control.choices)
                    Require(choice.label.find(L"Protection") == std::wstring::npos, "protection is not an HDR mode");
            }
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
                        Require(SliderFraction(c, c.minVal) == 0 && SliderFraction(c, c.maxVal) == 1,
                                "slider endpoints match the actual range");
                        Require(SliderValue(c, 0) == c.minVal && SliderValue(c, 1) == c.maxVal,
                                "slider reaches both bounds");
                        const float middle = float((double(c.minVal) + c.maxVal) / 2);
                        Require(c.logarithmic || (SliderValue(c, .5f) == middle &&
                                                  std::abs(SliderFraction(c, middle) - .5f) < 1e-5f),
                                "slider midpoint is half the value range, independent of the default");
                        for (float f : {0.f, .1f, .25f, .5f, .75f, .9f, 1.f})
                        {
                            float v = SliderValue(c, f);
                            Require(std::isfinite(v) && v >= c.minVal && v <= c.maxVal, "slider stays in range");
                            Require(std::abs(SliderFraction(c, v) - f) < 1e-5f,
                                    "slider display and dragging use the same mapping");
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
        auto& iterations = find(L"Processing", L"Debanding", L"Iterations");
        for (const auto* name : {L"Threshold", L"Radius", L"Grain"})
        {
            auto& control = find(L"Processing", L"Debanding", name);
            Require(control.logarithmic && control.minVal == 0 && control.maxVal == 1000,
                    "deband sliders retain full range with fine control near zero");
            Require(SliderFraction(control, control.Default()) > .1f,
                    "normal debanding values occupy usable track space");
            float previous = -1;
            for (float value : {0.f, .001f, .01f, .1f, 1.f, 3.f, 4.f, 16.f, 100.f, 1000.f})
            {
                const float position = SliderFraction(control, value);
                Require(position > previous &&
                            std::abs(SliderValue(control, position) - value) <= std::max(.0001f, value * 1e-5f),
                        "log slider is continuous through zero, ordered and invertible");
                previous = position;
            }
        }
        Require(iterations.kind == ControlKind::SliderInt && iterations.Default() == 1 &&
                    SliderFraction(iterations, 1) == 1.f / 16.f,
                "iteration default sits near the left end rather than halfway along the track");
        for (int value = 0; value <= 16; ++value)
            Require(SliderFraction(iterations, float(value)) == value / 16.f &&
                        SliderValue(iterations, value / 16.f) == float(value),
                    "all iteration values, including zero, have uniform slider spacing");
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
        auto& rtx = find(L"Scaling", L"RTX Video Super Resolution", L"RTX Video Super Resolution");
        Require(rtx.headerToggle && !s.rtxSuperResolution, "RTX heading toggle defaults off");
        rtx.set(1);
        Require(!scaler.Enabled(), "RTX owns image upscaling");
        rtx.reset();
        Require(!s.rtxSuperResolution && scaler.Enabled(), "RTX reset restores normal scaler");
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
        for (int role = 0; role < 5; ++role)
        {
            const auto title = std::wstring(L"Custom: ") + ScalingLabels[role];
            auto& toggle = find(L"Scaling", title.c_str(), L"Enable Custom Settings");
            Require(toggle.headerToggle, "custom switch belongs to card heading");
            auto& starting = find(L"Scaling", title.c_str(), L"Starting Filter");
            auto& preset = find(L"Scaling", L"Filters", ScalingLabels[role]);
            auto& filter = s.scaling.filters[role];
            filter = {};
            strcpy_s(filter.filter, role == 4 ? "hermite" : "lanczos");
            Require(toggle.Enabled() && !starting.Enabled(), "custom switch usable while editor disabled");
            toggle.set(1);
            Require(toggle.Value() == 1 && starting.Enabled() && !strcmp(filter.filter, "custom"),
                    "custom switch enables editor and custom filter");
            Require(!preset.Enabled(), "custom switch disables corresponding preset selector");
            Require(!strcmp(filter.preset, role == 4 ? "hermite" : "lanczos"),
                    "custom switch inherits selected preset");
            filter.overrides = 4;
            filter.values[2] = 1.1f;
            Require(SUCCEEDED(ValidateSettings(s)), "custom switch creates valid settings");
            toggle.set(0);
            Require(!starting.Enabled() && !strcmp(filter.filter, filter.preset),
                    "custom switch restores starting preset");
            Require(preset.Enabled(), "disabling custom re-enables preset selector");
            toggle.set(1);
            Require(filter.overrides == 4 && filter.values[2] == 1.1f, "toggle preserves custom adjustments");
            toggle.reset();
            Require(toggle.Value() == 0 && !starting.Enabled(), "custom switch reset disables editor");
            filter = {};
        }
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
        {
            const auto savedSettings = s;
            auto savedPages = host->m_pages;
            s.debandThreshold = 1.5f;
            D2DControl number;
            number.name = L"Threshold";
            number.desc = L"Number entry test";
            number.kind = ControlKind::SliderFloat;
            number.minVal = 0;
            number.maxVal = 10;
            number.stepVal = .001f;
            number.get = [&] { return s.debandThreshold; };
            number.set = [&](float value) { s.debandThreshold = value; };
            number.defaultValue = [] { return 1.5f; };
            number.reset = [&] { s.debandThreshold = 1.5f; };
            host->m_pages = {{L"Numbers", L"", L"Numbers", L"", {{L"Numbers", L"", {number}}}}};
            host->m_activePage = host->m_activeCard = 0;
            host->m_scrollY = 0;
            auto region = [&](HitRegion::Type type)
            {
                host->Render();
                for (const auto& hit : host->m_hitRegions)
                    if (hit.type == type)
                        return hit;
                throw std::runtime_error("missing number interaction");
            };
            auto click = [&](HitRegion::Type type)
            {
                const auto hit = region(type);
                host->OnLButtonDown((hit.rect.left + hit.rect.right) / 2, (hit.rect.top + hit.rect.bottom) / 2);
            };
            click(HitRegion::Increment);
            Require(std::abs(s.debandThreshold - 1.51f) < 1e-5f, "plus uses a modestly coarser step");
            host->m_stepStarted = GetTickCount64() - 2000;
            SendMessageW(view, WM_TIMER, 18, 0);
            Require(std::abs(s.debandThreshold - 1.61f) < 1e-5f, "holding plus repeats and accelerates");
            host->OnLButtonUp(0, 0);
            Require(!host->m_stepControl && GetCapture() != view, "release stops number repetition");
            const auto stopped = s.debandThreshold;
            SendMessageW(view, WM_TIMER, 18, 0);
            Require(s.debandThreshold == stopped, "late timer cannot continue stepping after release");
            click(HitRegion::Decrement);
            host->OnLButtonUp(0, 0);
            Require(std::abs(s.debandThreshold - 1.60f) < 1e-5f, "minus lowers the value");
            click(HitRegion::Number);
            Require(host->m_edit != nullptr, "number field starts direct editing");
            SetWindowTextW(host->m_edit, L"2,003");
            host->EndEdit();
            Require(std::abs(s.debandThreshold - 2.003f) < 1e-5f, "direct entry retains fine decimal precision");
            Require(NumberText(number, s.debandThreshold) == L"2.003", "drawn number does not hide fine precision");
            Require(NumberText(number, 0) == L"0" && NumberText(number, 3) == L"3" && NumberText(number, .5f) == L"0.5",
                    "values inside number fields do not gain trailing decimal zeros");
            Require(NumberText(number, 0, true) == L"0" && NumberText(number, 2, true) == L"2.0" &&
                        NumberText(number, 100, true) == L"100" && NumberText(number, 1000, true) == L"1000",
                    "small nonzero range endpoints have one decimal, large whole endpoints stay compact");
            auto integer = number;
            integer.kind = ControlKind::SliderInt;
            Require(NumberText(integer, 1) == L"1", "integer fields do not gain decimal places");
            Require(NumberText(integer, 0, true) == L"0", "integer range endpoints remain whole numbers");
            click(HitRegion::Number);
            SetWindowTextW(host->m_edit, L"11");
            host->EndEdit();
            Require(std::abs(s.debandThreshold - 2.003f) < 1e-5f, "out-of-range direct entry is rejected");
            click(HitRegion::ResetValue);
            Require(s.debandThreshold == 1.5f, "individual reset remains available");
            s.debandThreshold = 10;
            host->Render();
            for (const auto& hit : host->m_hitRegions)
                Require(hit.type != HitRegion::Increment && hit.type != HitRegion::Slider,
                        "upper limit disables plus and numbers have no slider track");
            s.debandThreshold = 0;
            host->Render();
            for (const auto& hit : host->m_hitRegions)
                Require(hit.type != HitRegion::Decrement, "lower limit disables minus");
            click(HitRegion::Number);
            wchar_t zeroText[64];
            GetWindowTextW(host->m_edit, zeroText, 64);
            Require(!wcscmp(zeroText, L"0"), "zero remains plain zero while editing the field");
            host->EndEdit(false);
            click(HitRegion::Increment);
            host->StopNumberRepeat();
            Require(!host->m_stepControl && GetCapture() != view,
                    "cancelling repeat releases mouse capture even without a button-up event");
            host->m_pages = std::move(savedPages);
            s = host->m_accepted = savedSettings;
            host->m_pending = false;
            host->m_error.clear();
            host->Render();
            puts("PASS number fields: plus/minus, accelerated hold, release, fine direct entry, limits and reset");
        }
        {
            int processing = -1;
            for (int i = 0; i < int(host->m_pages.size()); ++i)
                if (host->m_pages[i].name == L"Processing")
                    processing = i;
            Require(processing >= 0, "processing navigation exists");
            const int savedPage = host->m_activePage, savedCard = host->m_activeCard;
            host->m_navigationOpen = false;
            host->m_navigationProgress = 0;
            host->m_activePage = processing;
            host->m_activeCard = 1;
            host->m_scrollY = 24;
            host->m_contentStarted = 123;
            host->m_contentProgress = 1;
            for (int i = 0; i < 5; ++i)
                host->OnLButtonDown(30, 62.f + processing * 36.f + 16.f);
            Require(host->m_activeCard == 1 && host->m_scrollY == 24 && host->m_contentStarted == 123 &&
                        host->m_contentProgress == 1,
                    "repeated active rail clicks preserve tab, scroll and animation state");
            host->m_navigationOpen = true;
            host->m_navigationProgress = 1;
            host->Render();
            Require(host->m_pRenderTarget != nullptr, "rounded sidebar renders without a device error");
            HitRegion activeItem;
            for (const auto& hit : host->m_hitRegions)
                if (hit.type == HitRegion::NavItem && hit.pageIndex == processing)
                    activeItem = hit;
            Require(activeItem.type == HitRegion::NavItem, "active expanded navigation item exists");
            host->m_scrollY = 24;
            host->OnLButtonDown((activeItem.rect.left + activeItem.rect.right) / 2,
                                (activeItem.rect.top + activeItem.rect.bottom) / 2);
            Require(!host->m_navigationOpen && host->m_activeCard == 1 && host->m_scrollY == 24 &&
                        host->m_contentStarted == 123 && host->m_contentProgress == 1,
                    "active drawer item closes navigation without restarting its page");
            host->m_navigationProgress = 0;
            const int nextPage = (processing + 1) % int(host->m_pages.size());
            host->OnLButtonDown(30, 62.f + nextPage * 36.f + 16.f);
            Require(host->m_activePage == nextPage && host->m_activeCard == 0 && host->m_scrollY == 0 &&
                        host->m_contentProgress == 0,
                    "different page still resets state and starts its transition");
            host->m_searchQuery = L"Threshold";
            host->OnLButtonDown(30, 62.f + nextPage * 36.f + 16.f);
            Require(host->m_searchQuery.empty(), "selecting a page still leaves search results");
            host->m_activePage = savedPage;
            host->m_activeCard = savedCard;
            host->m_contentProgress = 1;
            host->m_previousCard = -1;
            host->Render();
            puts("PASS repeated active navigation clicks do not restart the page or reset its state");
        }
        {
            auto savedPages = host->m_pages;
            D2DControl control;
            control.name = L"Test";
            control.kind = ControlKind::Toggle;
            host->m_pages = {{L"Layout", L"", L"Layout", L"", {{L"Test", L"", {control}}}}};
            host->m_activePage = 0;
            host->m_scrollY = 0;
            host->m_pRenderTarget->SetDpi(96.f, 96.f);
            // The reset action has its own header row above cards without tabs.
            Require(SUCCEEDED(host->m_pRenderTarget->Resize(D2D1::SizeU(960, 210))), "fitting layout target");
            host->Render();
            Require(host->m_maxScrollY == 0, "last card fits without scrolling into trailing padding");
            Require(SUCCEEDED(host->m_pRenderTarget->Resize(D2D1::SizeU(960, 200))), "overflow layout target");
            host->Render();
            Require(host->m_maxScrollY > 0, "actual clipped content remains scrollable");
            host->m_scrollY = host->m_maxScrollY;
            Require(SUCCEEDED(host->m_pRenderTarget->Resize(D2D1::SizeU(960, 224))), "restore fitting target");
            host->Render();
            Require(host->m_maxScrollY == 0 && host->m_scrollY == 0, "resize removes stale scroll offset");
            for (const auto& hit : host->m_hitRegions)
                Require(hit.type != HitRegion::Scrollbar, "fitting content has no scrollbar hit region");
            host->m_pages = std::move(savedPages);
        }
        for (UINT dpi : {96u, 120u, 144u})
        {
            const auto preferred = SIZE{MulDiv(1040, dpi, 96), MulDiv(720, dpi, 96)};
            const auto content = SettingsLayout::ContentSize(preferred, dpi, dpi);
            const auto frame =
                SettingsLayout::FitFrame({content.cx, content.cy + MulDiv(84, dpi, 96)}, {1920, 1040}, dpi);
            Require(frame.cx <= 1728 && frame.cy <= 936, "Full HD frame leaves desktop margins at every DPI");
            const auto viewSize = D2D1::SizeU(frame.cx, frame.cy - MulDiv(84, dpi, 96));
            Require(SUCCEEDED(host->m_pRenderTarget->Resize(viewSize)), "resize Full HD render target");
            host->m_pRenderTarget->SetDpi(float(dpi), float(dpi));
            for (int page = 0; page < int(host->m_pages.size()); ++page)
            {
                host->m_activePage = page;
                host->m_scrollY = 0;
                // Settings beside the closed icon rail.
                host->m_navigationOpen = false;
                host->m_navigationProgress = 0;
                host->Render();
                const auto size = host->m_pRenderTarget->GetSize();
                // Controls scrolled out of view have empty (clipped) hit areas.
                for (const auto& hit : host->m_hitRegions)
                    if ((hit.type == HitRegion::DropdownHeader || hit.type == HitRegion::Slider) &&
                        hit.rect.right > hit.rect.left)
                        Require(hit.rect.left >= 60 && hit.rect.right <= size.width - 16,
                                "Full HD controls remain inside content area");
                // Navigation, display and transfer controls in the open drawer.
                host->m_navigationOpen = true;
                host->m_navigationProgress = 1;
                host->Render();
                float navBottom = 0, actionsTop = size.height;
                for (const auto& hit : host->m_hitRegions)
                {
                    if (hit.type == HitRegion::NavItem)
                        navBottom = std::max(navBottom, hit.rect.bottom);
                    if (hit.type == HitRegion::ImportBtn || hit.type == HitRegion::ExportBtn)
                    {
                        Require(hit.rect.left >= 16 && hit.rect.right <= SettingsLayout::SidebarWidth - 16 &&
                                    hit.rect.bottom <= size.height,
                                "Full HD action buttons remain inside sidebar");
                        actionsTop = std::min(actionsTop, hit.rect.top - 72.f);
                    }
                }
                Require(navBottom < actionsTop, "Full HD navigation does not overlap device or transfer controls");
                host->m_navigationOpen = false;
                host->m_navigationProgress = 0;
            }
        }
        Require(SUCCEEDED(host->m_pRenderTarget->Resize(D2D1::SizeU(1100, 850))), "restore model render target");
        host->m_pRenderTarget->SetDpi(96.f, 96.f);
        host->m_activePage = 0;
        host->Render();
        Require(host->m_fmtNav->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_LEADING,
                "navigation remains left aligned after repeated paint");
        Require(host->m_fmtDesc->GetFontWeight() >= DWRITE_FONT_WEIGHT_MEDIUM, "descriptions use stronger font weight");
        bool resetInContent = false;
        for (const auto& hit : host->m_hitRegions)
            if (hit.type == HitRegion::ResetBtn)
                resetInContent = hit.rect.left > 240;
        Require(resetInContent, "reset belongs to content header");
        host->m_searchQuery = L"Custom: Image Upscaler";
        host->m_scrollY = 0;
        host->Render();
        int headerSwitches = 0;
        for (const auto& hit : host->m_hitRegions)
        {
            if (hit.type == HitRegion::Toggle && hit.pControl && hit.pControl->headerToggle)
            {
                ++headerSwitches;
            }
        }
        Require(headerSwitches == 1, "custom card renders one heading switch without duplicate row");
        host->m_searchQuery.clear();
        // Tabs keep pages short; a low view still has to scroll.
        Require(SUCCEEDED(host->m_pRenderTarget->Resize(D2D1::SizeU(1100, 360))), "low render target");
        host->Render();
        bool scrollbar = false, apply = false;
        for (const auto& hit : host->m_hitRegions)
        {
            scrollbar |= hit.type == HitRegion::Scrollbar && hit.pageIndex == -1;
            apply |= hit.type == HitRegion::ApplyBtn;
        }
        Require(scrollbar && !apply, "visible page scrollbar and no live preview button");
        Require(SUCCEEDED(host->m_pRenderTarget->Resize(D2D1::SizeU(1100, 850))), "restore model render target");
        // Import and export live in the navigation drawer.
        host->m_navigationOpen = true;
        host->m_navigationProgress = 1;
        host->Render();
        int transferButtons = 0;
        for (const auto& hit : host->m_hitRegions)
            if (hit.type == HitRegion::ImportBtn || hit.type == HitRegion::ExportBtn)
            {
                ++transferButtons;
                Require(hit.rect.left >= 16 && hit.rect.right <= SettingsLayout::SidebarWidth - 16 &&
                            hit.rect.bottom <= 850,
                        "import/export fit inside sidebar");
            }
        Require(transferButtons == 2, "import and export buttons visible");
        host->m_navigationOpen = false;
        host->m_navigationProgress = 0;
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
        // General, Processing and Color: first tabs fit without scrolling.
        for (int index : {0, 1, 3})
        {
            host->m_activePage = index;
            host->m_scrollY = 0;
            host->Render();
            Require(host->m_maxScrollY == 0, "short sections fit without unnecessary scrolling");
        }
        host->m_activePage = 5; // Shaders
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
            host->m_navigationOpen = true;
            host->m_navigationProgress = 1;
            host->Render();
            int displayHits = 0, renameHits = 0;
            for (const auto& hit : host->m_hitRegions)
            {
                if (hit.type == HitRegion::DisplayProfile)
                {
                    ++displayHits;
                    Require(hit.rect.top > 430 && hit.rect.right <= SettingsLayout::SidebarWidth - 16,
                            "device selector fits below navigation");
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
