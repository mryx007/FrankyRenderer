#pragma once
#include "Filter.h"

class ColorMappingPage final : public CBasePropertyPage
{
    IPlaceboSettings* config = nullptr;
    PlaceboSettings original;
    TonalSettings tonalDraft;
    ColorMappingSettings draft;
    bool loading = false;
    int group = 0;
    HFONT title = nullptr;
    HBRUSH background = nullptr;
    HWND helpTip = nullptr;
    std::vector<int> rows;
    void Populate();
    void Editor();
    bool ReadEditor();
    void Dirty();
    void Step(int row, bool up);
    void PopulateHdr();
    void UpdateHdr();
    void AddHdrHelp();
    bool ReadHdr(PlaceboSettings&);

  public:
    ColorMappingPage(IUnknown*, HRESULT*);
    ~ColorMappingPage() override;
    HRESULT OnConnect(IUnknown*) override;
    HRESULT OnDisconnect() override;
    HRESULT OnActivate() override;
    HRESULT OnDeactivate() override;
    HRESULT OnApplyChanges() override;
    INT_PTR OnReceiveMessage(HWND, UINT, WPARAM, LPARAM) override;
};
