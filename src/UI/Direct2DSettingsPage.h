#pragma once
#include "Renderer/Filter.h"
#include "UI/Direct2DSettingsHost.h"
#include "UI/Direct2DFrame.h"

class Direct2DSettingsPage final : public CBasePropertyPage
{
    IPlaceboSettings* config = nullptr;
    PlaceboSettings draft;
    HWND view = nullptr;
    Direct2DFrame frame;
    SIZE preferredSize = {};

  public:
    Direct2DSettingsPage(IUnknown* outer, HRESULT* hr);
    HRESULT OnConnect(IUnknown*) override;
    HRESULT OnDisconnect() override;
    HRESULT OnActivate() override;
    HRESULT OnDeactivate() override;
    HRESULT OnApplyChanges() override;
    INT_PTR OnReceiveMessage(HWND, UINT, WPARAM, LPARAM) override;
};
