#include "Diagnostics.h"
#define NOMINMAX
#include <windows.h>
#include "../MPCVideoRenderer/external/BaseClasses/streams.h"
#include <dvdmedia.h>
#include <wrl/client.h>
#include "Settings.h"
#include "NumberControls.h"
#include "SettingsChoices.h"
#include "UiHelp.h"
#include "StatisticsKey.h"
#include <commctrl.h>
#include "../MPCVideoRenderer/Include/FilterInterfaces.h"
#include "../MPCVideoRenderer/Include/ISubRender11.h"
#include <atomic>
#include <thread>
#include <d3d11.h>
#include "../MPCVideoRenderer/Include/ID3DVideoMemoryConfiguration.h"
#include <initguid.h>
#include "../MPCVideoRenderer/Include/IMediaSideData.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>
using Microsoft::WRL::ComPtr;
HINSTANCE g_hInst=nullptr;
static void Check(HRESULT hr,const char* text){if(FAILED(hr)){printf("FAIL %s %08lX\n",text,static_cast<unsigned long>(hr));exit(1);}}
static bool hardware=false;
static bool measureTest=false;
static bool startupTest=false;
static bool interlacedTest=false,bottomFirst=false,weaveTest=false,eosTest=false,yadifTest=false,forceBffTest=false,motionTest=false,sharpenTest=false;
static bool flushRaceTest=false;
static std::atomic<bool> blockSampleTime=false;
static HANDLE sampleTimeEntered=nullptr,sampleTimeRelease=nullptr;
static DXGI_FORMAT textureFormat=DXGI_FORMAT_P010;
static UINT textureBind=D3D11_BIND_DECODER;
static std::atomic<HRESULT> sourceFailure=S_OK;
static DWORD FourCC(){return textureFormat==DXGI_FORMAT_NV12?MAKEFOURCC('N','V','1','2'):textureFormat==DXGI_FORMAT_P016?MAKEFOURCC('P','0','1','6'):MAKEFOURCC('P','0','1','0');}
static long SampleBytes(){return 64*64*3/(textureFormat==DXGI_FORMAT_NV12?2:1);}
class TextureSample final:public IMediaSample,public IMediaSampleD3D11 {
    std::atomic<ULONG> refs=1;ComPtr<IMediaSample> sample;ComPtr<ID3D11Texture2D> texture;
public:
    TextureSample(IMediaSample* source,ID3D11Texture2D* tex):sample(source),texture(tex){}
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_IMediaSample)*out=static_cast<IMediaSample*>(this);else if(id==__uuidof(IMediaSampleD3D11))*out=static_cast<IMediaSampleD3D11*>(this);else if(id==__uuidof(IMediaSideData)||id==IID_IMediaSample2)return sample->QueryInterface(id,out);else return E_NOINTERFACE;AddRef();return S_OK;}
    STDMETHODIMP_(ULONG) AddRef()override{return ++refs;}
    STDMETHODIMP_(ULONG) Release()override{ULONG n=--refs;if(!n)delete this;return n;}
    STDMETHODIMP GetD3D11Texture(int view,ID3D11Texture2D** out,UINT* slice)override{if(!out||!slice)return E_POINTER;if(view)return E_INVALIDARG;*out=texture.Get();(*out)->AddRef();*slice=1;return S_OK;}
    STDMETHODIMP GetPointer(BYTE** p)override{return sample->GetPointer(p);}
    STDMETHODIMP_(long) GetSize()override{return sample->GetSize();}
    STDMETHODIMP GetTime(REFERENCE_TIME* a,REFERENCE_TIME* b)override{
        if(blockSampleTime.exchange(false)){SetEvent(sampleTimeEntered);if(WaitForSingleObject(sampleTimeRelease,5000)!=WAIT_OBJECT_0)return E_FAIL;}
        return sample->GetTime(a,b);
    }
    STDMETHODIMP SetTime(REFERENCE_TIME* a,REFERENCE_TIME* b)override{return sample->SetTime(a,b);}
    STDMETHODIMP IsSyncPoint()override{return sample->IsSyncPoint();}
    STDMETHODIMP SetSyncPoint(BOOL b)override{return sample->SetSyncPoint(b);}
    STDMETHODIMP IsPreroll()override{return sample->IsPreroll();}
    STDMETHODIMP SetPreroll(BOOL b)override{return sample->SetPreroll(b);}
    STDMETHODIMP_(long) GetActualDataLength()override{return sample->GetActualDataLength();}
    STDMETHODIMP SetActualDataLength(long n)override{return sample->SetActualDataLength(n);}
    STDMETHODIMP GetMediaType(AM_MEDIA_TYPE** mt)override{return sample->GetMediaType(mt);}
    STDMETHODIMP SetMediaType(AM_MEDIA_TYPE* mt)override{return sample->SetMediaType(mt);}
    STDMETHODIMP IsDiscontinuity()override{return sample->IsDiscontinuity();}
    STDMETHODIMP SetDiscontinuity(BOOL b)override{return sample->SetDiscontinuity(b);}
    STDMETHODIMP GetMediaTime(LONGLONG* a,LONGLONG* b)override{return sample->GetMediaTime(a,b);}
    STDMETHODIMP SetMediaTime(LONGLONG* a,LONGLONG* b)override{return sample->SetMediaTime(a,b);}
};
class SubtitleProbe final:public ISubRender11Callback {
    std::atomic<ULONG> refs=1;ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> vertices;ComPtr<ID3D11ShaderResourceView> bitmap;ComPtr<ID3D11SamplerState> sampler;
public:
    unsigned attached=0,detached=0,drawn=0;
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=__uuidof(ISubRender11Callback))return E_NOINTERFACE;*out=static_cast<ISubRender11Callback*>(this);AddRef();return S_OK;}
    STDMETHODIMP_(ULONG) AddRef()override{return ++refs;}
    STDMETHODIMP_(ULONG) Release()override{ULONG n=--refs;if(!n)delete this;return n;}
    STDMETHODIMP SetDevice11(ID3D11Device* next)override{
        vertices.Reset();bitmap.Reset();sampler.Reset();device=next;if(!next){detached++;return S_OK;}attached++;
        const float quad[]={-1,1,0,0,0, 1,1,0,1,0, -1,-1,0,0,1, 1,-1,0,1,1};
        D3D11_BUFFER_DESC buffer={};buffer.ByteWidth=sizeof(quad);buffer.Usage=D3D11_USAGE_IMMUTABLE;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;D3D11_SUBRESOURCE_DATA initial={};initial.pSysMem=quad;HRESULT hr=device->CreateBuffer(&buffer,&initial,&vertices);if(FAILED(hr))return hr;
        const BYTE green[]={0,255,0,0};D3D11_TEXTURE2D_DESC desc={};desc.Width=desc.Height=desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;initial.pSysMem=green;initial.SysMemPitch=4;ComPtr<ID3D11Texture2D> texture;hr=device->CreateTexture2D(&desc,&initial,&texture);if(FAILED(hr))return hr;hr=device->CreateShaderResourceView(texture.Get(),nullptr,&bitmap);if(FAILED(hr))return hr;
        D3D11_SAMPLER_DESC sample={};sample.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sample.AddressU=sample.AddressV=sample.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sample.MaxLOD=D3D11_FLOAT32_MAX;return device->CreateSamplerState(&sample,&sampler);
    }
    STDMETHODIMP Render11(REFERENCE_TIME,REFERENCE_TIME,REFERENCE_TIME,RECT,RECT,RECT,double,int,DWORD)override{
        if(!device)return E_UNEXPECTED;ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);ComPtr<ID3D11RenderTargetView> view;context->OMGetRenderTargets(1,&view,nullptr);if(!view)return E_FAIL;
        // Like the host callback: provide bitmap/vertices, use renderer shaders.
        auto resource=bitmap.Get();auto sampling=sampler.Get();auto buffer=vertices.Get();UINT stride=5*sizeof(float),offset=0;
        context->PSSetShaderResources(0,1,&resource);context->PSSetSamplers(0,1,&sampling);context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->Draw(4,0);drawn++;return S_OK;
    }
};
class PageSite final:public IPropertyPageSite {
    std::atomic<ULONG> refs=1;
public:
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IPropertyPageSite)return E_NOINTERFACE;*out=static_cast<IPropertyPageSite*>(this);AddRef();return S_OK;}
    STDMETHODIMP_(ULONG) AddRef()override{return ++refs;}
    STDMETHODIMP_(ULONG) Release()override{ULONG n=--refs;if(!n)delete this;return n;}
    STDMETHODIMP OnStatusChange(DWORD)override{return S_OK;}
    STDMETHODIMP GetLocaleID(LCID* out)override{if(!out)return E_POINTER;*out=0x407;return S_OK;}
    STDMETHODIMP GetPageContainer(IUnknown** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    STDMETHODIMP TranslateAccelerator(MSG*)override{return S_FALSE;}
};
static void NumberClick(HWND dialog,int field,bool increase){
    const int id=NumberButtonId(field,increase);HWND button=GetDlgItem(dialog,id);if(!button)exit(66);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(button));
}
static float NumberText(HWND dialog,int field){wchar_t text[64];GetDlgItemTextW(dialog,field,text,64);for(auto& ch:text)if(ch==L',')ch=L'.';return wcstof(text,nullptr);}
static void CheckNumberButtons(HWND dialog){
    for(const auto& control:NumberControls)if(HWND field=GetDlgItem(dialog,control.id)){
        RECT edit={};GetWindowRect(field,&edit);if(edit.right-edit.left<35)exit(67);
        for(bool increase:{false,true}){HWND button=GetDlgItem(dialog,NumberButtonId(control.id,increase));RECT arrow={};if(!button)exit(68);GetWindowRect(button,&arrow);
            if(arrow.left<edit.right||!!IsWindowEnabled(button)!=!!IsWindowEnabled(field))exit(69);
        }
    }
}
static HWND HelpWindow(HWND dialog){
    struct Search {HWND owner,tip;};Search search={GetAncestor(dialog,GA_ROOT),nullptr};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM param)->BOOL{auto& search=*reinterpret_cast<Search*>(param);wchar_t name[64];GetClassNameW(window,name,64);if(GetWindow(window,GW_OWNER)==search.owner&&!wcscmp(name,TOOLTIPS_CLASSW)){search.tip=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.tip;
}
static void CheckHelp(HWND dialog){
    HWND tip=HelpWindow(dialog);if(!tip){puts("FAIL help tooltip window missing");exit(88);}int count=0;
    for(const auto& help:UiHelp)for(int id:{help.id,NumberButtonId(help.id,false),NumberButtonId(help.id,true)})if(HWND control=GetDlgItem(dialog,id)){
        wchar_t text[1024]={};TOOLINFOW tool={};tool.cbSize=TTTOOLINFOW_V2_SIZE;tool.uFlags=TTF_IDISHWND;tool.hwnd=dialog;tool.uId=reinterpret_cast<UINT_PTR>(control);tool.lpszText=text;
        if(!SendMessageW(tip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&tool))||!tool.lpszText||wcscmp(tool.lpszText,help.text)){printf("FAIL help text for control %d count=%lld text=%p expected=%p\n",id,static_cast<long long>(SendMessageW(tip,TTM_GETTOOLCOUNT,0,0)),tool.lpszText,help.text);exit(89);}count++;
    }
    if(SendMessageW(tip,TTM_GETTOOLCOUNT,0,0)!=count)exit(90);
}
static void SaveHdrPreview(HWND,const char*);
static void TestPage(IClassFactory* factory,IBaseFilter* renderer,IPlaceboSettings* config){
    ComPtr<IPropertyPage> page;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&page)),"settings page factory");ComPtr<PageSite> site;site.Attach(new PageSite);Check(page->SetPageSite(site.Get()),"page site");IUnknown* object=renderer;Check(page->SetObjects(1,&object),"page object");
    PROPPAGEINFO info={};info.cb=sizeof(info);Check(page->GetPageInfo(&info),"page metadata");CoTaskMemFree(info.pszTitle);CoTaskMemFree(info.pszDocString);CoTaskMemFree(info.pszHelpFile);
    HWND parent=CreateWindowExW(0,L"STATIC",L"Internal settings test",WS_POPUP,0,0,1100,800,nullptr,nullptr,g_hInst,nullptr);if(!parent)exit(9);RECT bounds={0,0,1100,800};Check(page->Activate(parent,&bounds,FALSE),"page activation");HWND dialog=GetWindow(parent,GW_CHILD);
    for(int id:{110,111,112,113,720,730,731,735})if(!GetDlgItem(dialog,id)){printf("FAIL missing settings control %d\n",id);exit(10);}
    ShowWindow(parent,SW_SHOWNOACTIVATE);Check(page->Show(SW_SHOW),"adjustment preview show");UpdateWindow(dialog);SaveHdrPreview(dialog,"PlaceboRenderer/build/color-adjustment-preview.bmp");ShowWindow(parent,SW_HIDE);
    PlaceboSettings before,after;Check(config->GetSettings(&before),"settings snapshot");CheckNumberButtons(dialog);if(std::abs(NumberText(dialog,110)-(before.saturation-1))>.00001f||std::abs(NumberText(dialog,111)-(before.contrast-1))>.00001f||std::abs(NumberText(dialog,113)-(before.gamma-1))>.00001f)exit(86);
    if(std::abs(StepNumber(NumberControls[0],-.5,true)+.495)>.00001)exit(87);
    SetDlgItemTextW(dialog,110,L"0");for(int i=0;i<3;i++)NumberClick(dialog,110,true);if(std::abs(NumberText(dialog,110)-.015f)>.00001f)exit(70);
    Check(page->Apply(),"fractional arrow steps apply");Check(config->GetSettings(&after),"arrow settings readback");if(after.saturation!=1.015f)exit(71);
    SetDlgItemTextW(dialog,110,L"99");NumberClick(dialog,110,true);if(NumberText(dialog,110)!=99)exit(72);
    SetDlgItemTextW(dialog,112,L"0");NumberClick(dialog,112,false);if(std::abs(NumberText(dialog,112)+.001f)>.00001f)exit(73);SetDlgItemTextW(dialog,112,L"0");
    SetDlgItemTextW(dialog,110,L"-0,25");Check(page->Apply(),"German decimal apply");Check(config->GetSettings(&after),"page changed settings");if(after.saturation!=.75f){printf("FAIL page change did not reach renderer\n");exit(11);}
    SetDlgItemTextW(dialog,110,L"-0,5");SetDlgItemTextW(dialog,111,L"0,2");SetDlgItemTextW(dialog,112,L"0,1");SetDlgItemTextW(dialog,113,L"0,3");Check(page->Apply(),"all correction values apply");
    Check(config->GetSettings(&after),"correction readback");if(after.saturation!=.5f||after.contrast!=1.2f||after.brightness!=.1f||after.gamma!=1.3f)exit(82);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(123,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(dialog,123)));
    for(int id:{110,111,112,113})if(NumberText(dialog,id)!=0)exit(83);
    Check(config->GetSettings(&after),"unapplied reset readback");if(after.saturation!=.5f||after.gamma!=1.3f)exit(84);
    Check(page->Apply(),"neutral reset apply");Check(config->GetSettings(&after),"neutral readback");if(after.saturation!=1||after.contrast!=1||after.brightness!=0||after.gamma!=1)exit(85);
    SetDlgItemTextW(dialog,730,L"90");SetDlgItemTextW(dialog,731,L"3000");Check(page->Apply(),"hue and Kelvin apply");Check(config->GetSettings(&after),"hue Kelvin readback");if(std::abs(after.hue-1.57079633f)>.00001f||after.temperature!=-1)exit(121);
    for(auto pair:{std::pair{730,L"181"},std::pair{731,L"2499"}}){SetDlgItemTextW(dialog,pair.first,pair.second);if(SUCCEEDED(page->Apply()))exit(122);SetDlgItemTextW(dialog,pair.first,pair.first==730?L"0":L"6500");}
    CheckDlgButton(dialog,720,BST_UNCHECKED);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(720,BN_CLICKED),0);Check(page->Apply(),"color bypass apply");Check(config->GetSettings(&after),"bypass readback");if(after.colorAdjustment||!IsWindowEnabled(GetDlgItem(dialog,730)))exit(123);
    after.hdrNits=555;Check(config->SetSettings(&after),"HDR edit while color page open");SetDlgItemTextW(dialog,112,L"0.1");Check(page->Apply(),"color page merges HDR edit");Check(config->GetSettings(&after),"merged HDR readback");if(after.hdrNits!=555)exit(124);
    SetDlgItemTextW(dialog,110,L"invalid");if(SUCCEEDED(page->Apply())){printf("FAIL invalid setting accepted\n");exit(12);}Check(config->SetSettings(&before),"restore settings");
    HWND oldTip=HelpWindow(dialog);Check(page->Deactivate(),"page deactivation");if(IsWindow(oldTip))exit(91);Check(page->SetObjects(0,nullptr),"page disconnect");Check(page->SetPageSite(nullptr),"page detach site");DestroyWindow(parent);printf("PASS settings page: neutral zero display, correction conversion, unapplied/applied reset, contextual help lifetime, arrows, scalers and validation\n");
}
static void SaveHdrPreview(HWND dialog,const char* filename="PlaceboRenderer/build/hdr-menu-preview.bmp"){
    RECT r={};GetClientRect(dialog,&r);const int w=r.right,h=r.bottom;
    HDC screen=GetDC(dialog),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);auto old=SelectObject(dc,bitmap);
    HBRUSH brush=CreateSolidBrush(RGB(248,250,252));RECT area={0,0,w,h};FillRect(dc,&area,brush);DeleteObject(brush);
    // WM_PRINT with PRF_CHILDREN can paint collapsed controls as well. Render
    // only visible direct children at their actual coordinates for this preview.
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)){
        RECT rect;GetWindowRect(child,&rect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&rect),2);const int saved=SaveDC(dc);SetViewportOrgEx(dc,rect.left,rect.top,nullptr);IntersectClipRect(dc,0,0,rect.right-rect.left,rect.bottom-rect.top);SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_ERASEBKGND|PRF_CHECKVISIBLE|PRF_CHILDREN);RestoreDC(dc,saved);
    }
    BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    std::vector<BYTE> pixels(static_cast<size_t>(w)*h*4);SelectObject(dc,old);
    if(!GetDIBits(screen,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS))exit(63);
    BITMAPFILEHEADER header={};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());
    FILE* f=nullptr;fopen_s(&f,filename,"wb");if(!f)exit(64);fwrite(&header,sizeof(header),1,f);fwrite(&info.bmiHeader,sizeof(info.bmiHeader),1,f);fwrite(pixels.data(),pixels.size(),1,f);fclose(f);
    DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(dialog,screen);
}
static int barsAction=0;static bool barsSeen=false;
static BOOL CALLBACK BarsWindow(HWND window,LPARAM timer){
    wchar_t title[128];GetWindowTextW(window,title,128);if(wcscmp(title,L"Black Bars / Zoom"))return TRUE;
    KillTimer(nullptr,static_cast<UINT_PTR>(timer));barsSeen=true;
    for(int id:{701,702,703,704,705,706,707,708,11410,11411,11412,11413})if(!GetDlgItem(window,id))exit(180);
    if(barsAction==1){SetDlgItemTextW(window,705,L"8");SendMessageW(window,WM_COMMAND,IDCANCEL,0);return FALSE;}
    if(barsAction==2){CheckDlgButton(window,701,BST_CHECKED);CheckDlgButton(window,703,BST_UNCHECKED);CheckDlgButton(window,708,BST_UNCHECKED);SendDlgItemMessageW(window,704,CB_SETCURSEL,1,0);SetDlgItemTextW(window,705,L"1,5");SetDlgItemTextW(window,706,L"3");SendMessageW(window,WM_COMMAND,MAKEWPARAM(11413,BN_CLICKED),0);ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);SaveHdrPreview(window,"PlaceboRenderer/build/black-bars-menu-preview.bmp");}
    if(barsAction==3)SendMessageW(window,WM_COMMAND,MAKEWPARAM(707,BN_CLICKED),0);
    SendMessageW(window,WM_COMMAND,IDOK,0);return FALSE;
}
static void CALLBACK BarsTimer(HWND,UINT,UINT_PTR timer,DWORD){EnumThreadWindows(GetCurrentThreadId(),BarsWindow,static_cast<LPARAM>(timer));}
static void TestBarsDialog(HWND dialog,IPropertyPage* page,IPlaceboSettings* config){
    PlaceboSettings before;Check(config->GetSettings(&before),"bars snapshot");
    for(int action=1;action<=3;action++){
        barsAction=action;barsSeen=false;UINT_PTR timer=SetTimer(nullptr,0,10,BarsTimer);if(!timer)exit(181);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(608,BN_CLICKED),0);KillTimer(nullptr,timer);if(!barsSeen)exit(182);
        PlaceboSettings value;Check(config->GetSettings(&value),"bars draft readback");if(memcmp(&value.blackBars,&before.blackBars,sizeof(BlackBarSettings)))exit(183);
        if(action==1){if(page->IsPageDirty()!=S_FALSE)exit(184);continue;}
        value.brightness=.17f;Check(config->SetSettings(&value),"concurrent page edit");Check(page->Apply(),"bars Apply");Check(config->GetSettings(&value),"bars applied readback");
        if(value.brightness!=.17f)exit(185);
        if(action==2){if(!value.blackBars.enabled||!value.blackBars.crop||value.blackBars.keepSubtitles||value.blackBars.stableZoom||value.blackBars.zoom!=1||value.blackBars.threshold!=1.5f||fabs(value.blackBars.delay-3.1f)>.001f)exit(186);}
        else{BlackBarSettings defaults;if(memcmp(&value.blackBars,&defaults,sizeof(defaults)))exit(187);}
        before=value;
    }
    puts("PASS Black Bars dialog: controls, Cancel, decimal comma, arrows, draft OK, deferred Apply, Reset and cross-page merge");
}
static void TestScaling(IClassFactory* factory,IBaseFilter* renderer,IPlaceboSettings* config){
    PlaceboSettings before;Check(config->GetSettings(&before),"scaling snapshot");
    ComPtr<IPropertyPage> page;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&page)),"scaling factory");ComPtr<PageSite> site;site.Attach(new PageSite);Check(page->SetPageSite(site.Get()),"scaling site");IUnknown* object=renderer;Check(page->SetObjects(1,&object),"scaling object");
    HWND parent=CreateWindowExW(0,L"STATIC",L"Scaling Test",WS_POPUP,-10000,-10000,1100,850,nullptr,nullptr,g_hInst,nullptr);RECT bounds={0,0,1100,850};Check(page->Activate(parent,&bounds,FALSE),"scaling activation");HWND dialog=GetWindow(parent,GW_CHILD);
    for(int id:{600,601,602,603,604,605,610,611,612,613,620,621,622,623,624,625,626,627,628,630,640,240})if(!GetDlgItem(dialog,id))exit(101);
    ShowWindow(parent,SW_SHOWNOACTIVATE);Check(page->Show(SW_SHOW),"scaling preview show");UpdateWindow(dialog);SaveHdrPreview(dialog,"PlaceboRenderer/build/scaling-menu-preview.bmp");ShowWindow(parent,SW_HIDE);
    TestBarsDialog(dialog,page.Get(),config);
    auto select=[&](int id,int at){SendDlgItemMessageW(dialog,id,CB_SETCURSEL,at,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialog,id)));};
    for(int role=0;role<5;role++){
        const auto count=SendDlgItemMessageW(dialog,600+role,CB_GETCOUNT,0,0);
        if(count<6)exit(102);
        for(int at=0;at<count;at++){select(600+role,at);Check(page->Apply(),"scaling filter apply");}
        select(600+role,0);Check(page->Apply(),"auto filter restore");
    }
    // Customizing a preset copies its base and remains a draft until Apply.
    const auto lanczos=SendDlgItemMessageW(dialog,600,CB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(L"lanczos"));if(lanczos==CB_ERR)exit(190);
    select(600,static_cast<int>(lanczos));Check(page->Apply(),"preset before Customize");
    if(!IsWindowEnabled(GetDlgItem(dialog,641))||IsWindowEnabled(GetDlgItem(dialog,622)))exit(191);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(641,BN_CLICKED),0);
    PlaceboSettings customized;Check(config->GetSettings(&customized),"Customize remains draft");if(strcmp(customized.scaling.filters[0].filter,"lanczos"))exit(192);
    if(!IsWindowEnabled(GetDlgItem(dialog,622))||IsWindowEnabled(GetDlgItem(dialog,641)))exit(193);
    Check(page->Apply(),"Customize apply");Check(config->GetSettings(&customized),"Customize readback");
    if(strcmp(customized.scaling.filters[0].filter,"custom")||strcmp(customized.scaling.filters[0].preset,"lanczos")||customized.scaling.filters[0].overrides)exit(194);
    ShowWindow(parent,SW_SHOWNOACTIVATE);UpdateWindow(dialog);SaveHdrPreview(dialog,"PlaceboRenderer/build/scaling-customize-preview.bmp");ShowWindow(parent,SW_HIDE);
    select(600,2);SetDlgItemTextW(dialog,621,L"0.25");SetDlgItemTextW(dialog,622,L"0.8");Check(page->Apply(),"custom parameter apply");
    PlaceboSettings value;Check(config->GetSettings(&value),"custom readback");if(strcmp(value.scaling.filters[0].filter,"custom")||value.scaling.filters[0].values[1]!=.25f||value.scaling.filters[0].values[2]!=.8f)exit(103);
    SetDlgItemTextW(dialog,621,L"Auto");Check(page->Apply(),"custom auto restore");Check(config->GetSettings(&value),"auto readback");if(value.scaling.filters[0].overrides&2)exit(104);
    CheckDlgButton(dialog,240,BST_CHECKED);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(240,BN_CLICKED),0);Check(page->Apply(),"scaling smooth motion apply");Check(config->GetSettings(&value),"smooth readback");if(!value.smoothMotion)exit(105);
    value.brightness=.12f;Check(config->SetSettings(&value),"other page concurrent edit");SetDlgItemTextW(dialog,605,L"0");Check(page->Apply(),"explicit zero apply");Check(config->GetSettings(&value),"merged readback");if(value.brightness!=.12f||value.scaling.antiringing!=0)exit(106);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(640,BN_CLICKED),0);Check(page->Apply(),"scaling reset apply");Check(config->GetSettings(&value),"reset readback");if(value.scaling.antiringing!=-1||!value.smoothMotion)exit(107);for(const auto& f:value.scaling.filters)if(strcmp(f.filter,"auto")||f.overrides)exit(108);
    for(int id:{606,607}){SendDlgItemMessageW(dialog,id,CB_SETCURSEL,2,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),0);}Check(page->Apply(),"performance flags On");Check(config->GetSettings(&value),"performance flags readback");if(value.extras.values[15]!=1||value.extras.values[16]!=1)exit(167);
    value.extras.values[0]=.3f;value.extras.values[14]=2;Check(config->SetSettings(&value),"other extras while Scaling open");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(640,BN_CLICKED),0);Check(page->Apply(),"reset Scaling flags");Check(config->GetSettings(&value),"Scaling extras reset readback");if(value.extras.values[15]!=-1||value.extras.values[16]!=-1||value.extras.values[0]!=.3f||value.extras.values[14]!=2)exit(168);
    Check(config->SetSettings(&before),"restore scaling snapshot");Check(page->Deactivate(),"scaling deactivate");Check(page->SetObjects(0,nullptr),"scaling disconnect");Check(page->SetPageSite(nullptr),"scaling detach");DestroyWindow(parent);printf("PASS Scaling page: all filters, Auto/custom, zero overrides, reset, smooth motion, cross-page merge\n");
}
static int outputAction=0;static bool outputSeen=false;
static BOOL CALLBACK OutputWindow(HWND window,LPARAM timer){
    wchar_t title[128];GetWindowTextW(window,title,128);if(wcscmp(title,L"Output Background and Advanced"))return TRUE;
    KillTimer(nullptr,static_cast<UINT_PTR>(timer));outputSeen=true;
    if(outputAction==1){SetDlgItemTextW(window,1100,L"0.8");SendMessageW(window,WM_COMMAND,IDCANCEL,0);return FALSE;}
    if(outputAction==2){SetDlgItemTextW(window,1100,L"0,25");SetDlgItemTextW(window,1113,L"16");SaveHdrPreview(window,"PlaceboRenderer/build/output-background-menu-preview.bmp");}
    if(outputAction==3)SendMessageW(window,WM_COMMAND,MAKEWPARAM(1140,BN_CLICKED),0);
    SendMessageW(window,WM_COMMAND,IDOK,0);return FALSE;
}
static void CALLBACK OutputTimer(HWND,UINT,UINT_PTR timer,DWORD){EnumThreadWindows(GetCurrentThreadId(),OutputWindow,static_cast<LPARAM>(timer));}
static void TestOutputDialog(HWND dialog,IPropertyPage* page,IPlaceboSettings* config){
    PlaceboSettings value;Check(config->GetSettings(&value),"advanced output before");value.extras.values[14]=2;value.extras.values[15]=value.extras.values[16]=1;Check(config->SetSettings(&value),"other pages extras edits");
    for(int action=1;action<=3;action++){
        outputAction=action;outputSeen=false;UINT_PTR timer=SetTimer(nullptr,0,10,OutputTimer);if(!timer)exit(162);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(241,BN_CLICKED),0);KillTimer(nullptr,timer);if(!outputSeen)exit(163);
        Check(config->GetSettings(&value),"advanced output before Apply");if(value.extras.values[0]!=(action==3?.25f:-1.f))exit(164);
        if(action==1){if(page->IsPageDirty()!=S_FALSE)exit(165);continue;}
        Check(page->Apply(),"advanced output Apply");Check(config->GetSettings(&value),"advanced output saved");if(value.extras.values[0]!=(action==2?.25f:-1.f)||value.extras.values[13]!=(action==2?16.f:-1.f)||value.extras.values[14]!=2||value.extras.values[15]!=1||value.extras.values[16]!=1)exit(166);
    }
    puts("PASS Output Background dialog: Cancel, draft OK, decimal values, Reset, deferred Apply and cross-page extras merge");
}
static void TestDebanding(IClassFactory* factory,IBaseFilter* renderer,IPlaceboSettings* config){
    PlaceboSettings before;Check(config->GetSettings(&before),"debanding snapshot");ComPtr<IPropertyPage> page;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&page)),"debanding factory");ComPtr<PageSite> site;site.Attach(new PageSite);Check(page->SetPageSite(site.Get()),"debanding site");IUnknown* object=renderer;Check(page->SetObjects(1,&object),"debanding object");
    HWND parent=CreateWindowExW(0,L"STATIC",L"Debanding Test",WS_POPUP,-10000,-10000,1100,850,nullptr,nullptr,g_hInst,nullptr);RECT bounds={0,0,1100,850};Check(page->Activate(parent,&bounds,FALSE),"debanding activate");HWND dialog=GetWindow(parent,GW_CHILD);CheckNumberButtons(dialog);CheckHelp(dialog);
    for(int id:{210,211,212,213,220,700,711,712,713,714})if(!GetDlgItem(dialog,id))exit(113);
    ShowWindow(parent,SW_SHOWNOACTIVATE);Check(page->Show(SW_SHOW),"debanding preview show");UpdateWindow(dialog);SaveHdrPreview(dialog,"PlaceboRenderer/build/debanding-menu-preview.bmp");ShowWindow(parent,SW_HIDE);
    auto mode=[&](int at){SendDlgItemMessageW(dialog,220,CB_SETCURSEL,at,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(220,CBN_SELCHANGE),0);};
    mode(1);for(int id:{210,211,212,213})if(!IsWindowEnabled(GetDlgItem(dialog,id)))exit(114);
    SetDlgItemTextW(dialog,210,L"0");SetDlgItemTextW(dialog,211,L"4,5");SetDlgItemTextW(dialog,212,L"100");SetDlgItemTextW(dialog,213,L"0");Check(page->Apply(),"debanding zero/manual apply");PlaceboSettings value;Check(config->GetSettings(&value),"debanding readback");if(value.deband!=1||value.debandIterations!=0||value.debandThreshold!=4.5f||value.debandRadius!=100||value.debandGrain!=0)exit(115);
    for(auto pair:{std::pair{210,L"17"},std::pair{210,L"1.5"},std::pair{211,L"1001"},std::pair{212,L"-2"},std::pair{213,L"nan"}}){SetDlgItemTextW(dialog,pair.first,pair.second);if(SUCCEEDED(page->Apply()))exit(116);SetDlgItemTextW(dialog,pair.first,L"Default");}
    NumberClick(dialog,210,true);if(NumberText(dialog,210)!=2)exit(117);
    SetDlgItemTextW(dialog,210,L"16");SetDlgItemTextW(dialog,211,L"1000");SetDlgItemTextW(dialog,212,L"1000");SetDlgItemTextW(dialog,213,L"1000");Check(page->Apply(),"full debanding ranges");
    mode(0);if(!IsWindowEnabled(GetDlgItem(dialog,211)))exit(118);Check(page->Apply(),"debanding off editable");
    for(int algorithm=0;algorithm<3;algorithm++)for(int order=0;order<3;order++){
        SendDlgItemMessageW(dialog,132,CB_SETCURSEL,algorithm,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(132,CBN_SELCHANGE),0);SendDlgItemMessageW(dialog,133,CB_SETCURSEL,order,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(133,CBN_SELCHANGE),0);Check(page->Apply(),"moved deinterlacing selections");Check(config->GetSettings(&value),"moved field settings readback");if(value.deinterlacing!=algorithm||value.fieldOverride!=order)exit(159);
    }
    SendDlgItemMessageW(dialog,230,CB_SETCURSEL,2,0);CheckDlgButton(dialog,231,BST_CHECKED);CheckDlgButton(dialog,500,BST_CHECKED);SetDlgItemTextW(dialog,510,L"0.7");SendDlgItemMessageW(dialog,239,CB_SETCURSEL,3,0);
    Check(config->GetSettings(&value),"before merge edit");value.brightness=.14f;Check(config->SetSettings(&value),"other page edit");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(700,BN_CLICKED),0);Check(page->Apply(),"reset debanding Default");Check(config->GetSettings(&value),"reset readback");if(value.brightness!=.14f||value.deband!=0||value.debandIterations!=-1||value.debandThreshold!=-1||value.debandRadius!=-1||value.debandGrain!=-1||value.deinterlacing!=2||value.fieldOverride!=2||value.ditherMethod!=2||!value.ditherTemporal||!value.adaptiveSharpen||value.sharpenStrength!=.7f||value.maximumFramesInFlight!=3)exit(119);
    Check(page->Deactivate(),"debanding deactivate");Check(page->Activate(parent,&bounds,FALSE),"debanding reactivate Default");dialog=GetWindow(parent,GW_CHILD);wchar_t text[32];for(int id:{210,211,212,213}){GetDlgItemTextW(dialog,id,text,32);const float expected[]={1,3,16,4};if(wcstof(text,nullptr)!=expected[id-210])exit(120);}
    TestOutputDialog(dialog,page.Get(),config);
    Check(config->SetSettings(&before),"debanding restore");Check(page->Deactivate(),"debanding final deactivate");Check(page->SetObjects(0,nullptr),"debanding disconnect");Check(page->SetPageSite(nullptr),"debanding detach");DestroyWindow(parent);puts("PASS Debanding in Processing: Default, On/Off, direct descriptions, zero, full ranges, arrows, validation, reset, reactivation and page merge");
}
static void TestShaderPage(IClassFactory* factory,IBaseFilter* renderer,IPlaceboSettings* config){
 PlaceboSettings before;Check(config->GetSettings(&before),"shader snapshot");for(const auto& c:before.userShaders.shaders)if(c.enabled)exit(201);
 ComPtr<IPropertyPage> page;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&page)),"shader factory");ComPtr<PageSite> site;site.Attach(new PageSite);Check(page->SetPageSite(site.Get()),"shader site");IUnknown* object=renderer;Check(page->SetObjects(1,&object),"shader objects");
 HWND parent=CreateWindowExW(0,L"STATIC",L"Shader test",WS_POPUP,-10000,-10000,1100,850,nullptr,nullptr,g_hInst,nullptr);RECT bounds={0,0,1100,850};Check(page->Activate(parent,&bounds,FALSE),"shader activate");HWND d=GetWindow(parent,GW_CHILD);
 if(SendDlgItemMessageW(d,1500,LB_GETCOUNT,0,0)!=ShaderCount||NumberText(d,1530)!=.5f)exit(202);
 SendMessageW(d,WM_COMMAND,MAKEWPARAM(1500,LBN_DBLCLK),0);if(GetDlgItem(d,1501)||SendDlgItemMessageW(d,1500,LB_GETCURSEL,0,0)!=0)exit(213);wchar_t toggleText[128];SendDlgItemMessageW(d,1500,LB_GETTEXT,0,reinterpret_cast<LPARAM>(toggleText));if(wcsncmp(toggleText,L"[On]",4))exit(213);
 SendMessageW(d,WM_COMMAND,MAKEWPARAM(1500,LBN_DBLCLK),0);SendDlgItemMessageW(d,1500,LB_GETTEXT,0,reinterpret_cast<LPARAM>(toggleText));if(wcsncmp(toggleText,L"[Off]",5))exit(214);
 SendMessageW(d,WM_COMMAND,MAKEWPARAM(1500,LBN_DBLCLK),0);SetDlgItemTextW(d,1530,L"0,75");
 PlaceboSettings value;Check(config->GetSettings(&value),"shader unapplied");if(value.userShaders.shaders[0].enabled)exit(203);
 value.brightness=.08f;Check(config->SetSettings(&value),"shader cross-page edit");Check(page->Apply(),"shader apply");Check(config->GetSettings(&value),"shader readback");if(!value.userShaders.shaders[0].enabled||value.userShaders.shaders[0].values[0]!=.75f||value.brightness!=.08f)exit(204);
 SendMessageW(d,WM_COMMAND,MAKEWPARAM(1505,BN_CLICKED),0);Check(page->Apply(),"shader order apply");Check(config->GetSettings(&value),"shader order readback");if(value.userShaders.order[1]!=0||value.userShaders.order[0]!=1)exit(205);
 SendMessageW(d,WM_COMMAND,MAKEWPARAM(1506,BN_CLICKED),0);Check(page->Apply(),"shader parameters reset");Check(config->GetSettings(&value),"shader reset readback");if(value.userShaders.shaders[0].overrides||!value.userShaders.shaders[0].enabled)exit(206);
 for(int pos=0;pos<ShaderCount;pos++){SendDlgItemMessageW(d,1500,LB_SETCURSEL,pos,0);SendMessageW(d,WM_COMMAND,MAKEWPARAM(1500,LBN_SELCHANGE),0);const int id=value.userShaders.order[pos];for(int p=0;p<ShaderCatalog[id].count;p++)if(std::abs(NumberText(d,1530+p)-ShaderCatalog[id].parameters[p].initial)>1e-5)exit(207);}
 // Largest parameter set must remain inside the compact panel.
 SendDlgItemMessageW(d,1500,LB_SETCURSEL,2,0);SendMessageW(d,WM_COMMAND,MAKEWPARAM(1500,LBN_SELCHANGE),0);ShowWindow(parent,SW_SHOWNOACTIVATE);Check(page->Show(SW_SHOW),"shader preview show");UpdateWindow(d);SaveHdrPreview(d,"PlaceboRenderer/build/shaders-menu-preview.bmp");ShowWindow(parent,SW_HIDE);
 SendMessageW(d,WM_COMMAND,MAKEWPARAM(1507,BN_CLICKED),0);Check(page->Apply(),"all shaders off");Check(config->GetSettings(&value),"all off readback");for(const auto& c:value.userShaders.shaders)if(c.enabled)exit(208);
 auto invalid=value;invalid.userShaders.order[0]=invalid.userShaders.order[1];if(SUCCEEDED(config->SetSettings(&invalid)))exit(209);
 Check(page->Deactivate(),"shader deactivate");Check(page->Activate(parent,&bounds,FALSE),"shader reopen");Check(page->Apply(),"shader reopened apply");Check(config->GetSettings(&value),"shader persisted order");if(value.userShaders.order[1]!=0)exit(210);
 Check(config->SetSettings(&before),"shader restore");Check(page->Deactivate(),"shader final deactivate");Check(page->SetObjects(0,nullptr),"shader detach");Check(page->SetPageSite(nullptr),"shader detach site");DestroyWindow(parent);puts("PASS Shaders: 21 choices, numeric defaults, draft/apply, parameter edits, order, reset, All Off, validation, reopen and cross-page merge");
}
static void TestProcessing(IClassFactory* mainFactory,IClassFactory* processingFactory,IClassFactory* colorFactory,IClassFactory* hdrFactory,IBaseFilter* renderer,IPlaceboSettings* config){
    PlaceboSettings before;Check(config->GetSettings(&before),"processing settings snapshot");
    ComPtr<IPropertyPage> pages[4];ComPtr<PageSite> sites[4];HWND parents[4]={},dialogs[4]={};
    for(int i=0;i<4;i++){
        Check((i==3?hdrFactory:i==2?colorFactory:i?processingFactory:mainFactory)->CreateInstance(nullptr,IID_PPV_ARGS(&pages[i])),"three-page factory");sites[i].Attach(new PageSite);Check(pages[i]->SetPageSite(sites[i].Get()),"three-page site");IUnknown* object=renderer;Check(pages[i]->SetObjects(1,&object),"three-page config");
        parents[i]=CreateWindowExW(0,L"STATIC",L"Internal page merge test",WS_POPUP,0,0,1100,850,nullptr,nullptr,g_hInst,nullptr);RECT rect={0,0,1100,850};Check(pages[i]->Activate(parents[i],&rect,FALSE),"two-page activate");dialogs[i]=GetWindow(parents[i],GW_CHILD);
    }
    for(HWND dialog:dialogs){CheckNumberButtons(dialog);CheckHelp(dialog);}
    SetWindowPos(parents[3],nullptr,-10000,-10000,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);
    ShowWindow(parents[3],SW_SHOWNOACTIVATE);Check(pages[3]->Show(SW_SHOW),"HDR preview page show");
    const int primary[]={114,115,121,122,400};
    auto checkPrimary=[&](){for(int id:primary)if(!IsWindowVisible(GetDlgItem(dialogs[3],id)))exit(126);};checkPrimary();
    UpdateWindow(dialogs[3]);SaveHdrPreview(dialogs[3],"PlaceboRenderer/build/hdr-main.bmp");
    for(int section=1;section<=1;section++){
        SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(800+section,BN_CLICKED),0);checkPrimary();
        const int visibleIds[]={410,440,450};for(int i=0;i<3;i++)if(!!IsWindowVisible(GetDlgItem(dialogs[3],visibleIds[i]))!=(i==section-1))exit(126);
        for(HWND child=GetWindow(dialogs[3],GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(IsWindowVisible(child)&&GetDlgCtrlID(child)!=140&&GetDlgCtrlID(child)!=141){RECT rect,footer;GetWindowRect(child,&rect);GetWindowRect(GetDlgItem(dialogs[3],140),&footer);if(rect.bottom>footer.top)exit(127);}
        UpdateWindow(dialogs[3]);char filename[160];sprintf_s(filename,"PlaceboRenderer/build/hdr-advanced-%d.bmp",section);SaveHdrPreview(dialogs[3],filename);
        SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(800+section,BN_CLICKED),0);checkPrimary();if(IsWindowVisible(GetDlgItem(dialogs[3],visibleIds[section-1])))exit(128);
    }
    SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(801,BN_CLICKED),0);UpdateWindow(dialogs[3]);SaveHdrPreview(dialogs[3]);ShowWindow(parents[3],SW_HIDE);
    SetWindowPos(parents[0],nullptr,-10000,-10000,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);ShowWindow(parents[0],SW_SHOWNOACTIVATE);Check(pages[0]->Show(SW_SHOW),"image preview show");UpdateWindow(dialogs[0]);SaveHdrPreview(dialogs[0],"PlaceboRenderer/build/image-menu-preview.bmp");ShowWindow(parents[0],SW_HIDE);
    for(int index:{1,2}){SetWindowPos(parents[index],nullptr,-10000,-10000,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);ShowWindow(parents[index],SW_SHOWNOACTIVATE);Check(pages[index]->Show(SW_SHOW),"translated preview show");UpdateWindow(dialogs[index]);SaveHdrPreview(dialogs[index],index==1?"PlaceboRenderer/build/processing-menu-preview.bmp":"PlaceboRenderer/build/color-menu-preview.bmp");ShowWindow(parents[index],SW_HIDE);}
    for(int id:{132,133,210,211,212,213,220,230,231,500,510,236,237,238,239})if(!GetDlgItem(dialogs[1],id))exit(158);
    for(int id:{150,151,230,231,236,237,238,239})if(!GetDlgItem(dialogs[1],id))exit(26);
    if(SendDlgItemMessageW(dialogs[1],239,CB_GETCOUNT,0,0)!=17)exit(92);
    for(int frames=0;frames<=16;frames++){SendDlgItemMessageW(dialogs[1],239,CB_SETCURSEL,frames,0);SendMessageW(dialogs[1],WM_COMMAND,MAKEWPARAM(239,CBN_SELCHANGE),0);Check(pages[1]->Apply(),"frames in flight selection");PlaceboSettings value;Check(config->GetSettings(&value),"frame latency readback");if(value.maximumFramesInFlight!=frames)exit(93);}
    PlaceboSettings invalidLatency;Check(config->GetSettings(&invalidLatency),"latency snapshot");for(int value:{-1,17}){invalidLatency.maximumFramesInFlight=value;if(SUCCEEDED(config->SetSettings(&invalidLatency)))exit(94);}
    SendDlgItemMessageW(dialogs[1],239,CB_SETCURSEL,0,0);SendMessageW(dialogs[1],WM_COMMAND,MAKEWPARAM(239,CBN_SELCHANGE),0);Check(pages[1]->Apply(),"restore auto latency");
    for(int id:{150,151,300,310,311,312,320,321,322})if(!GetDlgItem(dialogs[2],id))exit(36);
    if(IsWindowEnabled(GetDlgItem(dialogs[2],310))||IsWindowEnabled(GetDlgItem(dialogs[2],321)))exit(37);
    PlaceboSettings correctionEdit;Check(config->GetSettings(&correctionEdit),"color page edit snapshot");correctionEdit.gamma=1.2f;Check(config->SetSettings(&correctionEdit),"separate gamma page apply");
    SendDlgItemMessageW(dialogs[1],220,CB_SETCURSEL,1,0);SetDlgItemTextW(dialogs[1],210,L"2");SetDlgItemTextW(dialogs[1],211,L"4.5");SetDlgItemTextW(dialogs[1],212,L"20");SetDlgItemTextW(dialogs[1],213,L"1");Check(pages[1]->Apply(),"apply debanding inside Processing");
    for(int choice=0;choice<4;choice++){SendDlgItemMessageW(dialogs[1],230,CB_SETCURSEL,choice,0);SendMessageW(dialogs[1],WM_COMMAND,MAKEWPARAM(230,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialogs[1],230)));Check(pages[1]->Apply(),"dither selection");if(!!IsWindowEnabled(GetDlgItem(dialogs[1],231))!=(choice!=3))exit(27);}
    for(int choice=0;choice<4;choice++){SendDlgItemMessageW(dialogs[1],236,CB_SETCURSEL,choice,0);SendMessageW(dialogs[1],WM_COMMAND,MAKEWPARAM(236,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialogs[1],236)));Check(pages[1]->Apply(),"output format selection");}
    for(int choice=0;choice<3;choice++){SendDlgItemMessageW(dialogs[1],237,CB_SETCURSEL,choice,0);SendMessageW(dialogs[1],WM_COMMAND,MAKEWPARAM(237,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialogs[1],237)));Check(pages[1]->Apply(),"output range selection");PlaceboSettings v;Check(config->GetSettings(&v),"range readback");if(v.outputLevels!=choice)exit(41);}
    const int depths[]={0,6,8,10,12,16};for(int choice=0;choice<6;choice++){SendDlgItemMessageW(dialogs[1],238,CB_SETCURSEL,choice,0);SendMessageW(dialogs[1],WM_COMMAND,MAKEWPARAM(238,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(dialogs[1],238)));Check(pages[1]->Apply(),"display depth selection");PlaceboSettings v;Check(config->GetSettings(&v),"depth readback");if(v.displayDepth!=depths[choice])exit(42);}
    Check(config->GetSettings(&correctionEdit),"separate saturation edit snapshot");correctionEdit.saturation=.8f;Check(config->SetSettings(&correctionEdit),"separate saturation page apply");PlaceboSettings after;Check(config->GetSettings(&after),"merged settings");
    if(after.gamma!=1.2f||after.saturation!=.8f||!after.deband||after.debandIterations!=2||after.debandThreshold!=4.5f||after.debandRadius!=20||after.debandGrain!=1||after.ditherMethod!=3)exit(28);
    SetDlgItemTextW(dialogs[2],310,L"C:\\Farbprofile\\Prüfung.icc");SetDlgItemTextW(dialogs[2],321,L"C:\\Farbprofile\\Prüfung.cube");SendDlgItemMessageW(dialogs[2],312,CB_SETCURSEL,1,0);Check(pages[2]->Apply(),"save disabled color paths and intent");
    Check(config->GetSettings(&after),"color settings readback");if(after.iccEnabled||after.lutEnabled||after.iccIntent!=1||wcscmp(after.iccPath,L"C:\\Farbprofile\\Prüfung.icc")||after.gamma!=1.2f||after.debandThreshold!=4.5f)exit(38);
    for(int kind=0;kind<5;kind++){SendDlgItemMessageW(dialogs[2],318,CB_SETCURSEL,kind,0);SendMessageW(dialogs[2],WM_COMMAND,MAKEWPARAM(318,CBN_SELCHANGE),0);Check(pages[2]->Apply(),"LUT interpretation choice");Check(config->GetSettings(&after),"LUT type readback");if(after.extras.values[14]!=kind-1)exit(169);}
    for(int mode=0;mode<3;mode++){SendDlgItemMessageW(dialogs[3],121,CB_SETCURSEL,mode,0);SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(121,CBN_SELCHANGE),0);Check(pages[3]->Apply(),"HDR mode selection");Check(config->GetSettings(&after),"HDR mode readback");if(after.hdrMode!=mode||!!IsWindowEnabled(GetDlgItem(dialogs[3],122))!=(mode!=2)||!!IsWindowEnabled(GetDlgItem(dialogs[3],115))!=(mode==0))exit(135);}
    SendDlgItemMessageW(dialogs[3],121,CB_SETCURSEL,0,0);SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(121,CBN_SELCHANGE),0);Check(pages[3]->Apply(),"restore Auto HDR mode");
    for(int id:{114,115,121,122,415,416,417,801})if(!GetDlgItem(dialogs[3],id))exit(129);
    SetDlgItemTextW(dialogs[3],114,L"150");SetDlgItemTextW(dialogs[3],115,L"600");CheckDlgButton(dialogs[3],122,BST_UNCHECKED);CheckDlgButton(dialogs[3],415,BST_CHECKED);SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(415,BN_CLICKED),0);Check(pages[3]->Apply(),"unified HDR output and delayed peak");Check(config->GetSettings(&after),"unified HDR values readback");if(after.sdrNits!=150||after.hdrNits!=600||after.autoHdr||!after.allowDelayedPeak)exit(130);
    SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(417,BN_CLICKED),0);Check(pages[3]->Apply(),"high quality peak values");Check(config->GetSettings(&after),"HQ peak readback");if(after.peakPercentile!=99.995f||after.allowDelayedPeak)exit(131);
    SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(416,BN_CLICKED),0);Check(pages[3]->Apply(),"default peak values");Check(config->GetSettings(&after),"default peak readback");if(after.peakPercentile!=100||after.peakSmoothing!=20||after.sceneLow!=1||after.sceneHigh!=3||after.blackCutoff!=1||after.sdrNits!=150||after.hdrNits!=600)exit(132);
    for(int id:{400,410,411,412,413})if(!GetDlgItem(dialogs[3],id))exit(43);
    CheckDlgButton(dialogs[3],400,BST_UNCHECKED);SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(400,BN_CLICKED),0);if(IsWindowEnabled(GetDlgItem(dialogs[3],410)))exit(44);CheckNumberButtons(dialogs[3]);NumberClick(dialogs[3],410,true);if(NumberText(dialogs[3],410)!=20)exit(81);Check(pages[3]->Apply(),"disable own HDR measurement");
    CheckDlgButton(dialogs[3],400,BST_CHECKED);SendMessageW(dialogs[3],WM_COMMAND,MAKEWPARAM(400,BN_CLICKED),0);if(!IsWindowEnabled(GetDlgItem(dialogs[3],410)))exit(45);
    SetDlgItemTextW(dialogs[3],410,L"30");SetDlgItemTextW(dialogs[3],411,L"0,5");SetDlgItemTextW(dialogs[3],412,L"4");SetDlgItemTextW(dialogs[3],413,L"99,995");SetDlgItemTextW(dialogs[3],420,L"0,4");SetDlgItemTextW(dialogs[3],421,L"5");
    Check(pages[3]->Apply(),"peak settings after moving Color Mapping");Check(config->GetSettings(&after),"peak settings readback");if(after.peakPercentile!=99.995f||after.gamma!=1.2f||after.debandThreshold!=4.5f)exit(46);
    SetDlgItemTextW(dialogs[3],411,L"5");if(SUCCEEDED(pages[3]->Apply()))exit(47);SetDlgItemTextW(dialogs[3],411,L"0,5");SetDlgItemTextW(dialogs[3],413,L"101");if(SUCCEEDED(pages[3]->Apply()))exit(48);
    if(!GetDlgItem(dialogs[1],500)||!GetDlgItem(dialogs[1],510))exit(49);
    CheckDlgButton(dialogs[1],500,BST_CHECKED);SendMessageW(dialogs[1],WM_COMMAND,MAKEWPARAM(500,BN_CLICKED),0);if(!IsWindowEnabled(GetDlgItem(dialogs[1],510)))exit(50);SetDlgItemTextW(dialogs[1],510,L"0,75");Check(pages[1]->Apply(),"sharpening page apply");Check(config->GetSettings(&after),"sharpening page readback");if(!after.adaptiveSharpen||after.sharpenStrength!=.75f||after.gamma!=1.2f||after.debandThreshold!=4.5f)exit(51);
    SetDlgItemTextW(dialogs[1],510,L"2,1");if(SUCCEEDED(pages[1]->Apply()))exit(52);SetDlgItemTextW(dialogs[1],510,L"0,75");
    SetDlgItemTextW(dialogs[3],413,L"99,995");SetDlgItemTextW(dialogs[3],414,L"2");Check(pages[3]->Apply(),"peak advanced apply");
    Check(pages[0]->Apply(),"independent deinterlacing apply");Check(config->GetSettings(&after),"cross-page values");if(after.sdrNits!=150||after.hdrNits!=600||after.blackCutoff!=2)exit(60);
    Check(pages[3]->Deactivate(),"HDR deactivate");RECT hdrBounds={0,0,1100,850};Check(pages[3]->Activate(parents[3],&hdrBounds,FALSE),"HDR reactivate");dialogs[3]=GetWindow(parents[3],GW_CHILD);Check(pages[3]->Apply(),"HDR reopen apply");Check(config->GetSettings(&after),"HDR retained values");if(after.sdrNits!=150||after.hdrNits!=600||after.peakPercentile!=99.995f)exit(134);
    Check(config->SetSettings(&before),"restore settings after processing page test");
    for(int i=0;i<4;i++){Check(pages[i]->Deactivate(),"three-page deactivate");Check(pages[i]->SetObjects(0,nullptr),"three-page disconnect");Check(pages[i]->SetPageSite(nullptr),"three-page detach");DestroyWindow(parents[i]);}
    puts("PASS four pages: decimal arrow steps, limits, auto/manual transitions, disabled arrows, compact HDR controls, persistence and no cross-page setting loss");
}
static int tonalAction=0;static bool tonalSeen=false;
static BOOL CALLBACK TonalWindow(HWND d,LPARAM){
    wchar_t caption[100];GetWindowTextW(d,caption,100);if(wcscmp(caption,L"HDR Tonal Adjustments"))return TRUE;
    if(!IsWindowVisible(d)||!GetDlgItem(d,1608))return TRUE;
    tonalSeen=true;
    if(tonalAction==0){
        for(int i=0;i<9;i++)if(!IsWindowVisible(GetDlgItem(d,1600+i)))exit(210);
        SaveHdrPreview(d,"PlaceboRenderer/build/hdr-tonal-dialog.bmp");
        SetDlgItemTextW(d,1600,L"0.5");SendMessageW(d,WM_COMMAND,IDCANCEL,0);
    }else if(tonalAction==1){
        for(int i=0;i<9;i++){SetDlgItemTextW(d,1600+i,L"0");SendMessageW(d,WM_COMMAND,MAKEWPARAM(1701+i*2,BN_CLICKED),0);if(std::abs(NumberText(d,1600+i)-.005f)>1e-7)exit(238);SendMessageW(d,WM_COMMAND,MAKEWPARAM(1700+i*2,BN_CLICKED),0);if(std::abs(NumberText(d,1600+i))>1e-7)exit(239);}
        SetDlgItemTextW(d,1600,L"-0,0005");SendMessageW(d,WM_COMMAND,MAKEWPARAM(1701,BN_CLICKED),0);if(std::abs(NumberText(d,1600)-.0045f)>1e-7)exit(240);
        SetDlgItemTextW(d,1600,L"0,5");SetDlgItemTextW(d,1604,L"0.25");SetDlgItemTextW(d,1608,L"-0.2");
        SendMessageW(d,WM_COMMAND,MAKEWPARAM(1712,BN_CLICKED),0);
        SendMessageW(d,WM_COMMAND,IDOK,0);
    }else{SendMessageW(d,WM_COMMAND,MAKEWPARAM(1650,BN_CLICKED),0);SendMessageW(d,WM_COMMAND,IDOK,0);}
    return FALSE;
}
static void CALLBACK TonalTimer(HWND,UINT,UINT_PTR,DWORD){EnumThreadWindows(GetCurrentThreadId(),TonalWindow,0);}
static void TestTonalDialog(HWND dialog,IPropertyPage* page,IPlaceboSettings* config){
    PlaceboSettings before;Check(config->GetSettings(&before),"tonal snapshot");
    if(!IsWindowVisible(GetDlgItem(dialog,902)))exit(211);
    for(int action=0;action<3;action++){
        tonalAction=action;tonalSeen=false;auto timer=SetTimer(nullptr,0,10,TonalTimer);if(!timer)exit(212);
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(902,BN_CLICKED),0);KillTimer(nullptr,timer);if(!tonalSeen)exit(213);
        PlaceboSettings read;Check(config->GetSettings(&read),"modal changes staged");if(action<2&&memcmp(&before.tonal,&read.tonal,sizeof(read.tonal)))exit(214);
        Check(page->Apply(),"tonal apply");Check(config->GetSettings(&read),"tonal apply readback");
        if(action==0&&memcmp(&before.tonal,&read.tonal,sizeof(read.tonal)))exit(215);
        if(action==1&&(read.tonal.exposure[0]!=.5f||read.tonal.contrast[1]!=.25f||read.tonal.saturation[2]!=-.2f||read.tonal.exposure[2]!=-.005f))exit(216);
        if(action==2&&ActiveTonal(read.tonal))exit(217);
    }
    puts("PASS HDR tonal dialog: 9 visible fields, Cancel, staged Apply, decimals, arrows, Reset");
}
static void TestMapping(IClassFactory* factory,IClassFactory* hdrFactory,IBaseFilter* renderer,IPlaceboSettings* config){
    PlaceboSettings before;Check(config->GetSettings(&before),"mapping snapshot");
    ComPtr<IPropertyPage> hdr;Check(hdrFactory->CreateInstance(nullptr,IID_PPV_ARGS(&hdr)),"parallel HDR page");ComPtr<PageSite> hdrSite;hdrSite.Attach(new PageSite);Check(hdr->SetPageSite(hdrSite.Get()),"HDR merge site");IUnknown* hdrObject=renderer;Check(hdr->SetObjects(1,&hdrObject),"HDR merge config");HWND hdrParent=CreateWindowExW(0,L"STATIC",L"HDR merge",WS_POPUP,0,0,1100,850,nullptr,nullptr,g_hInst,nullptr);RECT hdrBounds={0,0,1100,850};Check(hdr->Activate(hdrParent,&hdrBounds,FALSE),"stale HDR page activate");
    ComPtr<IPropertyPage> page;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&page)),"mapping page");ComPtr<PageSite> site;site.Attach(new PageSite);Check(page->SetPageSite(site.Get()),"mapping site");IUnknown* object=renderer;Check(page->SetObjects(1,&object),"mapping config");
    HWND parent=CreateWindowExW(0,L"STATIC",L"Mapping preview",WS_POPUP,-10000,-10000,1100,850,nullptr,nullptr,g_hInst,nullptr);RECT bounds={0,0,1100,850};Check(page->Activate(parent,&bounds,FALSE),"mapping activate");HWND dialog=GetWindow(parent,GW_CHILD);ShowWindow(parent,SW_SHOWNOACTIVATE);Check(page->Show(SW_SHOW),"mapping visible");
    TestTonalDialog(dialog,page.Get(),config);
    if(SendDlgItemMessageW(dialog,434,CB_GETCOUNT,0,0)!=std::size(ToneChoices)||SendDlgItemMessageW(dialog,430,CB_GETCOUNT,0,0)!=std::size(GamutChoices))exit(140);
    for(int group=0;group<8;group++){
        SendDlgItemMessageW(dialog,900,CB_SETCURSEL,group,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(900,CBN_SELCHANGE),0);if(page->IsPageDirty()!=S_FALSE)exit(141);
        for(int id:{114,115,121,122,400,434,430,900,460})if(!IsWindowVisible(GetDlgItem(dialog,id)))exit(142);
        char path[180];sprintf_s(path,"PlaceboRenderer/build/color-mapping-%d.bmp",group);SaveHdrPreview(dialog,path);
        for(int row=0;row<8;row++)if(IsWindowVisible(GetDlgItem(dialog,920+row))){RECT r,footer;GetWindowRect(GetDlgItem(dialog,920+row),&r);GetWindowRect(GetDlgItem(dialog,970),&footer);if(r.bottom>footer.top)exit(143);}
    }
    SendDlgItemMessageW(dialog,900,CB_SETCURSEL,1,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(900,CBN_SELCHANGE),0);SetDlgItemTextW(dialog,925,L"0,75");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(925,CBN_EDITCHANGE),0);Check(page->Apply(),"mapping decimal apply");PlaceboSettings after;Check(config->GetSettings(&after),"mapping readback");if(strcmp(after.colorMapping.values[MappingIndex("exposure")],"0.75"))exit(144);
    Check(hdr->Apply(),"stale HDR page preserves mapping");Check(config->GetSettings(&after),"HDR merge mapping readback");if(strcmp(after.colorMapping.values[MappingIndex("exposure")],"0.75"))exit(146);
    for(int choice=0;choice<std::size(ToneChoices);choice++){SendDlgItemMessageW(dialog,434,CB_SETCURSEL,choice,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(434,CBN_SELCHANGE),0);Check(page->Apply(),"mapping tone selection");Check(config->GetSettings(&after),"mapping tone readback");if(after.toneMapping!=choice)exit(160);}
    for(int choice=0;choice<std::size(GamutChoices);choice++){SendDlgItemMessageW(dialog,430,CB_SETCURSEL,choice,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(430,CBN_SELCHANGE),0);Check(page->Apply(),"mapping gamut selection");Check(config->GetSettings(&after),"mapping gamut readback");if(after.gamutMapping!=choice)exit(161);}
    for(int mode=0;mode<3;mode++){SendDlgItemMessageW(dialog,121,CB_SETCURSEL,mode,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(121,CBN_SELCHANGE),0);Check(page->Apply(),"combined HDR modes");Check(config->GetSettings(&after),"combined HDR mode readback");if(after.hdrMode!=mode||!!IsWindowEnabled(GetDlgItem(dialog,115))!=(mode==0)||!!IsWindowEnabled(GetDlgItem(dialog,122))!=(mode!=2))exit(151);}
    SendDlgItemMessageW(dialog,121,CB_SETCURSEL,0,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(121,CBN_SELCHANGE),0);SetDlgItemTextW(dialog,114,L"160");SetDlgItemTextW(dialog,115,L"700");
    SendDlgItemMessageW(dialog,900,CB_SETCURSEL,7,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(900,CBN_SELCHANGE),0);CheckNumberButtons(dialog);CheckHelp(dialog);CheckDlgButton(dialog,400,BST_UNCHECKED);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(400,BN_CLICKED),0);if(IsWindowEnabled(GetDlgItem(dialog,410)))exit(152);CheckDlgButton(dialog,400,BST_CHECKED);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(400,BN_CLICKED),0);
    SetDlgItemTextW(dialog,410,L"35");SetDlgItemTextW(dialog,411,L"0,5");SetDlgItemTextW(dialog,412,L"4");SetDlgItemTextW(dialog,413,L"99,995");SetDlgItemTextW(dialog,414,L"2");CheckDlgButton(dialog,415,BST_CHECKED);Check(page->Apply(),"combined HDR peak values");Check(config->GetSettings(&after),"combined peak readback");if(after.sdrNits!=160||after.hdrNits!=700||after.peakSmoothing!=35||after.sceneLow!=.5f||after.sceneHigh!=4||after.peakPercentile!=99.995f||after.blackCutoff!=2||!after.allowDelayedPeak||strcmp(after.colorMapping.values[MappingIndex("exposure")],"0.75"))exit(153);
    SetDlgItemTextW(dialog,413,L"101");if(SUCCEEDED(page->Apply()))exit(154);SetDlgItemTextW(dialog,413,L"99,995");SetDlgItemTextW(dialog,411,L"5");if(SUCCEEDED(page->Apply()))exit(155);SetDlgItemTextW(dialog,411,L"0,5");
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(417,BN_CLICKED),0);Check(page->Apply(),"combined HQ peak");Check(config->GetSettings(&after),"HQ peak only readback");if(after.peakPercentile!=99.995f||after.allowDelayedPeak||after.hdrNits!=700||strcmp(after.colorMapping.values[MappingIndex("exposure")],"0.75"))exit(156);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(416,BN_CLICKED),0);Check(page->Apply(),"combined default peak");Check(config->GetSettings(&after),"peak default only readback");if(after.peakPercentile!=100||after.peakSmoothing!=20||after.blackCutoff!=1||after.hdrNits!=700)exit(157);
    Check(page->Deactivate(),"mapping deactivate");Check(page->Activate(parent,&bounds,FALSE),"mapping reopen");Check(page->Apply(),"mapping reopened apply");Check(config->GetSettings(&after),"mapping reopen readback");if(strcmp(after.colorMapping.values[MappingIndex("exposure")],"0.75"))exit(145);
    dialog=GetWindow(parent,GW_CHILD);SetDlgItemTextW(dialog,115,L"800");SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(901,BN_CLICKED),0);Check(config->GetSettings(&after),"unapplied reset");if(strcmp(after.colorMapping.values[MappingIndex("exposure")],"0.75"))exit(147);Check(page->Apply(),"mapping reset apply");Check(config->GetSettings(&after),"reset readback");for(const auto& value:after.colorMapping.values)if(value[0])exit(148);if(after.toneMapping||after.gamutMapping||after.showClipping||after.hdrNits!=800)exit(149);
    Check(hdr->Deactivate(),"HDR merge close");Check(hdr->SetObjects(0,nullptr),"HDR merge detach");Check(hdr->SetPageSite(nullptr),"HDR merge site detach");DestroyWindow(hdrParent);
    Check(page->Deactivate(),"mapping close");Check(page->SetObjects(0,nullptr),"mapping detach");Check(page->SetPageSite(nullptr),"mapping site detach");DestroyWindow(parent);Check(config->SetSettings(&before),"mapping restore");puts("PASS combined HDR and Color Mapping page: all 8 groups, native choices, category navigation, decimal apply, reset, cross-page merge and reopen persistence");
}
class PatternStream final:public CSourceStream {
    unsigned index=0;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;HANDLE mutex=nullptr;
public:
    PatternStream(CSource* owner,HRESULT* hr):CSourceStream(NAME("P010 pattern"),hr,owner,L"Output"){}
    ~PatternStream(){if(mutex)CloseHandle(mutex);}
    HRESULT CompleteConnect(IPin* pin)override{
        HRESULT hr=CSourceStream::CompleteConnect(pin);if(FAILED(hr)||!hardware)return hr;
        ComPtr<ID3D11DecoderConfiguration> config;hr=pin->QueryInterface(IID_PPV_ARGS(&config));if(FAILED(hr))return hr;
        hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);if(FAILED(hr))return hr;
        if(mutex)CloseHandle(mutex);mutex=CreateMutexW(nullptr,FALSE,nullptr);if(!mutex)return HRESULT_FROM_WIN32(GetLastError());
        hr=config->ActivateD3D11Decoding(device.Get(),context.Get(),mutex,0);if(FAILED(hr))return hr;
        ComPtr<IMemInputPin> input;hr=pin->QueryInterface(IID_PPV_ARGS(&input));if(FAILED(hr))return hr;
        ComPtr<IMemAllocator> proposed;
        if(input->GetAllocator(&proposed)!=E_NOTIMPL||proposed){printf("FAIL renderer offered CPU allocator after native decoder activation\n");return E_FAIL;}
        printf("PASS native decoder owns allocator after device activation\n");return S_OK;
    }
    HRESULT GetDeliveryBuffer(IMediaSample** out,REFERENCE_TIME* start,REFERENCE_TIME* stop,DWORD flags)override{
        ComPtr<IMediaSample> base;HRESULT hr=CSourceStream::GetDeliveryBuffer(&base,start,stop,flags);if(FAILED(hr))return hr;
        if(!hardware){*out=base.Detach();return S_OK;}
        D3D11_TEXTURE2D_DESC desc={};desc.Width=desc.Height=64;desc.MipLevels=1;desc.ArraySize=2;desc.Format=textureFormat;desc.SampleDesc.Count=1;desc.BindFlags=textureBind;
        ComPtr<ID3D11Texture2D> texture;hr=device->CreateTexture2D(&desc,nullptr,&texture);if(FAILED(hr)){if(sourceFailure.exchange(hr)==S_OK)printf("SOURCE CreateTexture2D DXGI format %u, bind %u, HRESULT %08lX\n",static_cast<unsigned>(desc.Format),textureBind,static_cast<unsigned long>(hr));return hr;}
        *out=new TextureSample(base.Get(),texture.Get());return S_OK;
    }
    HRESULT GetMediaType(CMediaType* mt)override{
        mt->InitMediaType();mt->SetType(&MEDIATYPE_Video);GUID p010={FourCC(),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};mt->SetSubtype(&p010);mt->SetFormatType(&FORMAT_VideoInfo2);
        auto info=reinterpret_cast<VIDEOINFOHEADER2*>(mt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER2)));if(!info)return E_OUTOFMEMORY;*info={};info->AvgTimePerFrame=333333;info->dwInterlaceFlags=interlacedTest?(AMINTERLACE_IsInterlaced|(bottomFirst?0:AMINTERLACE_Field1First)):0;info->dwPictAspectRatioX=1;info->dwPictAspectRatioY=1;info->bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info->bmiHeader.biWidth=info->bmiHeader.biHeight=64;info->bmiHeader.biPlanes=1;info->bmiHeader.biBitCount=textureFormat==DXGI_FORMAT_NV12?12:24;info->bmiHeader.biCompression=p010.Data1;info->bmiHeader.biSizeImage=SampleBytes();mt->SetSampleSize(SampleBytes());return S_OK;
    }
    HRESULT DecideBufferSize(IMemAllocator* allocator,ALLOCATOR_PROPERTIES* props)override{props->cBuffers=2;props->cbBuffer=SampleBytes();props->cbAlign=1;ALLOCATOR_PROPERTIES actual={};return allocator->SetProperties(props,&actual);}
    HRESULT OnThreadCreate()override{index=0;return S_OK;}
    HRESULT FillBuffer(IMediaSample* sample)override{
        if(index>=(eosTest?6u:90u))return S_FALSE;BYTE* bytes=nullptr;HRESULT hr=sample->GetPointer(&bytes);if(FAILED(hr))return hr;
        ComPtr<IMediaSample2> propsSample;if(SUCCEEDED(sample->QueryInterface(IID_PPV_ARGS(&propsSample)))){AM_SAMPLE2_PROPERTIES props={};Check(propsSample->GetProperties(sizeof(props),reinterpret_cast<BYTE*>(&props)),"sample properties");props.dwTypeSpecificFlags=weaveTest?AM_VIDEO_FLAG_WEAVE:bottomFirst?0:AM_VIDEO_FLAG_FIELD1FIRST;Check(propsSample->SetProperties(sizeof(props),reinterpret_cast<const BYTE*>(&props)),"sample field flags");}
        ComPtr<IMediaSideData> side;hr=sample->QueryInterface(IID_PPV_ARGS(&side));if(FAILED(hr))return hr;
        const BYTE* old=nullptr;size_t oldBytes=0;if(SUCCEEDED(side->GetSideData(IID_MediaSideDataHDR,&old,&oldBytes))){printf("FAIL reused allocator sample retained old HDR metadata\n");return E_FAIL;}
        MediaSideDataHDR hdr={};hdr.display_primaries_x[0]=.265;hdr.display_primaries_y[0]=.69;hdr.display_primaries_x[1]=.15;hdr.display_primaries_y[1]=.06;hdr.display_primaries_x[2]=.68;hdr.display_primaries_y[2]=.32;hdr.white_point_x=.3127;hdr.white_point_y=.329;hdr.max_display_mastering_luminance=1000;hdr.min_display_mastering_luminance=.0001;
        hr=side->SetSideData(IID_MediaSideDataHDR,reinterpret_cast<const BYTE*>(&hdr),sizeof(hdr));if(FAILED(hr))return hr;MediaSideDataHDRContentLightLevel light={1200,400};hr=side->SetSideData(IID_MediaSideDataHDRContentLightLevel,reinterpret_cast<const BYTE*>(&light),sizeof(light));if(FAILED(hr))return hr;
        if(textureFormat==DXGI_FORMAT_NV12){std::fill_n(bytes,64*64,static_cast<BYTE>(90));for(int i=64*64;i<64*64*3/2;i+=2){bytes[i]=48;bytes[i+1]=210;}}
        else{const unsigned factor=textureFormat==DXGI_FORMAT_P016?257:256;auto data=reinterpret_cast<uint16_t*>(bytes);std::fill_n(data,64*64,static_cast<uint16_t>(90*factor));for(int i=64*64;i<64*64*3/2;i+=2){data[i]=static_cast<uint16_t>(48*factor);data[i+1]=static_cast<uint16_t>(210*factor);}}
        if(hardware){ComPtr<IMediaSampleD3D11> gpu;hr=sample->QueryInterface(IID_PPV_ARGS(&gpu));if(FAILED(hr))return hr;ComPtr<ID3D11Texture2D> tex;UINT slice=0;hr=gpu->GetD3D11Texture(0,&tex,&slice);if(FAILED(hr))return hr;
            if(WaitForSingleObject(mutex,1000)!=WAIT_OBJECT_0)return E_FAIL;context->UpdateSubresource(tex.Get(),slice,nullptr,bytes,textureFormat==DXGI_FORMAT_NV12?64:128,0);context->Flush();ReleaseMutex(mutex);}
        REFERENCE_TIME start=index*333333LL,stop=start+333333;sample->SetTime(&start,&stop);sample->SetSyncPoint(TRUE);sample->SetActualDataLength(SampleBytes());index++;return S_OK;
    }
};
class PatternSource final:public CSource {
public:
    PatternSource(HRESULT* hr):CSource(NAME("P010 test source"),nullptr,CLSID_NULL){new PatternStream(this,hr);}
};
static int Spread(const std::vector<BYTE>& pixels){long sum=0;for(size_t i=0;i<pixels.size();i+=4)sum+=std::max({pixels[i],pixels[i+1],pixels[i+2]})-std::min({pixels[i],pixels[i+1],pixels[i+2]});return static_cast<int>(sum/(pixels.size()/4));}
using FactoryGetter=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
static void PumpLive(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static IPlaceboSettings* liveConfig=nullptr;static bool liveModalKeep=false,liveModalSeen=false;static int liveModalKind=0;
static BOOL CALLBACK LiveModalWindow(HWND d,LPARAM){wchar_t title[100];GetWindowTextW(d,title,100);const wchar_t* expected=liveModalKind==0?L"HDR Tonal Adjustments":liveModalKind==1?L"Output Background and Advanced":L"Black Bars / Zoom";if(wcscmp(title,expected)||!IsWindowVisible(d)||!GetDlgItem(d,liveModalKind==0?1608:liveModalKind==1?1113:706))return TRUE;
 if(liveModalSeen)return FALSE;liveModalSeen=true;
 if(liveModalKind==0){for(int i=0;i<9;i++){HWND edit=GetDlgItem(d,1600+i);SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);for(wchar_t c:std::wstring(i%3==0?L"3":L"-0,5")){SendMessageW(edit,WM_CHAR,c,1);PumpLive();}PlaceboSettings actual;Check(liveConfig->GetSettings(&actual),"typed tonal number");float value=i%3==0?actual.tonal.exposure[i/3]:i%3==1?actual.tonal.contrast[i/3]:actual.tonal.saturation[i/3];if(value!=(i%3==0?3.f:-.5f)){printf("FAIL typed tonal control %d value %.6f\n",i,double(value));exit(237);}}}
 if(liveModalKind==2){CheckDlgButton(d,701,BST_CHECKED);SendMessageW(d,WM_COMMAND,MAKEWPARAM(701,BN_CLICKED),0);}else{const int field=liveModalKind==0?1606:1100;if(liveModalKind==0){HWND edit=GetDlgItem(d,field);SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);for(wchar_t c:std::wstring(L"-1")){SendMessageW(edit,WM_CHAR,c,1);PumpLive();}}else{SetDlgItemTextW(d,field,L"0.25");SendMessageW(d,WM_COMMAND,MAKEWPARAM(field,EN_CHANGE),0);}}PumpLive();PlaceboSettings v;Check(liveConfig->GetSettings(&v),"live modal readback");if((liveModalKind==0&&v.tonal.exposure[2]!=-1)||(liveModalKind==1&&v.extras.values[0]!=.25f)||(liveModalKind==2&&!v.blackBars.enabled))exit(220);SendMessageW(d,WM_COMMAND,liveModalKeep?IDOK:IDCANCEL,0);return FALSE;}
static void CALLBACK LiveModalTimer(HWND,UINT,UINT_PTR,DWORD){EnumThreadWindows(GetCurrentThreadId(),LiveModalWindow,0);}
static void TestLiveUI(FactoryGetter get,IBaseFilter* renderer,IPlaceboSettings* config){
 PlaceboSettings baseline;Check(config->GetSettings(&baseline),"live baseline");
 for(bool keep:{false,true}){
  HWND host=CreateWindowExW(0,L"STATIC",L"Live test host",WS_POPUP,-10000,-10000,1100,850,nullptr,nullptr,g_hInst,nullptr);
  HWND apply=CreateWindowExW(0,L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE,0,0,50,20,host,reinterpret_cast<HMENU>(0x3021),g_hInst,nullptr);
  const CLSID ids[]={CLSID_PlaceboAdjustmentPage,CLSID_PlaceboProcessingPage,CLSID_PlaceboColorPage,CLSID_PlaceboScalingPage,CLSID_PlaceboMappingPage,CLSID_PlaceboEffectsPage};
  ComPtr<IPropertyPage> pages[6];ComPtr<PageSite> sites[6];HWND dialogs[6]={};HWND containers[6]={};ShowWindow(host,SW_SHOWNOACTIVATE);
  for(int i=0;i<6;i++){ComPtr<IClassFactory> factory;Check(get(ids[i],IID_PPV_ARGS(&factory)),"live page factory");Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&pages[i])),"live page create");sites[i].Attach(new PageSite);Check(pages[i]->SetPageSite(sites[i].Get()),"live site");IUnknown* o=renderer;Check(pages[i]->SetObjects(1,&o),"live objects");containers[i]=CreateWindowExW(0,L"STATIC",L"Live page",WS_CHILD|WS_VISIBLE,0,0,1100,850,host,nullptr,g_hInst,nullptr);RECT bounds={0,0,1100,850};Check(pages[i]->Activate(containers[i],&bounds,FALSE),"live page activate");dialogs[i]=GetWindow(containers[i],GW_CHILD);if(dialogs[i]==apply)exit(221);}
  if(GetWindowLongPtrW(apply,GWL_STYLE)&WS_VISIBLE)exit(222);
  auto edit=[&](int page,int id,const wchar_t* text){SetDlgItemTextW(dialogs[page],id,text);SendMessageW(dialogs[page],WM_COMMAND,MAKEWPARAM(id,EN_CHANGE),0);PumpLive();};
  PlaceboSettings v;
  edit(0,110,L"0.15");Check(config->GetSettings(&v),"live color adjustment");if(std::abs(v.saturation-1.15f)>1e-6)exit(223);
  edit(0,110,L"-");Check(config->GetSettings(&v),"partial input retained");if(std::abs(v.saturation-1.15f)>1e-6)exit(224);edit(0,110,L"0.15");
  edit(1,510,L"0.65");Check(config->GetSettings(&v),"live Processing");if(v.sharpenStrength!=.65f)exit(225);
  SendDlgItemMessageW(dialogs[2],318,CB_SETCURSEL,2,0);SendMessageW(dialogs[2],WM_COMMAND,MAKEWPARAM(318,CBN_SELCHANGE),0);PumpLive();Check(config->GetSettings(&v),"live Color Management");if(v.extras.values[14]!=1)exit(226);
  edit(3,605,L"0.35");Check(config->GetSettings(&v),"live Scaling");if(v.scaling.antiringing!=.35f)exit(227);
  edit(4,114,L"180");Check(config->GetSettings(&v),"live HDR");if(v.sdrNits!=180)exit(228);
  liveConfig=config;liveModalKind=0;for(bool modalKeep:{false,true}){liveModalKeep=modalKeep;liveModalSeen=false;auto timer=SetTimer(nullptr,0,10,LiveModalTimer);SendMessageW(dialogs[4],WM_COMMAND,MAKEWPARAM(902,BN_CLICKED),0);KillTimer(nullptr,timer);PumpLive();if(!liveModalSeen)exit(229);Check(config->GetSettings(&v),"live tonal modal result");if(v.tonal.exposure[2]!=(modalKeep?-1:0))exit(230);}
  for(int kind:{1,2})for(bool modalKeep:{false,true}){liveModalKind=kind;liveModalKeep=modalKeep;liveModalSeen=false;auto timer=SetTimer(nullptr,0,10,LiveModalTimer);SendMessageW(dialogs[kind==1?1:3],WM_COMMAND,MAKEWPARAM(kind==1?241:608,BN_CLICKED),0);KillTimer(nullptr,timer);PumpLive();if(!liveModalSeen)exit(235);Check(config->GetSettings(&v),"live extra modal result");if((kind==1&&v.extras.values[0]!=(modalKeep?.25f:-1.f))||(kind==2&&v.blackBars.enabled!=modalKeep))exit(236);}
  SendDlgItemMessageW(dialogs[5],1500,LB_SETCURSEL,0,0);SendMessageW(dialogs[5],WM_COMMAND,MAKEWPARAM(1500,LBN_DBLCLK),0);PumpLive();Check(config->GetSettings(&v),"live Shader toggle");if(!v.userShaders.shaders[0].enabled)exit(231);
  if(v.sdrNits!=180||v.scaling.antiringing!=.35f||v.sharpenStrength!=.65f||std::abs(v.saturation-1.15f)>1e-6)exit(232);
  SendMessageW(host,WM_COMMAND,keep?IDOK:IDCANCEL,0);
  for(int i=0;i<6;i++){Check(pages[i]->Deactivate(),"live deactivate");Check(pages[i]->SetObjects(0,nullptr),"live detach");Check(pages[i]->SetPageSite(nullptr),"live detach site");}
  DestroyWindow(host);Check(config->GetSettings(&v),"live session result");
  if(!keep&&(v.saturation!=baseline.saturation||v.sdrNits!=baseline.sdrNits||v.sharpenStrength!=baseline.sharpenStrength||v.scaling.antiringing!=baseline.scaling.antiringing||ActiveTonal(v.tonal)))exit(233);
  if(keep&&(v.sdrNits!=180||v.tonal.exposure[2]!=-1||v.scaling.antiringing!=.35f))exit(234);
 }
 Check(config->SetSettings(&baseline),"live restore baseline");puts("PASS all six live pages: immediate valid edits, partial numeric input, merge, hidden Apply, all 3 modals live/Cancel, all 9 HDR fields typed with focus, host Cancel rollback and OK commit");
}
int wmain(int argc,wchar_t** argv){
    for(int i=1;i<argc;i++)if(wcscmp(argv[i],L"--startup")==0)startupTest=true;
    for(int i=1;i<argc;i++)if(wcscmp(argv[i],L"--measure")==0)measureTest=true;
    hardware=argc>1&&wcscmp(argv[1],L"--gpu")==0;
    for(int i=1;i<argc;i++){if(wcscmp(argv[i],L"--nv12")==0)textureFormat=DXGI_FORMAT_NV12;if(wcscmp(argv[i],L"--p016")==0)textureFormat=DXGI_FORMAT_P016;}
    for(int i=1;i<argc;i++)if(wcscmp(argv[i],L"--srv")==0)textureBind=D3D11_BIND_SHADER_RESOURCE;
    for(int i=1;i<argc;i++){if(wcscmp(argv[i],L"--flush-race")==0)flushRaceTest=true;if(wcscmp(argv[i],L"--sharpen")==0)sharpenTest=true;if(wcscmp(argv[i],L"--motion")==0)motionTest=true;if(wcscmp(argv[i],L"--interlaced")==0)interlacedTest=true;if(wcscmp(argv[i],L"--bff")==0)bottomFirst=true;if(wcscmp(argv[i],L"--weave")==0)weaveTest=true;if(wcscmp(argv[i],L"--eos")==0)eosTest=true;if(wcscmp(argv[i],L"--yadif")==0)yadifTest=true;if(wcscmp(argv[i],L"--force-bff")==0)forceBffTest=true;}
    setvbuf(stdout,nullptr,_IONBF,0);g_hInst=GetModuleHandleW(nullptr);Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"COM");
    wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);std::wstring directory=executable;directory.resize(directory.find_last_of(L"\\")+1);
    std::wstring dllPath=directory+L"PlaceboRenderer.ax";for(int i=1;i+1<argc;i++)if(wcscmp(argv[i],L"--dll")==0)dllPath=argv[++i];
    HMODULE dll=LoadLibraryExW(dllPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!dll){printf("FAIL load %lu\n",GetLastError());return 1;}
    using Getter=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto getter=reinterpret_cast<Getter>(GetProcAddress(dll,"DllGetClassObject"));ComPtr<IClassFactory> factory;Check(getter(CLSID_PlaceboRenderer,IID_PPV_ARGS(&factory)),"factory");
    {HWND keyWindow=CreateWindowExW(0,L"STATIC",L"Internal shortcut test",WS_POPUP,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);StatisticsKey::Attach(keyWindow);StatisticsKey::Attach(keyWindow);if(StatisticsKey::windows.size()!=1||StatisticsKey::hooks.size()!=1)exit(99);
     MSG key={};key.message=WM_KEYDOWN;key.wParam='J';if(StatisticsKey::Handle(key,false,false,false,keyWindow)||StatisticsKey::Handle(key,true,false,false,nullptr))exit(100);
     if(!StatisticsKey::Handle(key,true,false,false,keyWindow)||key.message!=WM_NULL)exit(101);MSG posted={};if(!PeekMessageW(&posted,keyWindow,StatisticsKey::ToggleMessage,StatisticsKey::ToggleMessage,PM_REMOVE))exit(102);
     key.message=WM_KEYDOWN;key.wParam='J';key.lParam=1LL<<30;if(!StatisticsKey::Handle(key,true,false,false,keyWindow)||PeekMessageW(&posted,keyWindow,StatisticsKey::ToggleMessage,StatisticsKey::ToggleMessage,PM_REMOVE))exit(103);
     StatisticsKey::Detach(keyWindow);if(!StatisticsKey::windows.empty()||!StatisticsKey::hooks.empty())exit(104);DestroyWindow(keyWindow);puts("PASS Ctrl+J routing, focus restriction, repeat suppression and hook cleanup");}
    ComPtr<IBaseFilter> renderer;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&renderer)),"renderer");ComPtr<IPlaceboSettings> config;Check(renderer.As(&config),"settings interface");PlaceboSettings settings;Check(config->GetSettings(&settings),"read settings");if(const char* mixer=getenv("PLACEBO_TEST_MIXER"))strcpy_s(settings.scaling.filters[4].filter,mixer);if(getenv("PLACEBO_TEST_DEBAND")){settings.deband=TRUE;settings.debandGrain=0;}settings.adaptiveSharpen=sharpenTest;settings.smoothMotion=motionTest;settings.autoHdr=FALSE;settings.deinterlacing=yadifTest?2:1;settings.fieldOverride=forceBffTest?2:0;settings.saturation=1.f;Check(config->SetSettings(&settings),"set color without display switching");
    for(int i=1;i<argc;i++)if(!wcscmp(argv[i],L"--live-ui")){TestLiveUI(getter,renderer.Get(),config.Get());return 0;}
    ComPtr<ID3D11DecoderConfiguration> filterDecoder;Check(renderer.As(&filterDecoder),"decoder configuration on filter");ComPtr<ID3D11DecoderTextureConfiguration> filterTextures;Check(renderer.As(&filterTextures),"decoder texture configuration on filter");
    ComPtr<IExFilterConfig> hostConfig;Check(renderer.As(&hostConfig),"public host configuration");int renderType=0;Check(hostConfig->Flt_GetInt("renderType",&renderType),"D3D11 render type");if(renderType!=11)return 13;if(hostConfig->Flt_GetInt("unknown",&renderType)!=E_INVALIDARG)return 14;
    ComPtr<IQualProp> startupQuality;
    if(startupTest){Check(renderer->QueryInterface(IID_IQualProp,reinterpret_cast<void**>(startupQuality.GetAddressOf())),"DirectShow raw quality counter");int raw=0;__int64 sum=0;Check(startupQuality->get_FramesDroppedInRenderer(&raw),"initial raw drops");Check(hostConfig->Flt_GetInt64("placeboStatsDrops",&sum),"initial accumulated drops");printf("STARTUP fresh renderer raw=%d accumulated=%lld\n",raw,sum);if(raw||sum)exit(108);}
    ComPtr<IClassFactory> pageFactory;Check(getter(CLSID_PlaceboSettingsPage,IID_PPV_ARGS(&pageFactory)),"settings factory");{IID oldId={0x9a1392c7,0xa9ea,0x4f3c,{0x8e,0x24,0x77,0x33,0x3c,0x9a,0x81,0xbf}};void* old=nullptr;if(renderer->QueryInterface(oldId,&old)!=E_NOINTERFACE||old)exit(65);}
    {IID oldId={0x37628880,0x632f,0x46e9,{0x9a,0xa9,0xf7,0x8d,0xd0,0x44,0x21,0xe9}};void* old=nullptr;if(renderer->QueryInterface(oldId,&old)!=E_NOINTERFACE||old)exit(95);}
    {IID previousId={0xfae47621,0xc763,0x4fae,{0x99,0xa0,0xc8,0x45,0x9f,0x6b,0x51,0xe8}};void* previous=nullptr;if(renderer->QueryInterface(previousId,&previous)!=E_NOINTERFACE||previous)exit(136);}
    {IID previousId={0x772c1bf7,0xe7ea,0x498c,{0x84,0xe7,0xb7,0xa4,0x75,0x3c,0x76,0xe9}};void* previous=nullptr;if(renderer->QueryInterface(previousId,&previous)!=E_NOINTERFACE||previous)exit(133);}
    {IID previousId={0xb7214b57,0x2da7,0x444a,{0x98,0x05,0xa8,0xcb,0x80,0x2d,0xf5,0x1e}};void* previous=nullptr;if(renderer->QueryInterface(previousId,&previous)!=E_NOINTERFACE||previous)exit(125);}
    {IID oldId={0x082bccb7,0x181b,0x4d07,{0x9c,0x18,0x50,0x31,0x47,0xc4,0x59,0x21}};void* old=nullptr;if(renderer->QueryInterface(oldId,&old)!=E_NOINTERFACE||old)exit(112);}
    {IID oldId={0x1726072a,0xdf7c,0x4be9,{0xb2,0xd7,0xe4,0x49,0xfb,0xb0,0x42,0xab}};void* old=nullptr;if(renderer->QueryInterface(oldId,&old)!=E_NOINTERFACE||old)exit(170);}
    ComPtr<IClassFactory> adjustmentFactory;Check(getter(CLSID_PlaceboAdjustmentPage,IID_PPV_ARGS(&adjustmentFactory)),"Color Adjustment factory");TestPage(adjustmentFactory.Get(),renderer.Get(),config.Get());ComPtr<IClassFactory> scalingFactory;Check(getter(CLSID_PlaceboScalingPage,IID_PPV_ARGS(&scalingFactory)),"Scaling factory");TestScaling(scalingFactory.Get(),renderer.Get(),config.Get());ComPtr<IClassFactory> debandFactory;Check(getter(CLSID_PlaceboProcessingPage,IID_PPV_ARGS(&debandFactory)),"Debanding factory");TestDebanding(debandFactory.Get(),renderer.Get(),config.Get());ComPtr<IClassFactory> processingFactory;Check(getter(CLSID_PlaceboProcessingPage,IID_PPV_ARGS(&processingFactory)),"processing factory");ComPtr<IClassFactory> colorFactory;Check(getter(CLSID_PlaceboColorPage,IID_PPV_ARGS(&colorFactory)),"color factory");ComPtr<IClassFactory> hdrFactory;Check(getter(CLSID_PlaceboHdrPage,IID_PPV_ARGS(&hdrFactory)),"HDR details factory");ComPtr<ISpecifyPropertyPages> listedPages;Check(renderer.As(&listedPages),"listed property pages");CAUUID listed={};Check(listedPages->GetPages(&listed),"page list");if(listed.cElems!=6)exit(55);for(ULONG i=0;i<listed.cElems;i++)if(listed.pElems[i]==CLSID_PlaceboDebandingPage||listed.pElems[i]==CLSID_PlaceboSettingsPage||listed.pElems[i]==CLSID_PlaceboHdrPage)exit(150);CoTaskMemFree(listed.pElems);TestProcessing(pageFactory.Get(),processingFactory.Get(),colorFactory.Get(),hdrFactory.Get(),renderer.Get(),config.Get());
    ComPtr<IClassFactory> mappingFactory;Check(getter(CLSID_PlaceboMappingPage,IID_PPV_ARGS(&mappingFactory)),"mapping factory");TestMapping(mappingFactory.Get(),hdrFactory.Get(),renderer.Get(),config.Get());
    ComPtr<IClassFactory> shaderFactory;Check(getter(CLSID_PlaceboEffectsPage,IID_PPV_ARGS(&shaderFactory)),"shader page factory");TestShaderPage(shaderFactory.Get(),renderer.Get(),config.Get());
    ComPtr<IVideoWindow> window;Check(renderer.As(&window),"video window");
    ComPtr<IGraphBuilder> graph;Check(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&graph)),"graph");Check(graph->AddFilter(renderer.Get(),L"libplacebo"),"add renderer");
    HRESULT hr=S_OK;auto rawSource=new PatternSource(&hr);Check(hr,"source create");ComPtr<IBaseFilter> source;Check(rawSource->NonDelegatingQueryInterface(IID_PPV_ARGS(&source)),"source interface");Check(graph->AddFilter(source.Get(),L"P010"),"add source");
    ComPtr<IEnumPins> list;ComPtr<IPin> input;Check(renderer->EnumPins(&list),"input list");Check(list->Next(1,&input,nullptr),"input");Check(graph->ConnectDirect(rawSource->GetPin(0),input.Get(),nullptr),"connect P010");
    Check(window->put_AutoShow(OAFALSE),"disable autoshow");Check(window->put_Visible(OAFALSE),"hide test window");Check(window->SetWindowPosition(0,0,128,128),"video size");if(measureTest){Check(window->SetWindowPosition(40,40,640,360),"measurement window size");Check(window->put_Visible(OATRUE),"show measurement test window");}ComPtr<IMediaControl> control;Check(graph.As(&control),"control");if(eosTest){ComPtr<IMediaFilter> media;Check(graph.As(&media),"clock control");Check(media->SetSyncSource(nullptr),"unclocked exact-count test");}Check(control->Run(),"run");
    PlaceboStatus status;const DWORD start=GetTickCount();do{Sleep(20);MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Check(config->GetStatus(&status),"status");}while(status.rendered<5&&SUCCEEDED(sourceFailure.load())&&GetTickCount()-start<10000);
    if(startupTest){
        __int64 cached=0,objects=0;Check(hostConfig->Flt_GetInt64("placeboShaderCacheLoaded",&cached),"cache loaded");Check(hostConfig->Flt_GetInt64("placeboShaderCacheObjects",&objects),"cache objects");printf("STARTUP shader cache loaded=%lld objects=%lld\n",cached,objects);
        int raw=0;__int64 sum=0,confirmed=0;Check(startupQuality->get_FramesDroppedInRenderer(&raw),"startup raw drops");Check(hostConfig->Flt_GetInt64("placeboStatsDrops",&sum),"startup accumulated drops");Check(hostConfig->Flt_GetInt64("placeboStatsConfirmed",&confirmed),"startup display feedback");
        printf("STARTUP after %lu ms, video window intentionally hidden=%d: renderer outputs=%llu DXGI confirmed=%lld raw drops=%d accumulated=%lld\n",GetTickCount()-start,!measureTest,status.rendered,confirmed,raw,sum);
        // Pause freezes the scheduler, making raw and accumulated snapshots
        // comparable without racing another output on the source thread.
        Check(control->Pause(),"startup freeze");OAFilterState frozen;Check(control->GetState(3000,&frozen),"startup frozen state");Check(startupQuality->get_FramesDroppedInRenderer(&raw),"frozen raw drops");Check(hostConfig->Flt_GetInt64("placeboStatsDrops",&sum),"frozen accumulated drops");
        double cpuMs=0,gpuMs=0;Check(hostConfig->Flt_GetDouble("placeboStatsRenderCpuMs",&cpuMs),"startup CPU render time");auto gpuResult=hostConfig->Flt_GetDouble("placeboStatsRenderGpuMs",&gpuMs);
        printf("STARTUP frozen raw=%d accumulated=%lld difference=%lld render CPU average=%.3fms GPU average=%.3fms GPU result=%08lX\n",raw,sum,sum-raw,cpuMs,gpuMs,static_cast<unsigned long>(gpuResult));if(sum>raw)exit(109);
        Check(control->Run(),"startup resume and capture final raw epoch");Check(hostConfig->Flt_GetInt64("placeboStatsDrops",&sum),"retained startup total");if(sum<raw)exit(110);printf("STARTUP prior raw=%d retained after restart=%lld\n",raw,sum);
    }
    if(status.rendered<5){ComPtr<IMediaEvent> events;if(SUCCEEDED(graph.As(&events))){long code=0;LONG_PTR a=0,b=0;while(events->GetEvent(&code,&a,&b,0)==S_OK){printf("Graph event %ld, parameter %08llX\n",code,static_cast<unsigned long long>(a));events->FreeEventParams(code,a,b);}}printf("FAIL no rendered frames, count=%llu fields=%llu result %08lX\n",status.rendered,status.deinterlacedFields,static_cast<unsigned long>(status.lastResult));control->Stop();return 2;}
    if(hardware&&status.hardwareFrames<5){printf("FAIL D3D11 sample branch not used\n");control->Stop();return 8;}
    if(!eosTest&&interlacedTest&&(!weaveTest||forceBffTest)&&status.deinterlacedFields<5){printf("FAIL timed stream bypassed deinterlacing\n");return 19;}
    if((!interlacedTest||(weaveTest&&!forceBffTest))&&status.deinterlacedFields){printf("FAIL progressive samples deinterlaced\n");return 20;}
    if(eosTest){ComPtr<IMediaEvent> events;Check(graph.As(&events),"EOS events");long event=0;Check(events->WaitForCompletion(5000,&event),"complete deinterlaced stream");Check(config->GetStatus(&status),"EOS final count");PlaceboDiagnostics motion;Check(renderer.As(&filterDecoder),"renderer still valid");ComPtr<IPlaceboDiagnostics> motionInfo;Check(renderer.As(&motionInfo),"motion diagnostics");Check(motionInfo->GetDiagnostics(&motion),"final motion diagnostics");const uint64_t expected=motionTest&&motion.displayHz>0?static_cast<uint64_t>(std::ceil(1999998.*motion.displayHz/1e7-1e-5)):interlacedTest&&(!weaveTest||forceBffTest)?12:6;if(status.rendered!=expected||status.deinterlacedFields!=((interlacedTest&&(!weaveTest||forceBffTest))?expected:0)||(motionTest&&!status.mixedFrames)){printf("FAIL final fields lost/duplicated: frames=%llu fields=%llu expected=%llu\n",status.rendered,status.deinterlacedFields,expected);return 18;}if(motionTest)printf("PASS Smooth Motion %llu mixed outputs, display %.6f Hz, final timeline and EOS\n",status.mixedFrames,motion.displayHz);Check(control->Stop(),"EOS stop");printf("PASS exact EOS drain: %llu outputs, fields=%llu, %s %s\n",status.rendered,status.deinterlacedFields,bottomFirst?"BFF":"TFF",yadifTest?"Yadif":"Bwdif");return 0;}
    if(status.masteringMaxNits!=1000||abs(status.masteringMinNits-.0001f)>.000001f||status.maxCLL!=1200||status.maxFALL!=400){printf("FAIL allocated HDR side data did not reach renderer: %.1f %.6f %.1f %.1f\n",status.masteringMaxNits,status.masteringMinNits,status.maxCLL,status.maxFALL);control->Stop();return 15;}
    ComPtr<IPlaceboDiagnostics> diagnosticConfig;Check(renderer.As(&diagnosticConfig),"diagnostics interface");PlaceboDiagnostics actual;Check(diagnosticConfig->GetDiagnostics(&actual),"diagnostics snapshot");if(sharpenTest&&actual.sharpenState!=1)exit(53);if(!actual.haveVideo||actual.width!=64||actual.height!=64||!!actual.directGpu!=hardware||actual.frames==0||actual.sourcePeak!=1000)exit(30);actual.size=0;if(SUCCEEDED(diagnosticConfig->GetDiagnostics(&actual)))exit(31);printf("PASS diagnostics: actual input size, GPU/CPU path, source metadata, frame count and buffer size validation\n");
    printf("PASS allocator HDR metadata write/read, recycled samples cleared, mastering 1000 nits and MaxCLL/MaxFALL\n");
    if(hardware)printf("PASS decoder device negotiation, array texture slice 1 (bind %u), GPU sample branch (%llu frames)\n",textureBind,status.hardwareFrames);
    if(sharpenTest){for(bool enabled:{false,true}){
        settings.adaptiveSharpen=enabled;Check(config->SetSettings(&settings),"live sharpen toggle");const DWORD deadline=GetTickCount();bool matched=false;
        do{Sleep(10);MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}actual={};Check(diagnosticConfig->GetDiagnostics(&actual),"live sharpen diagnostics");matched=actual.sharpenState==(enabled?1:0)&&actual.haveVideo;}while(!matched&&GetTickCount()-deadline<5000);
        if(!matched){puts("FAIL live shader toggle stalled or not applied");return 54;}
    }puts("PASS live Adaptive Sharpen off/on without stopping playback");}
    if(motionTest){for(bool enabled:{false,true}){
        settings.smoothMotion=enabled;Check(config->SetSettings(&settings),"live smooth motion toggle");const DWORD deadline=GetTickCount();bool matched=false;
        do{Sleep(10);MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}actual={};Check(diagnosticConfig->GetDiagnostics(&actual),"live motion diagnostics");matched=!!actual.smoothMotion==enabled&&actual.haveVideo;}while(!matched&&GetTickCount()-deadline<5000);
        if(!matched){puts("FAIL live smooth motion toggle stalled or not applied");return 40;}
    }puts("PASS live Smooth Motion off/on without stopping playback");}
    if(measureTest){const auto deadline=GetTickCount64()+5000;__int64 confirmed=0;
        do{MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);Check(hostConfig->Flt_GetInt64("placeboStatsConfirmed",&confirmed),"confirmed presentation feedback");}while(confirmed<20&&GetTickCount64()<deadline);
        __int64 glitches=0,repeats=0;double gpuMs=0;Check(hostConfig->Flt_GetInt64("placeboStatsGlitches",&glitches),"clocked DXGI glitch counter");Check(hostConfig->Flt_GetInt64("placeboStatsRepeats",&repeats),"clocked DXGI repeat counter");Check(hostConfig->Flt_GetDouble("placeboStatsRenderGpuMs",&gpuMs),"asynchronous render GPU measurement");if(confirmed<20||gpuMs<=0)exit(107);
        printf("PASS actual DirectShow reference clock -> DXGI: %lld confirmed, repeats=%lld glitches=%lld GPU=%.3fms\n",confirmed,repeats,glitches,gpuMs);
    }
    double renderMs=0,presentMs=0;Check(hostConfig->Flt_GetDouble("placeboStatsRenderCpuMs",&renderMs),"render CPU measurement");Check(hostConfig->Flt_GetDouble("placeboStatsPresentCpuMs",&presentMs),"present CPU measurement");if(renderMs<0||presentMs<0)exit(105);
    Check(control->Pause(),"pause");OAFilterState paused;Check(control->GetState(3000,&paused),"paused state");
    __int64 timingCount=0;Check(hostConfig->Flt_GetInt64("placeboStatsTimingActive",&timingCount),"paused timing state");if(timingCount)exit(106);if(measureTest)Check(hostConfig->Flt_GetInt64("placeboStatsGlitches",&timingCount),"paused counters remain readable");puts("PASS pause resets correlation while retaining measured error totals");
    Check(renderer.As(&hostConfig),"host statistics interface");bool statsEnabled=true;Check(hostConfig->Flt_GetBool("statsEnable",&statsEnabled),"stats default");if(statsEnabled)exit(96);Check(hostConfig->Flt_SetBool("statsEnable",true),"enable statistics");Check(hostConfig->Flt_GetBool("statsEnable",&statsEnabled),"stats enabled readback");if(!statsEnabled)exit(97);std::vector<BYTE> statsPixels(1280*1080*4);Check(config->ReadPixels(1280,1080,statsPixels.data(),statsPixels.size()),"statistics with host subtitles");Check(hostConfig->Flt_SetBool("statsEnable",false),"disable statistics");Check(hostConfig->Flt_GetBool("statsEnable",&statsEnabled),"stats disabled readback");if(statsEnabled)exit(98);
    puts("PASS host statsEnable toggle and statistics GPU composition with subtitle callback");
    std::vector<BYTE> color(64*64*4),gray(color.size());Check(config->ReadPixels(64,64,color.data(),color.size()),"filter color pixels");{auto shaderSettings=settings;shaderSettings.userShaders.shaders[0].enabled=1;Check(config->SetSettings(&shaderSettings),"packaged AMD CAS enable");std::vector<BYTE> shaderPixels(color.size());Check(config->ReadPixels(64,64,shaderPixels.data(),shaderPixels.size()),"packaged custom shader pixels");long long delta=0;for(size_t i=0;i<color.size();i++)if(i%4!=3)delta+=std::abs(int(color[i])-int(shaderPixels[i]));if(delta<10)exit(211);Check(config->SetSettings(&settings),"packaged custom shader disable");Check(config->ReadPixels(64,64,shaderPixels.data(),shaderPixels.size()),"packaged all off pixels");long long restored=0;for(size_t i=0;i<color.size();i++)restored+=std::abs(int(color[i])-int(shaderPixels[i]));if(restored>10)exit(212);printf("PASS packaged custom shader GPU effect %lld and disable restores pixels\n",delta);}
    settings.saturation=0.f;Check(config->SetSettings(&settings),"live gray setting");Check(config->ReadPixels(64,64,gray.data(),gray.size()),"filter gray pixels");
    printf("Filter GPU output: color spread %d, gray spread %d, frames %llu\n",Spread(color),Spread(gray),status.rendered);if(Spread(color)<40||Spread(gray)>3){control->Stop();return 3;}
    ComPtr<ISubRender11> sub;Check(renderer.As(&sub),"subtitle interface");ComPtr<SubtitleProbe> probe;probe.Attach(new SubtitleProbe);
    Check(sub->SetCallback11(probe.Get()),"attach subtitle callback");Check(config->ReadPixels(64,64,color.data(),color.size()),"subtitle pixels");size_t center=(32*64+32)*4;
    if(color[center]>2||color[center+1]<253||color[center+2]>2||!probe->attached||!probe->drawn){printf("FAIL subtitle callback/output\n");return 5;}
    Check(sub->SetCallback11(nullptr),"detach subtitles");if(!probe->detached){printf("FAIL subtitle device not released\n");return 6;}
    Check(config->ReadPixels(64,64,color.data(),color.size()),"detached subtitle pixels");if(Spread(color)>3){printf("FAIL detached subtitles persisted\n");return 7;}
    printf("PASS DirectShow subtitle callback device, composed pixels and detach\n");
    if(flushRaceTest){
        if(!hardware){puts("FAIL flush race requires GPU sample wrapper");return 120;}
        sampleTimeEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);sampleTimeRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        HANDLE flushStarted=CreateEventW(nullptr,TRUE,FALSE,nullptr),flushDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Check(control->Run(),"race resume");
        // Arm after Run: the base scheduler may read the paused sample on the
        // control thread during Run; we need the next streaming Receive.
        blockSampleTime=true;
        if(WaitForSingleObject(sampleTimeEntered,3000)!=WAIT_OBJECT_0){puts("FAIL no sample entered race gate");return 121;}
        HRESULT flushed=E_PENDING;
        std::thread flush([&]{SetEvent(flushStarted);flushed=input->BeginFlush();SetEvent(flushDone);});
        WaitForSingleObject(flushStarted,1000);
        const bool returnedEarly=WaitForSingleObject(flushDone,150)==WAIT_OBJECT_0;
        SetEvent(sampleTimeRelease);
        if(WaitForSingleObject(flushDone,5000)!=WAIT_OBJECT_0){puts("FAIL flush deadlocked with active input");ExitProcess(122);}
        flush.join();Check(flushed,"concurrent begin flush");
        actual={};Check(diagnosticConfig->GetDiagnostics(&actual),"race flushed diagnostics");
        if(returnedEarly||actual.haveVideo){printf("FAIL flush returned before input completed=%d stale video=%d\n",returnedEarly,actual.haveVideo);return 123;}
        Check(input->EndFlush(),"race end flush");Check(input->NewSegment(0,30000000,1.),"race new segment");
        Check(control->Stop(),"race stop");
        for(HANDLE event:{sampleTimeEntered,sampleTimeRelease,flushStarted,flushDone})CloseHandle(event);
        sampleTimeEntered=sampleTimeRelease=nullptr;
        puts("PASS concurrent flush waits for temporal input, clears stale frames and completes without deadlock");
    }
    Check(control->Stop(),"stop");Check(input->BeginFlush(),"begin flush");Check(input->NewSegment(420000000,450000000,1.),"new segment");Check(input->EndFlush(),"end flush");Check(control->Run(),"restart");Sleep(250);Check(control->Stop(),"final stop");actual={};Check(diagnosticConfig->GetDiagnostics(&actual),"diagnostics after stop");if(actual.haveVideo)exit(32);
    // Reuse the same renderer, replacing decoder format and device rather than
    // constructing a fresh filter that could hide stale allocator/texture state.
    for(int phase=0;phase<3;phase++){
        Check(graph->Disconnect(rawSource->GetPin(0)),"disconnect decoder");Check(graph->Disconnect(input.Get()),"disconnect renderer");
        ComPtr<IMemInputPin> mem;Check(input.As(&mem),"input allocator interface");ComPtr<IMemAllocator> available;Check(mem->GetAllocator(&available),"CPU allocator restored after disconnect");available.Reset();
        hardware=phase!=0;textureFormat=phase==1?DXGI_FORMAT_NV12:DXGI_FORMAT_P010;
        Check(graph->ConnectDirect(rawSource->GetPin(0),input.Get(),nullptr),"reconnect changed decoder");
        Check(config->GetStatus(&status),"reconnect baseline");const auto beforeFrames=status.rendered,beforeHardware=status.hardwareFrames;
        Check(control->Run(),"run reconnected decoder");const DWORD deadline=GetTickCount();
        do{Sleep(10);MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Check(config->GetStatus(&status),"reconnect status");}while(status.rendered<beforeFrames+5&&GetTickCount()-deadline<5000);
        Check(control->Pause(),"pause reconnected decoder");Check(control->GetState(3000,&paused),"reconnected paused state");
        if(status.rendered<beforeFrames+5||FAILED(status.lastResult)||(hardware?status.hardwareFrames<beforeHardware+5:status.hardwareFrames!=beforeHardware)){printf("FAIL reconnect did not use requested CPU/GPU path\n");return 16;}
        Check(config->ReadPixels(64,64,gray.data(),gray.size()),"reconnected image pixels");if(Spread(gray)>3||gray[0]<30){printf("FAIL stale/empty image after decoder change\n");return 17;}
        actual={};Check(diagnosticConfig->GetDiagnostics(&actual),"diagnostics after decoder change");if(!actual.haveVideo||!!actual.directGpu!=hardware||strcmp(actual.pixelFormat,phase==1?"NV12":"P010"))exit(33);
        Check(control->Stop(),"stop reconnected decoder");printf("PASS same renderer reconnect: %s %s, new frames and pixels verified\n",hardware?"GPU":"CPU",textureFormat==DXGI_FORMAT_NV12?"NV12":"P010");
    }
    PlaceboSettings persist;Check(config->GetSettings(&persist),"settings before persistence check");persist.allowDelayedPeak=TRUE;persist.kneeAdaptation=.7f;persist.splineContrast=.8f;persist.kneeMinimum=.2f;persist.kneeMaximum=.9f;persist.blackCutoff=2;persist.perceptualStrength=.6f;persist.softclipKnee=.65f;persist.showClipping=TRUE;persist.maximumFramesInFlight=7;persist.toneMapping=2;persist.hdrNits=600;persist.antiringing=.6f;persist.upscalerBlur=.9f;persist.downscalerBlur=1.2f;persist.debandIterations=3;persist.debandThreshold=5;persist.debandRadius=24;persist.debandGrain=1;persist.ditherMethod=2;persist.ditherTemporal=TRUE;persist.chromaScaler=4;persist.adaptiveSharpen=TRUE;persist.sharpenStrength=.75f;persist.peakDetect=FALSE;persist.peakSmoothing=35;persist.sceneLow=.5f;persist.sceneHigh=4;persist.peakPercentile=99.995f;persist.contrastRecovery=.4f;persist.contrastSmoothness=5;persist.gamutMapping=3;persist.outputLevels=2;persist.displayDepth=12;persist.outputFormat=3;Check(config->SetSettings(&persist),"save advanced settings");
    ComPtr<IBaseFilter> second;ComPtr<IPlaceboSettings> secondConfig;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&second)),"second renderer");Check(second.As(&secondConfig),"second settings");PlaceboSettings readback;Check(secondConfig->GetSettings(&readback),"saved readback");if(!readback.allowDelayedPeak||readback.maximumFramesInFlight!=7||readback.kneeAdaptation!=.7f||readback.splineContrast!=.8f||readback.kneeMinimum!=.2f||readback.kneeMaximum!=.9f||readback.blackCutoff!=2||readback.perceptualStrength!=.6f||readback.softclipKnee!=.65f||!readback.showClipping||readback.adaptiveSharpen!=TRUE||readback.sharpenStrength!=.75f||readback.smoothMotion!=persist.smoothMotion||readback.saturation!=0.f||readback.toneMapping!=2||readback.hdrNits!=600||readback.antiringing!=.6f||readback.upscalerBlur!=.9f||readback.downscalerBlur!=1.2f||readback.debandIterations!=3||readback.debandThreshold!=5||readback.debandRadius!=24||readback.debandGrain!=1||readback.ditherMethod!=2||!readback.ditherTemporal||readback.chromaScaler!=4||readback.outputFormat!=3||readback.outputLevels!=2||readback.displayDepth!=12||readback.peakDetect||readback.peakSmoothing!=35||readback.sceneLow!=.5f||readback.sceneHigh!=4||readback.peakPercentile!=99.995f||readback.contrastRecovery!=.4f||readback.contrastSmoothness!=5||readback.gamutMapping!=3){printf("FAIL saved advanced settings not read\n");return 4;}
    printf("PASS DirectShow %s, clocked playback, pause, measured live setting, flush/new segment, restart, stop, saved settings\n",textureFormat==DXGI_FORMAT_NV12?"NV12":textureFormat==DXGI_FORMAT_P016?"P016":"P010");return 0;
}

