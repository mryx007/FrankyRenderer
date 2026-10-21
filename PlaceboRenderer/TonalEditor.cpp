#include "LiveSettings.h"
#include "TonalEditor.h"
#include <algorithm>
#include <cwctype>
#include <cstdio>
namespace {
struct Editor {TonalSettings draft;std::function<HRESULT(const TonalSettings&)> preview;};
float& Value(TonalSettings& s,int i){return i%3==0?s.exposure[i/3]:i%3==1?s.contrast[i/3]:s.saturation[i/3];}
void Write(HWND d,int i,float v){wchar_t text[40];swprintf_s(text,L"%.6g",double(v));SetDlgItemTextW(d,1600+i,text);}
bool Read(HWND d,int i,float& value){wchar_t text[80];GetDlgItemTextW(d,1600+i,text,80);for(auto& c:text)if(c==L',')c=L'.';wchar_t* end;value=wcstof(text,&end);while(iswspace(*end))++end;return end!=text&&!*end&&std::isfinite(value)&&std::abs(value)<=(i%3==0?4:1);}
void Populate(HWND d,Editor& e){for(int i=0;i<9;i++)Write(d,i,Value(e.draft,i));}
INT_PTR CALLBACK Procedure(HWND d,UINT msg,WPARAM w,LPARAM l){
    auto e=reinterpret_cast<Editor*>(GetWindowLongPtrW(d,DWLP_USER));
    if(msg==WM_INITDIALOG){SetWindowLongPtrW(d,DWLP_USER,l);e=reinterpret_cast<Editor*>(l);Populate(d,*e);if(e->preview)LiveSettings::AttachEditor(d,[d,e](){TonalSettings next;for(int i=0;i<9;i++)if(!Read(d,i,Value(next,i)))return E_INVALIDARG;return e->preview(next);});return TRUE;}
    if(!e)return FALSE;
    if(msg==WM_CLOSE){EndDialog(d,IDCANCEL);return TRUE;}
    if(msg!=WM_COMMAND)return FALSE;const int id=LOWORD(w);
    if(id==IDCANCEL){EndDialog(d,IDCANCEL);return TRUE;}
    if(id==1650){e->draft={};Populate(d,*e);return TRUE;}
    if(id==IDOK){TonalSettings next;for(int i=0;i<9;i++)if(!Read(d,i,Value(next,i))){MessageBoxW(d,L"Use Exposure from -4 to +4 stops, and Contrast / Saturation from -1 to +1. Zero is neutral.",L"Check Tonal Adjustments",MB_OK|MB_ICONINFORMATION);SetFocus(GetDlgItem(d,1600+i));return TRUE;}e->draft=next;EndDialog(d,IDOK);return TRUE;}
    if(id>=1700&&id<1718){const int i=(id-1700)/2;float v;if(Read(d,i,v)){const float limit=i%3==0?4.f:1.f;Write(d,i,std::clamp(v+((id&1)?.005f:-.005f),-limit,limit));}return TRUE;}
    return FALSE;
}
}
bool EditTonalOptions(HINSTANCE module,HWND parent,TonalSettings& settings,std::function<HRESULT(const TonalSettings&)> preview){Editor e{settings,std::move(preview)};if(DialogBoxParamW(module,MAKEINTRESOURCEW(113),parent,Procedure,reinterpret_cast<LPARAM>(&e))!=IDOK)return false;settings=e.draft;return true;}
