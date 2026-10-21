#pragma once
#include <windows.h>
// Uses the same public DisplayConfig APIs as MPC's HDR toggle path.
class HdrDisplay {
    HMONITOR monitor=nullptr;bool requested=false,changed=false;
    LUID adapter={};UINT32 target=0;
    bool Set(bool enabled);
public:
    ~HdrDisplay(){Restore();}
    bool Update(HWND window,bool enable);
    void Restore();
};
