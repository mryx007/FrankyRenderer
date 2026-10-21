#include "LiveSettings.h"
#include "OutputOptions.h"
#include <commctrl.h>
#include <array>
#include <cwctype>
#include <algorithm>
namespace {
struct Editor {
    std::function<HRESULT(const RendererExtras&)> preview;
    RendererExtras draft;bool loading=false;HWND tip=nullptr;
    HBRUSH brush=CreateSolidBrush(RGB(248,250,252));std::array<std::wstring,14> help;
    ~Editor(){if(tip&&IsWindow(tip))DestroyWindow(tip);if(brush)DeleteObject(brush);}
};
std::wstring Number(double n){wchar_t text[64];swprintf_s(text,L"%.6g",n);return text;}
std::wstring Text(HWND d,int id){wchar_t text[64]={};GetDlgItemTextW(d,id,text,64);return text;}
float DefaultValue(int i){auto o=pl_options_alloc(nullptr);pl_options_reset(o,&pl_render_default_params);const auto data=pl_options_get(o,ExtraOptions[i].key);const float value=data?static_cast<float>(atof(data->text)):0;pl_options_free(&o);return value;}
bool Numeric(HWND d,int i,float& value){auto text=Text(d,1100+i);if(!_wcsicmp(text.c_str(),L"Default")){value=-1;return true;}auto editor=reinterpret_cast<Editor*>(GetWindowLongPtrW(d,DWLP_USER));if(editor&&editor->draft.values[i]<0&&text==Number(DefaultValue(i))){value=-1;return true;}for(auto& c:text)if(c==L',')c=L'.';wchar_t* end;value=wcstof(text.c_str(),&end);while(iswspace(*end))++end;const auto& o=ExtraOptions[i];return end!=text.c_str()&&!*end&&std::isfinite(value)&&value>=o.minimum&&value<=o.maximum&&(!o.integer||std::floor(value)==value);}
void Populate(HWND d,Editor& e){e.loading=true;for(int i=0;i<14;i++){
    if(i==5||i==6){SendDlgItemMessageW(d,1100+i,CB_RESETCONTENT,0,0);for(auto label:{L"Default (Off)",L"Off",L"On"})SendDlgItemMessageW(d,1100+i,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendDlgItemMessageW(d,1100+i,CB_SETCURSEL,static_cast<int>(e.draft.values[i])+1,0);}
    else SetDlgItemTextW(d,1100+i,Number(e.draft.values[i]<0?DefaultValue(i):e.draft.values[i]).c_str());
}e.loading=false;}
void Init(HWND d,Editor& e){
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    e.tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,d,nullptr,GetModuleHandleW(nullptr),nullptr);if(e.tip)SendMessageW(e.tip,TTM_SETMAXTIPWIDTH,0,420);
    for(int i=0;i<14;i++){
        SetDlgItemTextW(d,1200+i,ExtraOptions[i].label);
        const std::wstring value=(i==5||i==6)?L"Off":Number(DefaultValue(i));
        std::wstring summary=L"Default = "+value;if(i!=5&&i!=6)summary+=L"; range "+Number(ExtraOptions[i].minimum)+L" to "+Number(ExtraOptions[i].maximum);SetDlgItemTextW(d,1300+i,summary.c_str());
        e.help[i]=ExtraOptions[i].help;e.help[i]+=L"\n"+summary;
        for(int id:{1100+i,2200+i*2,2201+i*2})if(HWND field=GetDlgItem(d,id)){TOOLINFOW t={};t.cbSize=TTTOOLINFOW_V2_SIZE;t.uFlags=TTF_IDISHWND|TTF_SUBCLASS;t.hwnd=d;t.uId=reinterpret_cast<UINT_PTR>(field);t.lpszText=e.help[i].data();SendMessageW(e.tip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&t));}
    }
    Populate(d,e);
}
INT_PTR CALLBACK Procedure(HWND d,UINT msg,WPARAM w,LPARAM l){
    auto e=reinterpret_cast<Editor*>(GetWindowLongPtrW(d,DWLP_USER));
    if(msg==WM_INITDIALOG){e=reinterpret_cast<Editor*>(l);SetWindowLongPtrW(d,DWLP_USER,l);Init(d,*e);if(e->preview)LiveSettings::AttachEditor(d,[d,e](){auto next=e->draft;for(int i=0;i<14;i++){if(i==5||i==6){auto at=SendDlgItemMessageW(d,1100+i,CB_GETCURSEL,0,0);if(at<0||at>2)return E_INVALIDARG;next.values[i]=static_cast<float>(at-1);}else if(!Numeric(d,i,next.values[i]))return E_INVALIDARG;}return e->preview(next);});return TRUE;}if(!e)return FALSE;
    if(msg==WM_CTLCOLORDLG)return reinterpret_cast<INT_PTR>(e->brush);
    if(msg==WM_CTLCOLORSTATIC){SetBkColor(reinterpret_cast<HDC>(w),RGB(248,250,252));SetTextColor(reinterpret_cast<HDC>(w),RGB(40,49,60));return reinterpret_cast<INT_PTR>(e->brush);}
    if(msg==WM_CLOSE){EndDialog(d,IDCANCEL);return TRUE;}
    if(msg!=WM_COMMAND||e->loading)return FALSE;const int id=LOWORD(w),event=HIWORD(w);
    if(id==IDCANCEL){EndDialog(d,IDCANCEL);return TRUE;}
    if(id==1140){for(int i=0;i<14;i++)e->draft.values[i]=-1;Populate(d,*e);return TRUE;}
    if(id==IDOK){auto next=e->draft;for(int i=0;i<14;i++){
        if(i==5||i==6){const auto at=SendDlgItemMessageW(d,1100+i,CB_GETCURSEL,0,0);if(at<0||at>2)return TRUE;next.values[i]=static_cast<float>(at-1);}
        else if(!Numeric(d,i,next.values[i])){MessageBoxW(d,L"Use Default or a number within the displayed range.",ExtraOptions[i].label,MB_OK|MB_ICONINFORMATION);SetFocus(GetDlgItem(d,1100+i));return TRUE;}
    }e->draft=next;EndDialog(d,IDOK);return TRUE;}
    if(id>=2200&&id<2228&&event==BN_CLICKED){const int i=(id-2200)/2;float value;if(Numeric(d,i,value)){if(value<0)value=DefaultValue(i);value=std::clamp(value+((id&1)?ExtraOptions[i].step:-ExtraOptions[i].step),ExtraOptions[i].minimum,ExtraOptions[i].maximum);SetDlgItemTextW(d,1100+i,Number(value).c_str());}return TRUE;}
    if(id>=1100&&id<1114&&(event==EN_SETFOCUS||event==CBN_SETFOCUS))SetDlgItemTextW(d,1150,ExtraOptions[id-1100].help);
    return FALSE;
}
}
bool EditOutputOptions(HINSTANCE module,HWND parent,RendererExtras& values,std::function<HRESULT(const RendererExtras&)> preview){Editor e;e.draft=values;e.preview=std::move(preview);if(DialogBoxParamW(module,MAKEINTRESOURCEW(110),parent,Procedure,reinterpret_cast<LPARAM>(&e))!=IDOK)return false;values=e.draft;return true;}
