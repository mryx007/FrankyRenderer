#pragma once
#include <windows.h>
#include "RendererExtras.h"
#include <functional>
bool EditOutputOptions(HINSTANCE,HWND,RendererExtras&,std::function<HRESULT(const RendererExtras&)> preview={});
