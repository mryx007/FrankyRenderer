#pragma once
#include "Filter.h"

class SettingsPage : public CBasePropertyPage
{
    IPlaceboSettings* config = nullptr;
    PlaceboSettings settings;
    bool loading = false, processing = false, color = false, hdrDetails = false, effects = false, debanding = false,
         adjustments = false;
    HFONT titleFont = nullptr;
    HBRUSH background = nullptr;
    HWND helpTip = nullptr;
    bool debandDefaults[4] = {};
    void Populate();
    void UpdateControls();
    void ShowDiagnostics(bool details);
    bool Number(int, float&);
    void AddNumberButtons();
    void UpdateControlStates();
    bool HandleNumberButton(int id);
    void AddHelp();
    void PopulateDebanding();
    void PopulateDeinterlacing();
    bool ReadDebanding(PlaceboSettings&);
    bool ReadDeinterlacing(PlaceboSettings&);
    void BrowseColor(bool icc);
    RendererExtras outputDraft;
    int hdrSection = -1;

    struct HdrPosition
    {
        HWND window;
        int section, x, y, w, h;
    };

    std::vector<HdrPosition> hdrPositions;
    void InitHdrSections();
    void LayoutHdrSections();

  public:
    SettingsPage(IUnknown* outer, HRESULT* hr, int pageKind = 0);
    ~SettingsPage() override;
    HRESULT OnConnect(IUnknown*) override;
    HRESULT OnDisconnect() override;
    HRESULT OnActivate() override;
    HRESULT OnDeactivate() override;
    HRESULT OnApplyChanges() override;
    INT_PTR OnReceiveMessage(HWND, UINT, WPARAM, LPARAM) override;
};

class ProcessingPage final : public SettingsPage
{
  public:
    ProcessingPage(IUnknown* outer, HRESULT* hr) : SettingsPage(outer, hr, true) {}
};

class ColorPage final : public SettingsPage
{
  public:
    ColorPage(IUnknown* outer, HRESULT* hr) : SettingsPage(outer, hr, 2) {}
};

class HdrPage final : public SettingsPage
{
  public:
    HdrPage(IUnknown* outer, HRESULT* hr) : SettingsPage(outer, hr, 3) {}
};

class EffectsPage final : public SettingsPage
{
  public:
    EffectsPage(IUnknown* outer, HRESULT* hr) : SettingsPage(outer, hr, 4) {}
};

class DebandingPage final : public SettingsPage
{
  public:
    DebandingPage(IUnknown* outer, HRESULT* hr) : SettingsPage(outer, hr, 6) {}
};

class AdjustmentPage final : public SettingsPage
{
  public:
    AdjustmentPage(IUnknown* outer, HRESULT* hr) : SettingsPage(outer, hr, 7) {}
};
