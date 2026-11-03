#include "UI/Direct2DSettingsPage.h"
#include "Settings/LiveSettings.h"

// Whether the renderer behind the page is part of a filter graph. A player
// opening the settings without a video creates a renderer outside any graph.
static bool InGraph(IPlaceboSettings* config)
{
    IBaseFilter* filter = nullptr;
    FILTER_INFO info = {};
    const bool joined = config && SUCCEEDED(config->QueryInterface(IID_PPV_ARGS(&filter))) &&
                        SUCCEEDED(filter->QueryFilterInfo(&info)) && info.pGraph;
    if (info.pGraph)
        info.pGraph->Release();
    if (filter)
        filter->Release();
    return joined;
}

Direct2DSettingsPage::Direct2DSettingsPage(IUnknown* outer, HRESULT* hr)
    : CBasePropertyPage(NAME("FrankyRenderer Settings"), outer, 116, 216)
{
    *hr = S_OK;
}

HRESULT Direct2DSettingsPage::OnConnect(IUnknown* object)
{
    return object->QueryInterface(IID_PPV_ARGS(&config));
}

HRESULT Direct2DSettingsPage::OnDisconnect()
{
    if (config)
    {
        config->Release();
        config = nullptr;
    }
    return S_OK;
}

HRESULT Direct2DSettingsPage::OnActivate()
{
    if (!config)
        return E_UNEXPECTED;
    auto hr = config->GetSettings(&draft);
    if (FAILED(hr))
        return hr;
    hr =
        PrepareDisplaySettings(draft, DisplayProfiles::Connected(), MonitorFromWindow(m_Dlg, MONITOR_DEFAULTTONEAREST));
    if (FAILED(hr))
        return hr;
    LiveSettings::Attach(m_Dlg, config, [this] { return OnApplyChanges(); });
    RECT bounds;
    GetClientRect(m_Dlg, &bounds);
    preferredSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    view = Direct2DSettingsHost::Create(
        m_Dlg,
        bounds,
        &draft,
        false,
        [this](const PlaceboSettings& next)
        {
            LiveSettings::RememberDisplay(m_Dlg, next);
            const auto result = config->SetSettings(&next);
            if (SUCCEEDED(result))
            {
                m_bDirty = FALSE;
                if (m_pPageSite)
                    m_pPageSite->OnStatusChange(PROPPAGESTATUS_CLEAN);
            }
            return result;
        },
        true);
    // Let OLE complete its own page/tab measurement before changing its frame.
    if (view)
    {
        frame.BeginOpening(m_Dlg);
        PostMessageW(m_Dlg, WM_APP + 304, 0, 0);
        if (InGraph(config))
            SetTimer(m_Dlg, GraphTimer, 500, nullptr);
    }
    return view ? S_OK : E_FAIL;
}

HRESULT Direct2DSettingsPage::OnDeactivate()
{
    KillTimer(m_Dlg, GraphTimer);
    frame.Detach();
    Direct2DSettingsHost::Destroy(view);
    view = nullptr;
    return S_OK;
}

HRESULT Direct2DSettingsPage::OnApplyChanges()
{
    if (!config)
        return E_UNEXPECTED;
    auto hr = ValidateSettings(draft);
    if (FAILED(hr))
        return hr;
    LiveSettings::RememberDisplay(m_Dlg, draft);
    hr = config->SetSettings(&draft);
    if (SUCCEEDED(hr))
        m_bDirty = FALSE;
    return hr;
}

INT_PTR Direct2DSettingsPage::OnReceiveMessage(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_APP + 302)
    {
        m_bDirty = TRUE;
        if (m_pPageSite)
            m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
        return TRUE;
    }
    if (msg == WM_SIZE && view)
    {
        RECT bounds;
        GetClientRect(h, &bounds);
        Direct2DSettingsHost::Resize(view, bounds);
        return TRUE;
    }
    if (msg == WM_SETFOCUS && view)
    {
        SetFocus(view);
        return TRUE;
    }
    if (msg == WM_APP + 304)
    {
        frame.Attach(m_Dlg, preferredSize);
        frame.FinishOpening();
        return TRUE;
    }
    // The player stays usable while the menu is open. Once it opens another
    // file or closes, this renderer leaves its graph; close its menu too.
    if (msg == WM_TIMER && w == GraphTimer)
    {
        if (!InGraph(config))
            frame.Close();
        return TRUE;
    }
    if (msg == WM_WINDOWPOSCHANGED || msg == WM_SHOWWINDOW)
        frame.Refresh();
    return CBasePropertyPage::OnReceiveMessage(h, msg, w, l);
}
