#pragma once
#include "Filter.h"
class ShaderPage final:public CBasePropertyPage {
    IPlaceboSettings* config=nullptr;UserShaderSettings draft;int selected=0;bool loading=false;
    HFONT title=nullptr;HBRUSH background=nullptr;
    void List();void Editor();bool Read();void Dirty();void Help(int parameter);int Position()const;
public:
    ShaderPage(IUnknown*,HRESULT*);~ShaderPage()override;
    HRESULT OnConnect(IUnknown*)override;HRESULT OnDisconnect()override;
    HRESULT OnActivate()override;HRESULT OnApplyChanges()override;
    INT_PTR OnReceiveMessage(HWND,UINT,WPARAM,LPARAM)override;
};
