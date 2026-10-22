#pragma once
#include "Filter.h"
#include <map>
#include <vector>

class ScalingPage final : public CBasePropertyPage
{
    IPlaceboSettings* config = nullptr;
    ScalingSettings draft;
    int target = 0;
    bool loading = false;
    int skipAntiAlias = -1, preserveMix = -1;
    BlackBarSettings blackBars;
    HWND tip = nullptr;
    HFONT title = nullptr;
    HBRUSH background = nullptr;
    std::map<int, std::vector<std::string>> choices;
    std::map<int, std::wstring> help;
    void Fill(int id, int role, int kind, const char* selected);
    const char* Selected(int id);
    void Editor();
    bool ReadEditor();
    void Populate();
    void Tool(int id, const std::wstring& text);
    void Dirty();
    void Step(int id, bool up);

  public:
    ScalingPage(IUnknown*, HRESULT*);
    ~ScalingPage() override;
    HRESULT OnConnect(IUnknown*) override;
    HRESULT OnDisconnect() override;
    HRESULT OnActivate() override;
    HRESULT OnDeactivate() override;
    HRESULT OnApplyChanges() override;
    INT_PTR OnReceiveMessage(HWND, UINT, WPARAM, LPARAM) override;
};
