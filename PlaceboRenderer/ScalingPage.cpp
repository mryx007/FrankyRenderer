#include "LiveSettings.h"
#include "ScalingPage.h"
#include "NumberControls.h"
#include <commctrl.h>
#include <cwctype>
#include <cwchar>
#include <cstdlib>
#pragma comment(lib,"comctl32.lib")
static thread_local std::function<HRESULT(const BlackBarSettings&)> BarsPreview;
static std::wstring Wide(const char* text){std::wstring s;for(;*text;text++)s+=static_cast<unsigned char>(*text);return s;}
static std::wstring Display(float value){wchar_t text[64];swprintf_s(text,L"%.6g",double(value));return text;}
static std::wstring Text(HWND dialog,int id){wchar_t text[128]={};GetDlgItemTextW(dialog,id,text,128);return text;}
static bool AutoNumber(HWND dialog,int id,float& value,bool& automatic){
    auto s=Text(dialog,id);for(auto& c:s)if(c==L',')c=L'.';
    automatic=s.empty()||!_wcsicmp(s.c_str(),L"Auto");if(automatic)return true;
    wchar_t* end=nullptr;value=wcstof(s.c_str(),&end);while(end&&iswspace(*end))++end;return end&&end!=s.c_str()&&!*end&&std::isfinite(value);
}
static INT_PTR CALLBACK BlackBarsDialog(HWND dlg,UINT msg,WPARAM w,LPARAM l){
    auto s=reinterpret_cast<BlackBarSettings*>(GetWindowLongPtrW(dlg,DWLP_USER));
    auto populate=[&](){
        CheckDlgButton(dlg,701,s->enabled?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(dlg,702,s->crop?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(dlg,703,s->keepSubtitles?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(dlg,708,s->stableZoom?BST_CHECKED:BST_UNCHECKED);
        SendDlgItemMessageW(dlg,704,CB_SETCURSEL,s->zoom,0);
        SetDlgItemTextW(dlg,705,Display(s->threshold).c_str());SetDlgItemTextW(dlg,706,Display(s->delay).c_str());
    };
    if(msg==WM_INITDIALOG){s=reinterpret_cast<BlackBarSettings*>(l);SetWindowLongPtrW(dlg,DWLP_USER,l);
        for(auto label:{L"Fit (Keep All Image Content)",L"Fill (May Crop Image Content)"})SendDlgItemMessageW(dlg,704,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        RECT unit={0,0,9,14};MapDialogRect(dlg,&unit);const auto font=SendMessageW(dlg,WM_GETFONT,0,0);
        for(int field:{705,706}){HWND edit=GetDlgItem(dlg,field);RECT r;GetWindowRect(edit,&r);MapWindowPoints(nullptr,dlg,reinterpret_cast<POINT*>(&r),2);const int width=r.right-r.left-2*unit.right-2;
            SetWindowPos(edit,nullptr,0,0,width,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
            for(bool up:{false,true}){auto button=CreateWindowExW(0,L"BUTTON",up?L"\u25B6":L"\u25C0",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,r.left+width+2+(up?unit.right:0),r.top,unit.right,r.bottom-r.top,dlg,reinterpret_cast<HMENU>(INT_PTR(NumberButtonId(field,up))),g_hInst,nullptr);SendMessageW(button,WM_SETFONT,font,TRUE);}}
        populate();if(BarsPreview)LiveSettings::AttachEditor(dlg,[dlg,s](){BlackBarSettings next=*s;next.enabled=IsDlgButtonChecked(dlg,701)==BST_CHECKED;next.crop=IsDlgButtonChecked(dlg,702)==BST_CHECKED;next.keepSubtitles=IsDlgButtonChecked(dlg,703)==BST_CHECKED;next.stableZoom=IsDlgButtonChecked(dlg,708)==BST_CHECKED;next.zoom=static_cast<int>(SendDlgItemMessageW(dlg,704,CB_GETCURSEL,0,0));bool automatic=false;if(!AutoNumber(dlg,705,next.threshold,automatic)||automatic||!AutoNumber(dlg,706,next.delay,automatic)||automatic||!ValidBlackBars(next))return E_INVALIDARG;return BarsPreview(next);});return TRUE;}
    if(msg==WM_COMMAND&&s){
        const int id=LOWORD(w);
        for(int field:{705,706})for(bool up:{false,true})if(id==NumberButtonId(field,up)){float value=0;bool automatic=false;if(AutoNumber(dlg,field,value,automatic)&&!automatic)SetDlgItemTextW(dlg,field,Display(std::clamp(value+(up?.1f:-.1f),0.f,10.f)).c_str());return TRUE;}
        if(id==IDCANCEL){EndDialog(dlg,IDCANCEL);return TRUE;}
        if(id==707){*s={};populate();return TRUE;}
        if(id==IDOK){BlackBarSettings next=*s;next.enabled=IsDlgButtonChecked(dlg,701)==BST_CHECKED;next.crop=IsDlgButtonChecked(dlg,702)==BST_CHECKED;next.keepSubtitles=IsDlgButtonChecked(dlg,703)==BST_CHECKED;next.stableZoom=IsDlgButtonChecked(dlg,708)==BST_CHECKED;
            next.zoom=static_cast<int>(SendDlgItemMessageW(dlg,704,CB_GETCURSEL,0,0));bool automatic=false;
            if(!AutoNumber(dlg,705,next.threshold,automatic)||automatic||!AutoNumber(dlg,706,next.delay,automatic)||automatic||!ValidBlackBars(next)){
                LiveSettings::Message(dlg,L"Threshold: 0 to 10 percent. Change Delay: 0 to 10 seconds.",L"Check Black Bar Settings",MB_OK|MB_ICONERROR);return TRUE;}
            *s=next;EndDialog(dlg,IDOK);return TRUE;
        }
    }
    return FALSE;
}
ScalingPage::ScalingPage(IUnknown* outer,HRESULT* hr):CBasePropertyPage(NAME("FrankyRenderer scaling"),outer,106,206){*hr=S_OK;background=CreateSolidBrush(RGB(248,250,252));}
ScalingPage::~ScalingPage(){if(tip)DestroyWindow(tip);if(title)DeleteObject(title);if(background)DeleteObject(background);}
HRESULT ScalingPage::OnConnect(IUnknown* object){return object->QueryInterface(IID_PPV_ARGS(&config));}
HRESULT ScalingPage::OnDisconnect(){if(config){config->Release();config=nullptr;}return S_OK;}
HRESULT ScalingPage::OnDeactivate(){if(tip){DestroyWindow(tip);tip=nullptr;}return S_OK;}
void ScalingPage::Fill(int id,int role,int kind,const char* selected){
    auto& list=choices[id];list.clear();SendDlgItemMessageW(m_Dlg,id,CB_RESETCONTENT,0,0);
    auto add=[&](const char* name,const std::wstring& label){list.emplace_back(name);SendDlgItemMessageW(m_Dlg,id,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(!strcmp(name,selected))SendDlgItemMessageW(m_Dlg,id,CB_SETCURSEL,list.size()-1,0);};
    add("auto",L"Auto");add("none",kind==1?L"None (Empty Base)":kind>=2?L"None":role==0?L"None (GPU Sampling)":role==4?L"None (Nearest Frame)":L"None (Inherit Image Filter)");
    if(kind==0)add("custom",L"Custom");
    if(kind<2){for(int i=0;i<pl_num_filter_configs;i++){const auto* f=pl_filter_configs[i];if(f->allowed&ScalingUsage(role))add(f->name,Wide(f->name));}}
    else for(int i=0;i<pl_num_filter_functions;i++)add(pl_filter_functions[i]->name,Wide(pl_filter_functions[i]->name));
    SendDlgItemMessageW(m_Dlg,id,CB_SETDROPPEDWIDTH,390,0);
}
const char* ScalingPage::Selected(int id){auto at=SendDlgItemMessageW(m_Dlg,id,CB_GETCURSEL,0,0);const auto& list=choices[id];return at>=0&&at<static_cast<LRESULT>(list.size())?list[at].c_str():nullptr;}
void ScalingPage::Tool(int id,const std::wstring& text){
    if(!tip||!GetDlgItem(m_Dlg,id))return;help[id]=text;
    for(int field:{id,NumberButtonId(id,false),NumberButtonId(id,true)})if(HWND window=GetDlgItem(m_Dlg,field)){
        TOOLINFOW t={};t.cbSize=TTTOOLINFOW_V2_SIZE;t.uFlags=TTF_IDISHWND|TTF_SUBCLASS;t.hwnd=m_Dlg;t.uId=reinterpret_cast<UINT_PTR>(window);t.lpszText=help[id].data();
        if(!SendMessageW(tip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&t)))SendMessageW(tip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&t));
    }
}
void ScalingPage::Editor(){
    loading=true;const auto& f=draft.filters[target];
    Fill(611,target,1,f.preset);Fill(612,target,2,f.kernel);Fill(613,target,3,f.window);
    for(int i=0;i<9;i++){const auto text=f.overrides&(1u<<i)?Display(f.values[i]):L"Auto";SetDlgItemTextW(m_Dlg,620+i,text.c_str());}
    SendDlgItemMessageW(m_Dlg,630,CB_RESETCONTENT,0,0);for(auto value:{L"Auto",L"No",L"Yes"})SendDlgItemMessageW(m_Dlg,630,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));SendDlgItemMessageW(m_Dlg,630,CB_SETCURSEL,f.polar+1,0);
    const bool custom=!strcmp(f.filter,"custom");
    SetDlgItemTextW(m_Dlg,641,custom?L"Editing":L"Customize");EnableWindow(GetDlgItem(m_Dlg,641),!custom&&strcmp(f.filter,"none"));
    Tool(641,L"Copy the selected filter into Custom mode. Adjust the enabled fields, then click Changes take effect immediately.");
    for(int id:{611,612,613,620,621,622,623,624,625,626,627,628,630})EnableWindow(GetDlgItem(m_Dlg,id),custom);
    auto o=pl_options_alloc(nullptr);pl_options_reset(o,&pl_render_default_params);const bool valid=ApplyScaling(o,draft);const auto* active=valid?ScalingFilter(o,target):nullptr;
    Tool(611,L"Copy a filter as the custom starting point. Auto uses this role's default base.");Tool(612,L"Reconstruction kernel. Auto inherits the starting filter.");Tool(613,L"Window function. Auto inherits; None removes windowing.");
    const wchar_t* descriptions[]={L"Kernel support radius (0 to 16). Only resizable kernels use this override. Temporal radius times blur must stay at or below 8.",L"Clamp (0 to 1). Suppress negative weights; higher values reduce ringing.",L"Blur (0 to 100). Below 1 sharpens, above 1 softens; 0 uses the filter default.",L"Taper (0 to 1). Flatten the kernel center.",L"Anti-ringing (0 to 1). Override this filter's anti-ringing; zero inherits the global strength.",L"First tunable kernel parameter.",L"Second tunable kernel parameter.",L"First tunable window parameter.",L"Second tunable window parameter."};
    for(int i=0;i<9;i++){
        bool supported=custom&&active&&active->kernel;
        if(i==0)supported=supported&&active->kernel->resizable;
        if(i==5||i==6)supported=supported&&active->kernel->tunable[i-5];
        if(i==7||i==8)supported=supported&&active->window&&active->window->tunable[i-7];
        EnableWindow(GetDlgItem(m_Dlg,620+i),supported);for(bool up:{false,true})EnableWindow(GetDlgItem(m_Dlg,NumberButtonId(620+i,up)),supported);
        std::wstring info=descriptions[i];info+=L"\nAuto inherits the selected filter. A typed 0 is an explicit value.";
        if(active){float v=0;switch(i){case 0:v=active->radius>0?active->radius:active->kernel?active->kernel->radius:0;break;case 1:v=active->clamp;break;case 2:v=active->blur;break;case 3:v=active->taper;break;case 4:v=active->antiring;break;case 5:case 6:v=active->params[i-5];break;default:v=active->wparams[i-7];}info+=L"\nCurrent resolved value: "+Display(v);}
        Tool(620+i,info);
    }
    EnableWindow(GetDlgItem(m_Dlg,630),custom&&target!=4);Tool(630,L"Use a two-dimensional EWA filter instead of separable filtering. Temporal filters remain one-dimensional.");
    std::wstring info=L"Auto inherits libplacebo. Custom controls apply to the selected role only.";
    if(custom)info=L"Editing custom filter. Auto inherits the starting filter; grey fields are not supported by this kernel.";
    else if(!strcmp(f.filter,"none"))info=L"Select a scaling filter before clicking Customize.";
    else info=L"Click Customize to edit the selected filter. Changes take effect immediately.";
    SetDlgItemTextW(m_Dlg,651,info.c_str());pl_options_free(&o);loading=false;
}
bool ScalingPage::ReadEditor(){
    auto next=draft.filters[target];auto copy=[&](int id,char (&out)[64]){const char* value=Selected(id);if(!value)return false;strcpy_s(out,value);return true;};
    if(!copy(611,next.preset)||!copy(612,next.kernel)||!copy(613,next.window))return false;
    for(int i=0;i<9;i++){bool automatic=false;float value=0;if(!AutoNumber(m_Dlg,620+i,value,automatic))return false;if(automatic)next.overrides&=~(1u<<i);else{next.overrides|=1u<<i;next.values[i]=value;}}
    const auto polar=SendDlgItemMessageW(m_Dlg,630,CB_GETCURSEL,0,0);if(polar<0)return false;next.polar=static_cast<int>(polar)-1;draft.filters[target]=next;return true;
}
void ScalingPage::Populate(){
    loading=true;for(int i=0;i<5;i++)Fill(600+i,i,0,draft.filters[i].filter);
    SendDlgItemMessageW(m_Dlg,610,CB_RESETCONTENT,0,0);for(auto label:ScalingLabels)SendDlgItemMessageW(m_Dlg,610,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendDlgItemMessageW(m_Dlg,610,CB_SETCURSEL,target,0);
    for(auto field:{std::pair{606,skipAntiAlias},std::pair{607,preserveMix}}){SendDlgItemMessageW(m_Dlg,field.first,CB_RESETCONTENT,0,0);for(auto label:{L"Default (Off)",L"Off",L"On"})SendDlgItemMessageW(m_Dlg,field.first,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendDlgItemMessageW(m_Dlg,field.first,CB_SETCURSEL,field.second+1,0);}
    SetDlgItemTextW(m_Dlg,605,draft.antiringing<0?L"Auto":Display(draft.antiringing).c_str());loading=false;Editor();
}
void ScalingPage::Dirty(){m_bDirty=TRUE;if(m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);}
void ScalingPage::Step(int id,bool up){
    bool automatic=false;float value=0;if(!AutoNumber(m_Dlg,id,value,automatic))return;
    const int field=id-620;const float step=id==605||field==1||field==3||field==4?.05f:.1f;
    if(automatic)value=id==622?1.f:0.f;
    value+=up?step:-step;if(id==605||field<5)value=std::max(0.f,value);
    if(id==605||field==1||field==3||field==4)value=std::min(1.f,value);if(field==0)value=std::min(16.f,value);if(field==2)value=std::min(100.f,value);
    SetDlgItemTextW(m_Dlg,id,Display(value).c_str());
}
HRESULT ScalingPage::OnActivate(){
    if(!config)return E_UNEXPECTED;PlaceboSettings current;HRESULT hr=config->GetSettings(&current);if(FAILED(hr))return hr;draft=current.scaling;skipAntiAlias=static_cast<int>(current.extras.values[15]);preserveMix=static_cast<int>(current.extras.values[16]);
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,m_Dlg,nullptr,g_hInst,nullptr);if(tip){SendMessageW(tip,TTM_SETMAXTIPWIDTH,0,480);SendMessageW(tip,TTM_SETDELAYTIME,TTDT_AUTOPOP,25000);}
    RECT unit={0,0,10,14};MapDialogRect(m_Dlg,&unit);const auto font=SendMessageW(m_Dlg,WM_GETFONT,0,0);
    for(int id:{605,620,621,622,623,624,625,626,627,628})if(!GetDlgItem(m_Dlg,NumberButtonId(id,false))){HWND edit=GetDlgItem(m_Dlg,id);RECT rect;GetWindowRect(edit,&rect);MapWindowPoints(nullptr,m_Dlg,reinterpret_cast<POINT*>(&rect),2);const int w=rect.right-rect.left-2*unit.right-2;SetWindowPos(edit,nullptr,0,0,w,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER);for(bool up:{false,true}){auto button=CreateWindowExW(0,L"BUTTON",up?L"\u25B6":L"\u25C0",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,rect.left+w+2+(up?unit.right:0),rect.top,unit.right,rect.bottom-rect.top,m_Dlg,reinterpret_cast<HMENU>(INT_PTR(NumberButtonId(id,up))),g_hInst,nullptr);SendMessageW(button,WM_SETFONT,font,TRUE);}}
    blackBars=current.blackBars;Populate();CheckDlgButton(m_Dlg,240,current.smoothMotion?BST_CHECKED:BST_UNCHECKED);
    Tool(606,ExtraOptions[15].help);Tool(607,ExtraOptions[16].help);
    const wchar_t* roles[]={L"Enlarge the image.",L"Reduce the image.",L"Enlarge subsampled planes, including chroma.",L"Reduce individual planes.",L"Temporal interpolation. Requires Smooth Motion; Auto uses oversampling."};
    for(int i=0;i<5;i++)Tool(600+i,std::wstring(roles[i])+L" Auto uses the library default.");Tool(605,L"Global anti-ringing strength. Auto uses the library default; manual range 0 to 1.");Tool(240,L"Resample video at the display cadence. Wider temporal filters require more buffered frames and startup delay.");Tool(640,L"Reset Scaling to Auto. Other pages and the Smooth Motion switch are preserved.");
    LOGFONTW f={};if(!title&&GetObjectW(reinterpret_cast<HFONT>(font),sizeof(f),&f)){f.lfWeight=FW_SEMIBOLD;f.lfHeight=f.lfHeight*4/3;title=CreateFontIndirectW(&f);}if(title)SendDlgItemMessageW(m_Dlg,150,WM_SETFONT,reinterpret_cast<WPARAM>(title),TRUE);LiveSettings::Attach(m_Dlg,config,[this](){auto hr=OnApplyChanges();if(SUCCEEDED(hr)&&m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_CLEAN);return hr;});return S_OK;
}
HRESULT ScalingPage::OnApplyChanges(){
    if(!ReadEditor())return E_INVALIDARG;bool automatic=false;float value=0;if(!AutoNumber(m_Dlg,605,value,automatic))return E_INVALIDARG;draft.antiringing=automatic?-1:value;
    if(!ValidScaling(draft)){LiveSettings::Message(m_Dlg,L"Check the custom kernel and parameter ranges. Frame mixers must be one-dimensional and have an effective radius of at most 8 frames.",L"Check Scaling Settings",MB_OK|MB_ICONERROR);return E_INVALIDARG;}
    PlaceboSettings next;HRESULT hr=config->GetSettings(&next);if(FAILED(hr))return hr;const auto aa=SendDlgItemMessageW(m_Dlg,606,CB_GETCURSEL,0,0),cache=SendDlgItemMessageW(m_Dlg,607,CB_GETCURSEL,0,0);if(aa<0||aa>2||cache<0||cache>2)return E_INVALIDARG;next.extras.values[15]=static_cast<float>(aa-1);next.extras.values[16]=static_cast<float>(cache-1);skipAntiAlias=static_cast<int>(aa-1);preserveMix=static_cast<int>(cache-1);next.scaling=draft;next.blackBars=blackBars;next.smoothMotion=IsDlgButtonChecked(m_Dlg,240)==BST_CHECKED;hr=config->SetSettings(&next);if(SUCCEEDED(hr)){m_bDirty=FALSE;if(!LiveSettings::silent)Editor();}return hr;
}
INT_PTR ScalingPage::OnReceiveMessage(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_CTLCOLORDLG)return reinterpret_cast<INT_PTR>(background);
    if(msg==WM_CTLCOLORSTATIC){SetBkColor(reinterpret_cast<HDC>(w),RGB(248,250,252));SetTextColor(reinterpret_cast<HDC>(w),RGB(40,49,60));return reinterpret_cast<INT_PTR>(background);}
    if(msg==WM_COMMAND&&!loading){const int id=LOWORD(w),event=HIWORD(w);
        if(id==641&&event==BN_CLICKED){
            if(!ReadEditor())return TRUE;const auto current=draft.filters[target];
            if(!strcmp(current.filter,"custom")||!strcmp(current.filter,"none"))return TRUE;
            ScalerSettings custom;strcpy_s(custom.filter,"custom");strcpy_s(custom.preset,current.filter);
            draft.filters[target]=custom;loading=true;Fill(600+target,target,0,"custom");loading=false;Editor();Dirty();return TRUE;
        }
        if(id==608&&event==BN_CLICKED){const auto before=blackBars;auto next=blackBars;BarsPreview=[this](const BlackBarSettings& value){blackBars=value;PlaceboSettings current;auto hr=config->GetSettings(&current);if(FAILED(hr))return hr;current.blackBars=value;return config->SetSettings(&current);};if(DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(111),m_Dlg,BlackBarsDialog,reinterpret_cast<LPARAM>(&next))==IDOK){blackBars=next;Dirty();}else if(LiveSettings::Enabled())BarsPreview(before);BarsPreview={};return TRUE;}
        if(id==640&&event==BN_CLICKED){draft={};skipAntiAlias=preserveMix=-1;Populate();Dirty();return TRUE;}
        if(event==BN_CLICKED)for(int field:{605,620,621,622,623,624,625,626,627,628})for(bool up:{false,true})if(id==NumberButtonId(field,up)){Step(field,up);return TRUE;}
        if(event==CBN_SELCHANGE&&id==610){const int next=static_cast<int>(SendDlgItemMessageW(m_Dlg,610,CB_GETCURSEL,0,0));if(next<0||next>=5||!ReadEditor()){SendDlgItemMessageW(m_Dlg,610,CB_SETCURSEL,target,0);return TRUE;}target=next;Editor();return TRUE;}
        if(event==CBN_SELCHANGE&&id>=600&&id<=604){if(!ReadEditor())return TRUE;const char* selected=Selected(id);if(selected)strcpy_s(draft.filters[id-600].filter,selected);target=id-600;SendDlgItemMessageW(m_Dlg,610,CB_SETCURSEL,target,0);Editor();Dirty();return TRUE;}
        if(event==CBN_SELCHANGE&&(id==611||id==612||id==613||id==630)){if(ReadEditor())Editor();Dirty();return TRUE;}
        if(event==CBN_SELCHANGE&&(id==606||id==607)){skipAntiAlias=static_cast<int>(SendDlgItemMessageW(m_Dlg,606,CB_GETCURSEL,0,0))-1;preserveMix=static_cast<int>(SendDlgItemMessageW(m_Dlg,607,CB_GETCURSEL,0,0))-1;Dirty();return TRUE;}
        if(event==EN_CHANGE||event==BN_CLICKED){Dirty();return TRUE;}
    }
    return CBasePropertyPage::OnReceiveMessage(hwnd,msg,w,l);
}
