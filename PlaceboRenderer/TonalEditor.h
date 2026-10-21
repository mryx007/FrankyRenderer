#pragma once
#include <windows.h>
#include "TonalSettings.h"
#include <functional>
bool EditTonalOptions(HINSTANCE,HWND,TonalSettings&,std::function<HRESULT(const TonalSettings&)> preview={});
