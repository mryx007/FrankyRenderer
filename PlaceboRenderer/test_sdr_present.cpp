#include "Backend.h"
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
static void Check(bool ok,const char* text){if(!ok){printf("FAIL %s\n",text);exit(1);}}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);WNDCLASSW wc={};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"FrankySdrPresentTest";RegisterClassW(&wc);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"Internal HDR to SDR presentation test",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);Check(window!=nullptr,"hidden test window");
    PlaceboBackend b;RenderSettings s;s.autoHdr=false;s.hdrMode=2;s.outputFormat=2;s.ditherMethod=3;s.peakDetect=false;s.colorAdjustment=false;
    Check(SUCCEEDED(b.SetSettings(s)),"settings");Check(SUCCEEDED(b.SetWindow(window)),"window");
    VideoFrame f;f.width=f.height=64;f.stride=128;f.format=PixelFormat::P010;f.matrix=PL_COLOR_SYSTEM_BT_2020_NC;f.color=pl_color_space_hdr10;f.color.hdr.max_luma=4000;
    std::vector<uint16_t> data(64*64*3/2,512<<6);for(int y=0;y<64;y++)for(int x=0;x<64;x++)data[y*64+x]=static_cast<uint16_t>((64+x*876/63)<<6);
    Check(SUCCEEDED(b.Upload(f,reinterpret_cast<uint8_t*>(data.data()),data.size()*2)),"HDR source");
    pl_color_transfer transfer=PL_COLOR_TRC_UNKNOWN;double average=0;
    b.SetPresentColorProbeForTest([&](const pl_color_space& c){transfer=c.transfer;});
    b.SetPresentProbeForTest([&](ID3D11Texture2D* texture){
        ComPtr<ID3D11Device> device;texture->GetDevice(&device);ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        D3D11_TEXTURE2D_DESC d;texture->GetDesc(&d);const auto format=d.Format;d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;Check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"capture texture");context->CopyResource(staging.Get(),texture);D3D11_MAPPED_SUBRESOURCE map={};Check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"capture pixels");
        double total=0;for(UINT y=0;y<d.Height;y++)for(UINT x=0;x<d.Width;x++){auto row=static_cast<const uint8_t*>(map.pData)+y*map.RowPitch;if(format==DXGI_FORMAT_R10G10B10A2_UNORM)total+=(reinterpret_cast<const uint32_t*>(row)[x]&1023)/1023.;else total+=row[x*4]/255.;}average=total/(d.Width*d.Height);context->Unmap(staging.Get(),0);
    });
    double previous=0;float previousNits=0;
    for(float n:{190.f,199.f,200.f,202.f,203.f,204.f,210.f,220.f,250.f,300.f,400.f,1000.f}){
        s.sdrNits=n;Check(SUCCEEDED(b.SetSettings(s)),"change target peak");Check(SUCCEEDED(b.Present()),"present HDR to SDR");printf("target %.0f transfer %d average %.6f\n",n,int(transfer),average);
        Check(!pl_color_transfer_is_hdr(transfer),"SDR presentation never switches to PQ/HLG across 203 nits");
        if(previousNits){Check(average<=previous+.001,"SDR output brightness changes continuously downward");if(n>=210)Check(average<previous-.0001,"target settings above 210 still affect output");}
        previous=average;previousNits=n;
    }
    b.SetWindow(nullptr);DestroyWindow(window);puts("PASS actual 10-bit swapchain: SDR transfer across 203 nits, continuous HDR-to-SDR output through 1000 nits");
}
