#include "LiveSettings.h"
#include "PropertyPage.h"
#include "SettingsChoices.h"
#include "NumberControls.h"
#include "UiHelp.h"
#include "OutputOptions.h"
#include <commctrl.h>
#pragma comment(lib,"comctl32.lib")
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <utility>
#include <commdlg.h>
#include <wrl/client.h>
static float DebandDefault(int id){const auto& d=pl_render_default_params.deband_params?*pl_render_default_params.deband_params:pl_deband_default_params;switch(id){case 210:return float(d.iterations);case 211:return d.threshold;case 212:return d.radius;default:return d.grain;}}
SettingsPage::SettingsPage(IUnknown* outer,HRESULT* hr,int pageKind):CBasePropertyPage(NAME("FrankyRenderer settings"),outer,101+pageKind,201+pageKind),processing(pageKind==1),color(pageKind==2),hdrDetails(pageKind==3),effects(pageKind==4),debanding(pageKind==6),adjustments(pageKind==7){*hr=S_OK;background=CreateSolidBrush(RGB(248,250,252));}
SettingsPage::~SettingsPage(){if(helpTip)DestroyWindow(helpTip);if(titleFont)DeleteObject(titleFont);if(background)DeleteObject(background);}
void SettingsPage::AddHelp(){
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    if(helpTip&&IsWindow(helpTip))return;
    helpTip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
        CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,m_Dlg,nullptr,g_hInst,nullptr);
    if(!helpTip)return;
    SendMessageW(helpTip,TTM_SETMAXTIPWIDTH,0,420);
    SendMessageW(helpTip,TTM_SETDELAYTIME,TTDT_INITIAL,450);
    SendMessageW(helpTip,TTM_SETDELAYTIME,TTDT_AUTOPOP,20000);
    for(const auto& help:UiHelp)for(int id:{help.id,NumberButtonId(help.id,false),NumberButtonId(help.id,true)})if(HWND field=GetDlgItem(m_Dlg,id)){
        TOOLINFOW tool={};tool.cbSize=TTTOOLINFOW_V2_SIZE;tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;
        tool.hwnd=m_Dlg;tool.uId=reinterpret_cast<UINT_PTR>(field);tool.lpszText=const_cast<wchar_t*>(help.text);
        SendMessageW(helpTip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    }
}
HRESULT SettingsPage::OnDeactivate(){hdrPositions.clear();if(helpTip){DestroyWindow(helpTip);helpTip=nullptr;}return S_OK;}
void SettingsPage::AddNumberButtons(){
    RECT unit={0,0,10,14};MapDialogRect(m_Dlg,&unit);const int buttonWidth=unit.right;
    const auto font=SendMessageW(m_Dlg,WM_GETFONT,0,0);
    for(const auto& control:NumberControls){
        HWND field=GetDlgItem(m_Dlg,control.id);if(!field||GetDlgItem(m_Dlg,NumberButtonId(control.id,false)))continue;
        RECT rect={};GetWindowRect(field,&rect);MapWindowPoints(nullptr,m_Dlg,reinterpret_cast<POINT*>(&rect),2);
        const int editWidth=rect.right-rect.left-2*buttonWidth-2;
        SetWindowPos(field,nullptr,0,0,editWidth,rect.bottom-rect.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        for(bool increase:{false,true}){
            HWND button=CreateWindowExW(0,L"BUTTON",increase?L"\u25B6":L"\u25C0",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                rect.left+editWidth+2+(increase?buttonWidth:0),rect.top,buttonWidth,rect.bottom-rect.top,m_Dlg,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(NumberButtonId(control.id,increase))),g_hInst,nullptr);
            if(button)SendMessageW(button,WM_SETFONT,font,TRUE);
        }
    }
}
bool SettingsPage::HandleNumberButton(int id){
    for(const auto& control:NumberControls)for(bool increase:{false,true})if(id==NumberButtonId(control.id,increase)){
        if(!IsWindowEnabled(GetDlgItem(m_Dlg,control.id)))return true;
        float value=0;if(!Number(control.id,value))return true;
        if((debanding||processing)&&value==-1){const auto& d=pl_render_default_params.deband_params?*pl_render_default_params.deband_params:pl_deband_default_params;switch(control.id){case 210:value=float(d.iterations);break;case 211:value=d.threshold;break;case 212:value=d.radius;break;case 213:value=d.grain;break;}}
        const double next=StepNumber(control,value,increase);if(std::abs(next-value)<1e-7)return true;
        wchar_t text[64];swprintf_s(text,L"%.6g",next);SetDlgItemTextW(m_Dlg,control.id,text);return true;
    }
    return false;
}
void SettingsPage::UpdateControls(){
    UpdateControlStates();
    for(const auto& control:NumberControls)if(HWND field=GetDlgItem(m_Dlg,control.id))
        for(bool increase:{false,true})if(HWND button=GetDlgItem(m_Dlg,NumberButtonId(control.id,increase)))EnableWindow(button,IsWindowEnabled(field));
}
void SettingsPage::ShowDiagnostics(bool details){
    Microsoft::WRL::ComPtr<IPlaceboDiagnostics> source;PlaceboDiagnostics d;
    if(!config||FAILED(config->QueryInterface(IID_PPV_ARGS(&source)))||FAILED(source->GetDiagnostics(&d)))return;
    if(!d.haveVideo){SetDlgItemTextW(m_Dlg,140,L"No video output yet");if(details)LiveSettings::Message(m_Dlg,L"No video output yet. Play a video and open the status again.",L"Playback Status",MB_OK|MB_ICONINFORMATION);return;}
    wchar_t line[256];swprintf_s(line,L"%d × %d | %hs | %s | %s",d.width,d.height,d.pixelFormat,d.directGpu?L"Direct GPU":L"GPU Upload",SignalName(d.signal));SetDlgItemTextW(m_Dlg,140,line);
    if(details){std::wstring peaks;
        if(d.signal!=SourceSignal::SDR){wchar_t value[96];
            if(std::isfinite(d.sourcePeak)&&d.sourcePeak>0){swprintf_s(value,L"\nSource Peak (Metadata): %.1f Nits",d.sourcePeak);peaks+=value;}
            if(std::isfinite(d.scenePeak)&&d.scenePeak>0){swprintf_s(value,L"\nScene Peak (Metadata): %.1f Nits",d.scenePeak);peaks+=value;}
        }
        wchar_t depth[64];if(d.ditherDepth)swprintf_s(depth,L"%d Bit",d.ditherDepth);else wcscpy_s(depth,L"Auto (Float)");wchar_t text[1700];swprintf_s(text,L"Input: %d × %d, %hs (%d Bit)\nTransfer: %s\nSource Signal: %s\nImage Processing: libplacebo on GPU\nOutput Texture: %d Bit %s\nRGB Output Levels: %s\nDither Target Precision: %s\nDeinterlacing: %s\nSmooth Motion: %s (Display %.3f Hz)\nAdaptive Sharpen: %s%s\n\nPresented Frames: %llu\nLast Presentation: %s",d.width,d.height,d.pixelFormat,d.bitDepth,d.directGpu?L"Direct D3D11 Texture":L"Arbeitsspeicher, danach GPU Upload",SignalName(d.signal),std::abs(d.outputTextureFormat),d.outputTextureFormat<0?L"Float":L"Integer",d.outputLevels==2?L"TV Levels (16–235)":L"PC Levels (0–255)",depth,d.deinterlacing==1?L"Bwdif":d.deinterlacing==2?L"Yadif":L"Inactive",d.smoothMotion?L"Active":L"Off / Inactive",d.displayHz,d.sharpenState==1?L"Active":d.sharpenState==2?L"Bypassed (linear/unsupported output)":L"Off",peaks.c_str(),d.frames,SUCCEEDED(d.result)?L"Success":L"Error");LiveSettings::Message(m_Dlg,text,L"Playback Status",MB_OK|MB_ICONINFORMATION);}
}
HRESULT SettingsPage::OnConnect(IUnknown* object){return object->QueryInterface(IID_PPV_ARGS(&config));}
HRESULT SettingsPage::OnDisconnect(){if(config){config->Release();config=nullptr;}return S_OK;}
void SettingsPage::PopulateDebanding(){
    wchar_t text[96];
    SendDlgItemMessageW(m_Dlg,220,CB_RESETCONTENT,0,0);for(auto mode:{L"Off",L"On"})SendDlgItemMessageW(m_Dlg,220,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(mode));SendDlgItemMessageW(m_Dlg,220,CB_SETCURSEL,settings.deband?1:0,0);
    for(auto pair:{std::pair{210,float(settings.debandIterations)},std::pair{211,settings.debandThreshold},std::pair{212,settings.debandRadius},std::pair{213,settings.debandGrain}}){debandDefaults[pair.first-210]=pair.second<0;swprintf_s(text,L"%.6g",double(pair.second<0?DebandDefault(pair.first):pair.second));SetDlgItemTextW(m_Dlg,pair.first,text);}
    const auto& d=pl_render_default_params.deband_params?*pl_render_default_params.deband_params:pl_deband_default_params;
    swprintf_s(text,L"Default: %d | 0-16",d.iterations);SetDlgItemTextW(m_Dlg,711,text);
    for(auto pair:{std::pair{712,d.threshold},std::pair{713,d.radius},std::pair{714,d.grain}}){swprintf_s(text,L"Default: %.6g | 0-1000",double(pair.second));SetDlgItemTextW(m_Dlg,pair.first,text);}
}
void SettingsPage::PopulateDeinterlacing(){
    SendDlgItemMessageW(m_Dlg,132,CB_RESETCONTENT,0,0);for(auto method:{L"Off",L"Bwdif",L"Yadif"})SendDlgItemMessageW(m_Dlg,132,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(method));SendDlgItemMessageW(m_Dlg,132,CB_SETCURSEL,settings.deinterlacing,0);
    SendDlgItemMessageW(m_Dlg,133,CB_RESETCONTENT,0,0);for(auto order:{L"Auto",L"Force Top Field First",L"Force Bottom Field First"})SendDlgItemMessageW(m_Dlg,133,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(order));SendDlgItemMessageW(m_Dlg,133,CB_SETCURSEL,settings.fieldOverride,0);
    SendDlgItemMessageW(m_Dlg,133,CB_SETDROPPEDWIDTH,240,0);
}
bool SettingsPage::ReadDebanding(PlaceboSettings& next){
    const auto mode=SendDlgItemMessageW(m_Dlg,220,CB_GETCURSEL,0,0);if(mode<0||mode>1)return false;next.deband=static_cast<BOOL>(mode);
    float iterations=0;if(!Number(210,iterations)||iterations<-1||iterations>16||std::floor(iterations)!=iterations||!Number(211,next.debandThreshold)||!Number(212,next.debandRadius)||!Number(213,next.debandGrain))return false;next.debandIterations=static_cast<int>(iterations);return true;
}
bool SettingsPage::ReadDeinterlacing(PlaceboSettings& next){
    const auto deinterlace=SendDlgItemMessageW(m_Dlg,132,CB_GETCURSEL,0,0),order=SendDlgItemMessageW(m_Dlg,133,CB_GETCURSEL,0,0);if(deinterlace<0||deinterlace>2||order<0||order>2)return false;
    next.deinterlacing=static_cast<int>(deinterlace);next.fieldOverride=static_cast<int>(order);return true;
}
void SettingsPage::Populate(){
    loading=true;wchar_t text[64];auto number=[&](int id,float value){swprintf_s(text,L"%.6g",value);SetDlgItemTextW(m_Dlg,id,text);};
    if(adjustments){CheckDlgButton(m_Dlg,720,settings.colorAdjustment?BST_CHECKED:BST_UNCHECKED);number(110,settings.saturation-1);number(111,settings.contrast-1);number(112,settings.brightness);number(113,settings.gamma-1);number(730,settings.hue*180.f/3.14159265358979323846f);number(731,6500+3500*settings.temperature);loading=false;UpdateControls();return;}
    if(debanding){PopulateDebanding();loading=false;UpdateControls();return;}
    if(effects){CheckDlgButton(m_Dlg,500,settings.adaptiveSharpen?BST_CHECKED:BST_UNCHECKED);number(510,settings.sharpenStrength);loading=false;UpdateControls();return;}
    if(hdrDetails){
        number(114,settings.sdrNits);number(115,settings.hdrNits);SendDlgItemMessageW(m_Dlg,121,CB_RESETCONTENT,0,0);for(auto mode:{L"Auto",L"Passthrough HDR to Display",L"Tone Map HDR using GPU Shaders"})SendDlgItemMessageW(m_Dlg,121,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(mode));SendDlgItemMessageW(m_Dlg,121,CB_SETCURSEL,settings.hdrMode,0);CheckDlgButton(m_Dlg,122,settings.autoHdr?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(m_Dlg,415,settings.allowDelayedPeak?BST_CHECKED:BST_UNCHECKED);
        number(414,settings.blackCutoff);
        CheckDlgButton(m_Dlg,400,settings.peakDetect?BST_CHECKED:BST_UNCHECKED);number(410,settings.peakSmoothing);number(411,settings.sceneLow);number(412,settings.sceneHigh);number(413,settings.peakPercentile);
        loading=false;UpdateControls();return;
    }
    if(color){
        SendDlgItemMessageW(m_Dlg,318,CB_RESETCONTENT,0,0);for(auto label:{L"Default (SDR Calibration)",L"Unknown (Infer from LUT)",L"Native (Gamma Light)",L"Normalized (Linear Light)",L"Conversion (Replaces Mapping)"})SendDlgItemMessageW(m_Dlg,318,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendDlgItemMessageW(m_Dlg,318,CB_SETCURSEL,static_cast<int>(settings.extras.values[14])+1,0);
        CheckDlgButton(m_Dlg,300,settings.iccEnabled?BST_CHECKED:BST_UNCHECKED);CheckDlgButton(m_Dlg,320,settings.lutEnabled?BST_CHECKED:BST_UNCHECKED);
        SendDlgItemMessageW(m_Dlg,310,EM_SETLIMITTEXT,1023,0);SendDlgItemMessageW(m_Dlg,321,EM_SETLIMITTEXT,1023,0);
        SetDlgItemTextW(m_Dlg,310,settings.iccPath);SetDlgItemTextW(m_Dlg,321,settings.lutPath);
        SendDlgItemMessageW(m_Dlg,312,CB_RESETCONTENT,0,0);for(auto intent:{L"Relative Colorimetric",L"Perceptual"})SendDlgItemMessageW(m_Dlg,312,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(intent));SendDlgItemMessageW(m_Dlg,312,CB_SETCURSEL,settings.iccIntent,0);
        loading=false;UpdateControls();return;
    }
    if(processing){
        outputDraft=settings.extras;PopulateDebanding();PopulateDeinterlacing();
        CheckDlgButton(m_Dlg,500,settings.adaptiveSharpen?BST_CHECKED:BST_UNCHECKED);number(510,settings.sharpenStrength);
        
        CheckDlgButton(m_Dlg,231,settings.ditherTemporal?BST_CHECKED:BST_UNCHECKED);
        SendDlgItemMessageW(m_Dlg,230,CB_RESETCONTENT,0,0);for(auto choice:DitherChoices)SendDlgItemMessageW(m_Dlg,230,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice));SendDlgItemMessageW(m_Dlg,230,CB_SETCURSEL,settings.ditherMethod,0);
        SendDlgItemMessageW(m_Dlg,239,CB_RESETCONTENT,0,0);SendDlgItemMessageW(m_Dlg,239,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Auto"));for(int frames=1;frames<=16;frames++){swprintf_s(text,L"%d",frames);SendDlgItemMessageW(m_Dlg,239,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));}SendDlgItemMessageW(m_Dlg,239,CB_SETCURSEL,settings.maximumFramesInFlight,0);
        SendDlgItemMessageW(m_Dlg,236,CB_RESETCONTENT,0,0);for(auto choice:OutputChoices)SendDlgItemMessageW(m_Dlg,236,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice));SendDlgItemMessageW(m_Dlg,236,CB_SETCURSEL,settings.outputFormat,0);
        SendDlgItemMessageW(m_Dlg,237,CB_RESETCONTENT,0,0);for(auto choice:LevelChoices)SendDlgItemMessageW(m_Dlg,237,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice));SendDlgItemMessageW(m_Dlg,237,CB_SETCURSEL,settings.outputLevels,0);
        SendDlgItemMessageW(m_Dlg,238,CB_RESETCONTENT,0,0);for(size_t i=0;i<std::size(DisplayDepths);i++){SendDlgItemMessageW(m_Dlg,238,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(DepthChoices[i]));if(settings.displayDepth==DisplayDepths[i])SendDlgItemMessageW(m_Dlg,238,CB_SETCURSEL,i,0);}
        loading=false;UpdateControls();return;
    }
    PopulateDeinterlacing();
    loading=false;UpdateControls();
}
void SettingsPage::UpdateControlStates(){if(adjustments){SetDlgItemTextW(m_Dlg,735,IsDlgButtonChecked(m_Dlg,720)==BST_CHECKED?L"Adjustments are On. All values below are active.":L"Adjustments are Off. Values are retained for later use.");return;}if(debanding){const auto mode=SendDlgItemMessageW(m_Dlg,220,CB_GETCURSEL,0,0);const bool enabled=mode==1;SetDlgItemTextW(m_Dlg,705,enabled?L"Debanding is On. Parameters below are active.":L"Debanding is Off. Choose On to activate it; parameters can be edited below.");return;}if(effects){EnableWindow(GetDlgItem(m_Dlg,510),IsDlgButtonChecked(m_Dlg,500)==BST_CHECKED);return;}if(hdrDetails){const auto mode=SendDlgItemMessageW(m_Dlg,121,CB_GETCURSEL,0,0);EnableWindow(GetDlgItem(m_Dlg,115),mode==0);EnableWindow(GetDlgItem(m_Dlg,122),mode!=2);SetDlgItemTextW(m_Dlg,823,mode==1?L"HDR display handles tone mapping. If HDR is unavailable, falls back to SDR tone mapping.":mode==2?L"GPU tone mapping to SDR. Windows HDR switching is not requested.":L"Auto: adapt HDR to an active HDR display; otherwise tone map to SDR.");for(int id:{410,411,412,413,414,415})EnableWindow(GetDlgItem(m_Dlg,id),IsDlgButtonChecked(m_Dlg,400)==BST_CHECKED);float strength=0;EnableWindow(GetDlgItem(m_Dlg,421),Number(420,strength)&&strength>0);
        const auto tone=SendDlgItemMessageW(m_Dlg,434,CB_GETCURSEL,0,0);const bool adaptive=tone==0||tone==1||tone==7||tone==8;
        for(int id:{440,442,443})EnableWindow(GetDlgItem(m_Dlg,id),adaptive);EnableWindow(GetDlgItem(m_Dlg,441),tone==0||tone==1);
        const auto gamut=SendDlgItemMessageW(m_Dlg,430,CB_GETCURSEL,0,0);EnableWindow(GetDlgItem(m_Dlg,450),gamut==0||gamut==1);EnableWindow(GetDlgItem(m_Dlg,451),gamut==0||gamut==1||gamut==3);return;}if(color){for(int id:{310,311,312})EnableWindow(GetDlgItem(m_Dlg,id),IsDlgButtonChecked(m_Dlg,300)==BST_CHECKED);for(int id:{321,322})EnableWindow(GetDlgItem(m_Dlg,id),IsDlgButtonChecked(m_Dlg,320)==BST_CHECKED);return;}if(processing){EnableWindow(GetDlgItem(m_Dlg,510),IsDlgButtonChecked(m_Dlg,500)==BST_CHECKED);EnableWindow(GetDlgItem(m_Dlg,231),SendDlgItemMessageW(m_Dlg,230,CB_GETCURSEL,0,0)!=3);return;}for(auto pair:{std::pair{130,118},std::pair{131,119}}){auto choice=SendDlgItemMessageW(m_Dlg,pair.first,CB_GETCURSEL,0,0);EnableWindow(GetDlgItem(m_Dlg,pair.second),choice>=0&&ScalerHasWidth(static_cast<size_t>(choice)));}EnableWindow(GetDlgItem(m_Dlg,115),IsDlgButtonChecked(m_Dlg,121)==BST_CHECKED);}
HRESULT SettingsPage::OnActivate(){if(!config)return E_UNEXPECTED;HRESULT hr=config->GetSettings(&settings);if(FAILED(hr))return hr;AddNumberButtons();AddHelp();Populate();if(hdrDetails){InitHdrSections();LayoutHdrSections();}if(!titleFont){LOGFONTW font={};HFONT base=reinterpret_cast<HFONT>(SendMessageW(m_Dlg,WM_GETFONT,0,0));if(GetObjectW(base,sizeof(font),&font)){font.lfWeight=FW_SEMIBOLD;font.lfHeight=font.lfHeight*4/3;titleFont=CreateFontIndirectW(&font);}}if(titleFont)SendDlgItemMessageW(m_Dlg,150,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);PlaceboStatus status;if(SUCCEEDED(config->GetStatus(&status))){wchar_t text[180];swprintf_s(text,L"%d x %d | %s | rendered frames: %llu",status.width,status.height,status.hdrSource?L"HDR":L"SDR",status.rendered);SetDlgItemTextW(m_Dlg,140,text);}ShowDiagnostics(false);LiveSettings::Attach(m_Dlg,config,[this](){auto hr=OnApplyChanges();if(SUCCEEDED(hr)&&m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_CLEAN);return hr;});return S_OK;}
bool SettingsPage::Number(int id,float& value){wchar_t text[64];GetDlgItemTextW(m_Dlg,id,text,64);if((debanding||processing)&&id>=210&&id<=213&&!_wcsicmp(text,L"Default")){value=-1;return true;}if((debanding||processing)&&id>=210&&id<=213&&debandDefaults[id-210]){wchar_t inherited[64];swprintf_s(inherited,L"%.6g",double(DebandDefault(id)));if(!wcscmp(text,inherited)){value=-1;return true;}}for(auto& ch:text)if(ch==L',')ch=L'.';wchar_t* end=nullptr;value=wcstof(text,&end);while(end&&iswspace(*end))++end;return end&&end!=text&&!*end&&std::isfinite(value);}
HRESULT SettingsPage::OnApplyChanges(){
    // Merge only this page's fields into the latest configuration. Otherwise
    // applying the second page would undo the first page's just-applied values.
    PlaceboSettings next;HRESULT current=config->GetSettings(&next);if(FAILED(current))return current;
    if(adjustments){
        float hue=0,kelvin=0;if(!Number(110,next.saturation)||!Number(111,next.contrast)||!Number(112,next.brightness)||!Number(113,next.gamma)||!Number(730,hue)||!Number(731,kelvin)||hue<-180||hue>180||kelvin<2500||kelvin>25000)return E_INVALIDARG;
        next.saturation+=1;next.contrast+=1;next.gamma+=1;next.hue=hue*3.14159265358979323846f/180;next.temperature=(kelvin-6500)/3500;next.colorAdjustment=IsDlgButtonChecked(m_Dlg,720)==BST_CHECKED;
        HRESULT hr=config->SetSettings(&next);if(SUCCEEDED(hr)){settings=next;UpdateControls();m_bDirty=FALSE;}return hr;
    }
    if(debanding){
        if(!ReadDebanding(next))return E_INVALIDARG;
        HRESULT hr=config->SetSettings(&next);if(SUCCEEDED(hr)){settings=next;UpdateControls();m_bDirty=FALSE;}return hr;
    }
    if(effects){next.adaptiveSharpen=IsDlgButtonChecked(m_Dlg,500)==BST_CHECKED;if(!Number(510,next.sharpenStrength))return E_INVALIDARG;HRESULT hr=config->SetSettings(&next);if(SUCCEEDED(hr)){settings=next;if(auto toneControl=GetDlgItem(m_Dlg,hdrDetails?434:134))SendMessageW(toneControl,CB_SETCURSEL,settings.toneMapping,0);UpdateControls();m_bDirty=FALSE;}return hr;}
    if(hdrDetails){
        if(!Number(114,next.sdrNits)||!Number(115,next.hdrNits))return E_INVALIDARG;const auto mode=SendDlgItemMessageW(m_Dlg,121,CB_GETCURSEL,0,0);if(mode<0||mode>2)return E_INVALIDARG;next.hdrMode=static_cast<int>(mode);next.hdrPassthrough=mode!=2;next.autoHdr=IsDlgButtonChecked(m_Dlg,122)==BST_CHECKED;next.allowDelayedPeak=IsDlgButtonChecked(m_Dlg,415)==BST_CHECKED;
        if(!Number(414,next.blackCutoff))return E_INVALIDARG;
        next.peakDetect=IsDlgButtonChecked(m_Dlg,400)==BST_CHECKED;
        if(!Number(410,next.peakSmoothing)||!Number(411,next.sceneLow)||!Number(412,next.sceneHigh)||!Number(413,next.peakPercentile))return E_INVALIDARG;
        HRESULT hr=config->SetSettings(&next);if(SUCCEEDED(hr)){settings=next;if(auto toneControl=GetDlgItem(m_Dlg,hdrDetails?434:134))SendMessageW(toneControl,CB_SETCURSEL,settings.toneMapping,0);UpdateControls();m_bDirty=FALSE;}return hr;
    }
    if(color){
        const auto lutType=SendDlgItemMessageW(m_Dlg,318,CB_GETCURSEL,0,0);if(lutType<0||lutType>4)return E_INVALIDARG;next.extras.values[14]=static_cast<float>(lutType-1);
        next.iccEnabled=IsDlgButtonChecked(m_Dlg,300)==BST_CHECKED;next.lutEnabled=IsDlgButtonChecked(m_Dlg,320)==BST_CHECKED;
        GetDlgItemTextW(m_Dlg,310,next.iccPath,1024);GetDlgItemTextW(m_Dlg,321,next.lutPath,1024);
        next.iccIntent=static_cast<int>(SendDlgItemMessageW(m_Dlg,312,CB_GETCURSEL,0,0));
        HRESULT hr=config->SetSettings(&next);if(SUCCEEDED(hr)){settings=next;if(auto toneControl=GetDlgItem(m_Dlg,hdrDetails?434:134))SendMessageW(toneControl,CB_SETCURSEL,settings.toneMapping,0);UpdateControls();m_bDirty=FALSE;}else LiveSettings::Message(m_Dlg,L"Could not load the color files. Select a valid RGB monitor profile (.icc/.icm) and a .cube 3D LUT (2–65 points per axis). Previous settings are retained.",L"Check Color Files",MB_OK|MB_ICONERROR);return hr;
    }
    if(processing){
        for(int i=0;i<14;i++)next.extras.values[i]=outputDraft.values[i];
        if(!ReadDebanding(next)||!ReadDeinterlacing(next))return E_INVALIDARG;
        next.adaptiveSharpen=IsDlgButtonChecked(m_Dlg,500)==BST_CHECKED;if(!Number(510,next.sharpenStrength))return E_INVALIDARG;
        next.ditherTemporal=IsDlgButtonChecked(m_Dlg,231)==BST_CHECKED;
        auto method=SendDlgItemMessageW(m_Dlg,230,CB_GETCURSEL,0,0);if(method<0||method>=static_cast<LRESULT>(std::size(DitherChoices)))return E_INVALIDARG;next.ditherMethod=static_cast<int>(method);
        const auto frames=SendDlgItemMessageW(m_Dlg,239,CB_GETCURSEL,0,0);if(frames<0||frames>16)return E_INVALIDARG;next.maximumFramesInFlight=static_cast<int>(frames);
        auto output=SendDlgItemMessageW(m_Dlg,236,CB_GETCURSEL,0,0);if(output<0||output>=static_cast<LRESULT>(std::size(OutputChoices)))return E_INVALIDARG;next.outputFormat=static_cast<int>(output);
        auto levels=SendDlgItemMessageW(m_Dlg,237,CB_GETCURSEL,0,0),depth=SendDlgItemMessageW(m_Dlg,238,CB_GETCURSEL,0,0);if(levels<0||levels>=static_cast<LRESULT>(std::size(LevelChoices))||depth<0||depth>=static_cast<LRESULT>(std::size(DisplayDepths)))return E_INVALIDARG;next.outputLevels=static_cast<int>(levels);next.displayDepth=DisplayDepths[depth];
        HRESULT hr=config->SetSettings(&next);if(SUCCEEDED(hr)){settings=next;if(auto toneControl=GetDlgItem(m_Dlg,hdrDetails?434:134))SendMessageW(toneControl,CB_SETCURSEL,settings.toneMapping,0);UpdateControls();m_bDirty=FALSE;}return hr;
    }
    if(!ReadDeinterlacing(next))return E_INVALIDARG;
    HRESULT hr=config->SetSettings(&next);if(SUCCEEDED(hr)){settings=next;if(auto toneControl=GetDlgItem(m_Dlg,hdrDetails?434:134))SendMessageW(toneControl,CB_SETCURSEL,settings.toneMapping,0);UpdateControls();m_bDirty=FALSE;}return hr;
}
INT_PTR SettingsPage::OnReceiveMessage(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    if(hdrDetails&&msg==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&LOWORD(w)>=801&&LOWORD(w)<=803){const int section=LOWORD(w)-800;hdrSection=hdrSection==section?-1:section;LayoutHdrSections();return TRUE;}
    if(hdrDetails&&msg==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&(LOWORD(w)==416||LOWORD(w)==417)){
        const auto& d=LOWORD(w)==417?pl_peak_detect_high_quality_params:pl_peak_detect_default_params;wchar_t text[64];
        for(auto v:{std::pair{410,d.smoothing_period},std::pair{411,d.scene_threshold_low},std::pair{412,d.scene_threshold_high},std::pair{413,d.percentile},std::pair{414,d.black_cutoff}}){swprintf_s(text,L"%.6g",double(v.second));SetDlgItemTextW(m_Dlg,v.first,text);}CheckDlgButton(m_Dlg,415,d.allow_delayed?BST_CHECKED:BST_UNCHECKED);UpdateControls();m_bDirty=TRUE;if(m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);return TRUE;
    }
    if((debanding||processing)&&msg==WM_COMMAND&&LOWORD(w)==700&&HIWORD(w)==BN_CLICKED){for(int id:{210,211,212,213}){debandDefaults[id-210]=true;wchar_t text[64];swprintf_s(text,L"%.6g",double(DebandDefault(id)));SetDlgItemTextW(m_Dlg,id,text);}UpdateControls();m_bDirty=TRUE;if(m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);return TRUE;}
    if(msg==WM_COMMAND&&LOWORD(w)==123&&HIWORD(w)==BN_CLICKED){for(int id:{110,111,112,113,730})SetDlgItemTextW(m_Dlg,id,L"0");SetDlgItemTextW(m_Dlg,731,L"6500");return TRUE;}
    if(msg==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&HandleNumberButton(LOWORD(w)))return TRUE;
    if(processing&&msg==WM_COMMAND&&LOWORD(w)==241&&HIWORD(w)==BN_CLICKED){const auto before=outputDraft;auto preview=[this](const RendererExtras& values){outputDraft=values;PlaceboSettings next;auto hr=config->GetSettings(&next);if(FAILED(hr))return hr;for(int i=0;i<14;i++)next.extras.values[i]=values.values[i];return config->SetSettings(&next);};if(EditOutputOptions(g_hInst,m_Dlg,outputDraft,preview)){m_bDirty=TRUE;if(m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);}else if(LiveSettings::Enabled())preview(before);return TRUE;}
    if(color&&msg==WM_COMMAND&&HIWORD(w)==BN_CLICKED&&(LOWORD(w)==311||LOWORD(w)==322)){BrowseColor(LOWORD(w)==311);return TRUE;}
    if(msg==WM_COMMAND&&LOWORD(w)==141&&HIWORD(w)==BN_CLICKED){ShowDiagnostics(true);return TRUE;}
    if(msg==WM_CTLCOLORDLG)return reinterpret_cast<INT_PTR>(background);
    if(msg==WM_CTLCOLORSTATIC){auto dc=reinterpret_cast<HDC>(w);SetBkColor(dc,RGB(248,250,252));SetTextColor(dc,GetDlgCtrlID(reinterpret_cast<HWND>(l))==150?RGB(33,73,108):RGB(40,49,60));return reinterpret_cast<INT_PTR>(background);}
    if(msg==WM_COMMAND&&!loading&&(HIWORD(w)==EN_CHANGE||HIWORD(w)==BN_CLICKED||HIWORD(w)==CBN_SELCHANGE)){UpdateControls();m_bDirty=TRUE;if(m_pPageSite)m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);}
    return CBasePropertyPage::OnReceiveMessage(hwnd,msg,w,l);
}

void SettingsPage::BrowseColor(bool icc){
    wchar_t path[1024]={};GetDlgItemTextW(m_Dlg,icc?310:321,path,1024);
    OPENFILENAMEW file={};file.lStructSize=sizeof(file);file.hwndOwner=m_Dlg;file.lpstrFile=path;file.nMaxFile=1024;
    file.lpstrFilter=icc?L"ICC Monitor Profiles (*.icc;*.icm)\0*.icc;*.icm\0All Files\0*.*\0":L"3D-LUT (*.cube)\0*.cube\0All Files\0*.*\0";
    file.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    if(GetOpenFileNameW(&file))SetDlgItemTextW(m_Dlg,icc?310:321,path);
}


void SettingsPage::InitHdrSections(){
    hdrPositions.clear();
    auto section=[](int id){if((id>=410&&id<=417)||(id>=831&&id<=839))return 1;if((id>=440&&id<=443)||id==420||id==421||(id>=841&&id<=849))return 2;if(id==450||id==451||(id>=851&&id<=859))return 3;return -1;};
    for(HWND child=GetWindow(m_Dlg,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        int id=GetDlgCtrlID(child),group=section(id);if(id>=10000)group=section((id-10000)/2);if(group<0)continue;
        RECT rect;GetWindowRect(child,&rect);MapWindowPoints(nullptr,m_Dlg,reinterpret_cast<POINT*>(&rect),2);hdrPositions.push_back({child,group,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top});
    }
}
void SettingsPage::LayoutHdrSections(){
    const wchar_t* labels[]={L"",L"Peak Detection - Advanced",L"Tone Mapping - Advanced",L"Gamut Mapping - Advanced"};
    const int heights[]={0,90,90,56};int top=156;
    for(int section=1;section<=1;section++){
        RECT header={8,top,488,top+16};MapDialogRect(m_Dlg,&header);SetWindowPos(GetDlgItem(m_Dlg,800+section),nullptr,header.left,header.top,header.right-header.left,header.bottom-header.top,SWP_NOZORDER|SWP_NOACTIVATE);
        std::wstring label=hdrSection==section?L"▾  ":L"▸  ";label+=labels[section];SetDlgItemTextW(m_Dlg,800+section,label.c_str());
        RECT offset={0,top+18,0,0};MapDialogRect(m_Dlg,&offset);
        for(const auto& item:hdrPositions)if(item.section==section){SetWindowPos(item.window,nullptr,item.x,item.y+offset.top,item.w,item.h,SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(item.window,hdrSection==section?SW_SHOW:SW_HIDE);}
        top+=18;if(hdrSection==section)top+=heights[section];
    }
    RedrawWindow(m_Dlg,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
