#include "LiveSettings.h"
#include "ShaderPage.h"
#include "UserShaderSource.h"
#include <cwctype>
#include <algorithm>
#include "ShaderParameterLabels.h"
static std::wstring Number(float n){wchar_t text[64];swprintf_s(text,L"%.6g",double(n));return text;}
static std::wstring Wide(const char* t){std::wstring s;for(;*t;t++)s+=wchar_t(static_cast<unsigned char>(*t));return s;}
static std::wstring Text(HWND d,int id){wchar_t text[128]={};GetDlgItemTextW(d,id,text,128);return text;}
ShaderPage::ShaderPage(IUnknown* outer,HRESULT* hr):CBasePropertyPage(NAME("FrankyRenderer shaders"),outer,112,210){*hr=S_OK;background=CreateSolidBrush(RGB(248,250,252));}
ShaderPage::~ShaderPage(){if(title)DeleteObject(title);if(background)DeleteObject(background);}
HRESULT ShaderPage::OnConnect(IUnknown* o){return o->QueryInterface(IID_PPV_ARGS(&config));}
HRESULT ShaderPage::OnDisconnect(){if(config){config->Release();config=nullptr;}return S_OK;}
void ShaderPage::Dirty(){m_bDirty=TRUE;if(m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);}
int ShaderPage::Position()const{for(int n=0;n<ShaderCount;n++)if(draft.order[n]==selected)return n;return 0;}
void ShaderPage::List(){loading=true;SendDlgItemMessageW(m_Dlg,1500,LB_RESETCONTENT,0,0);for(int id:draft.order){const auto& s=ShaderCatalog[id];std::wstring label=draft.shaders[id].enabled?L"[On] ":L"[Off] ";label+=s.label;SendDlgItemMessageW(m_Dlg,1500,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}SendDlgItemMessageW(m_Dlg,1500,LB_SETCURSEL,Position(),0);loading=false;}
void ShaderPage::Help(int p){const auto& s=ShaderCatalog[selected];if(p<0||p>=s.count)return;const auto& f=s.parameters[p];std::wstring text=ShaderParameterLabel(f.name)+L" ("+Wide(f.name)+L"): Default "+Number(f.initial);if(std::abs(f.minimum)<1e30f&&std::abs(f.maximum)<1e30f)text+=L"; range "+Number(f.minimum)+L" to "+Number(f.maximum);if(f.integer)text+=L"; whole numbers only";SetDlgItemTextW(m_Dlg,1590,text.c_str());}
void ShaderPage::Editor(){
 loading=true;const auto& s=ShaderCatalog[selected];const auto& c=draft.shaders[selected];
 std::wstring info=s.category;info+=L": ";info+=s.help;SetDlgItemTextW(m_Dlg,1502,info.c_str());SetDlgItemTextW(m_Dlg,1503,s.label);
 for(int p=0;p<ShaderParameterLimit;p++){
  const bool visible=p<s.count;for(int id:{1510+p,1530+p,1550+p,1570+p})ShowWindow(GetDlgItem(m_Dlg,id),visible?SW_SHOW:SW_HIDE);
  if(visible){SetDlgItemTextW(m_Dlg,1510+p,ShaderParameterLabel(s.parameters[p].name).c_str());SetDlgItemTextW(m_Dlg,1530+p,Number(c.overrides&(1u<<p)?c.values[p]:s.parameters[p].initial).c_str());}
 }
 EnableWindow(GetDlgItem(m_Dlg,1504),Position()>0);EnableWindow(GetDlgItem(m_Dlg,1505),Position()<ShaderCount-1);EnableWindow(GetDlgItem(m_Dlg,1506),s.count>0);
 SetDlgItemTextW(m_Dlg,1590,s.count?L"Select a parameter to see its default and range. Changes take effect immediately.":L"No adjustable parameters. Double-click the shader to toggle it; Changes take effect immediately.");loading=false;
}
bool ShaderPage::Read(){const auto& s=ShaderCatalog[selected];auto next=draft.shaders[selected];for(int p=0;p<s.count;p++){
 auto text=Text(m_Dlg,1530+p);for(auto& c:text)if(c==L',')c=L'.';wchar_t* end=nullptr;float value=wcstof(text.c_str(),&end);while(end&&iswspace(*end))++end;
 const auto& f=s.parameters[p];if(end==text.c_str()||!end||*end||!std::isfinite(value)||value<f.minimum||value>f.maximum||(f.integer&&std::floor(value)!=value)){Help(p);LiveSettings::Focus(GetDlgItem(m_Dlg,1530+p));return false;}
 if(!(next.overrides&(1u<<p))&&text==Number(f.initial))continue;next.overrides|=1u<<p;next.values[p]=value;
 }draft.shaders[selected]=next;return true;}
HRESULT ShaderPage::OnActivate(){if(!config)return E_UNEXPECTED;PlaceboSettings s;auto hr=config->GetSettings(&s);if(FAILED(hr))return hr;draft=s.userShaders;List();Editor();
 LOGFONTW f={};if(!title&&GetObjectW(reinterpret_cast<HFONT>(SendMessageW(m_Dlg,WM_GETFONT,0,0)),sizeof(f),&f)){f.lfWeight=FW_SEMIBOLD;f.lfHeight=f.lfHeight*4/3;title=CreateFontIndirectW(&f);}if(title)SendDlgItemMessageW(m_Dlg,150,WM_SETFONT,reinterpret_cast<WPARAM>(title),TRUE);LiveSettings::Attach(m_Dlg,config,[this](){auto hr=OnApplyChanges();if(SUCCEEDED(hr)&&m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_CLEAN);return hr;});return S_OK;}
HRESULT ShaderPage::OnApplyChanges(){if(!Read()||!ValidUserShaders(draft))return E_INVALIDARG;
 for(int i=0;i<ShaderCount;i++)if(draft.shaders[i].enabled&&LoadUserShaderSource(i).empty()){LiveSettings::Message(m_Dlg,L"A shader file could not be loaded. Keep the shaders folder beside PlaceboRenderer.ax.",ShaderCatalog[i].label,MB_OK|MB_ICONERROR);return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);}
 PlaceboSettings next;auto hr=config->GetSettings(&next);if(FAILED(hr))return hr;next.userShaders=draft;hr=config->SetSettings(&next);if(SUCCEEDED(hr))m_bDirty=FALSE;return hr;}
INT_PTR ShaderPage::OnReceiveMessage(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
 if(msg==WM_CTLCOLORDLG)return reinterpret_cast<INT_PTR>(background);
 if(msg==WM_CTLCOLORSTATIC){SetBkColor(reinterpret_cast<HDC>(w),RGB(248,250,252));SetTextColor(reinterpret_cast<HDC>(w),RGB(40,49,60));return reinterpret_cast<INT_PTR>(background);}
 if(msg==WM_COMMAND&&!loading){int id=LOWORD(w),event=HIWORD(w);
  if(id==1500&&event==LBN_SELCHANGE){const auto pos=SendDlgItemMessageW(m_Dlg,1500,LB_GETCURSEL,0,0);if(pos<0||pos>=ShaderCount)return TRUE;if(!Read()){SendDlgItemMessageW(m_Dlg,1500,LB_SETCURSEL,Position(),0);return TRUE;}selected=draft.order[pos];Editor();return TRUE;}
  if(id==1500&&event==LBN_DBLCLK){
   const auto pos=SendDlgItemMessageW(m_Dlg,1500,LB_GETCURSEL,0,0);if(pos<0||pos>=ShaderCount||!Read())return TRUE;
   selected=draft.order[pos];auto& shader=draft.shaders[selected];shader.enabled=!shader.enabled;List();Editor();LiveSettings::Focus(GetDlgItem(m_Dlg,1500));Dirty();return TRUE;
  }
  if((id==1504||id==1505)&&event==BN_CLICKED){if(!Read())return TRUE;const int pos=Position(),other=pos+(id==1504?-1:1);if(other>=0&&other<ShaderCount){std::swap(draft.order[pos],draft.order[other]);List();Editor();Dirty();}return TRUE;}
  if(id==1506&&event==BN_CLICKED){auto& c=draft.shaders[selected];c.overrides=0;std::fill(std::begin(c.values),std::end(c.values),0.f);Editor();Dirty();return TRUE;}
  if(id==1507&&event==BN_CLICKED){for(auto& c:draft.shaders)c.enabled=0;List();Editor();Dirty();return TRUE;}
  if((id>=1550&&id<1562)||(id>=1570&&id<1582)){if(event==BN_CLICKED&&Read()){
   const bool up=id>=1570;const int p=id-(up?1570:1550);if(p>=ShaderCatalog[selected].count)return TRUE;const auto& f=ShaderCatalog[selected].parameters[p];auto& c=draft.shaders[selected];const float span=f.maximum-f.minimum;const float step=f.integer||span>10?1.f:span<=.1f?.001f:span<=1?.01f:.05f;
   const float value=c.overrides&(1u<<p)?c.values[p]:f.initial;c.values[p]=std::clamp(value+(up?step:-step),f.minimum,f.maximum);c.overrides|=1u<<p;Editor();Help(p);Dirty();return TRUE;}}
  if(id>=1530&&id<1542){if(event==EN_SETFOCUS)Help(id-1530);if(event==EN_CHANGE)Dirty();}
 }
 return CBasePropertyPage::OnReceiveMessage(hwnd,msg,w,l);
}
