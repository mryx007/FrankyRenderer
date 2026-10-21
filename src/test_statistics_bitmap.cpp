#define NOMINMAX
#include "StatisticsBitmap.h"
#include <libplacebo/d3d11.h>
#include <cstdio>
#include <cstdlib>

static void Check(bool ok,const char* message){if(!ok){printf("FAIL %s\n",message);exit(1);}}
static std::optional<StatisticsRasterizer::Image> Wait(StatisticsRasterizer& raster,int viewWidth){
    const auto until=GetTickCount64()+3000;
    while(GetTickCount64()<until){auto image=raster.Take();if(image&&image->viewWidth==viewWidth)return image;Sleep(1);}return {};
}
static unsigned notifications=0;
static LRESULT CALLBACK WindowProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==StatisticsRasterizer::ReadyMessage){++notifications;return 0;}return DefWindowProcW(window,message,w,l);
}
int main(){
    setvbuf(stdout,nullptr,_IONBF,0);
    const DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    WNDCLASSW wc={};wc.lpfnWndProc=WindowProcedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"FrankyStatisticsTest";RegisterClassW(&wc);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);Check(window!=nullptr,"notification window");
    {
        StatisticsRasterizer raster;
        Check(raster.Submit(L"display 120 Hz\nwhite text",1280,1080,window),"queue actual GDI text");
        auto image=Wait(raster,1280);Check(image.has_value(),"background rasterization completed");
        Check(image->width<=1264&&image->height<=1080&&image->pixels.size()==size_t(image->width)*image->height*4,"bounded image dimensions");
        Check(image->pixels[0]==0&&image->pixels[3]==150,"unchanged translucent background");
        bool glyph=false;for(size_t i=0;i<image->pixels.size();i+=4)if(image->pixels[i]>200&&image->pixels[i+3]>200)glyph=true;
        Check(glyph,"white antialiased glyph pixels");
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))DispatchMessageW(&msg);Check(notifications>0,"completion wakes a paused window");
        std::wstring large;for(int i=0;i<2000;i++)large+=L"obsolete statistics\n";
        Check(raster.Submit(large,3840,2160),"queue old viewport");raster.Reset();Check(raster.Submit(L"latest",320,200),"queue after reset");
        image=Wait(raster,320);Check(image&&image->viewHeight==200,"reset rejects stale worker results");
        for(int i=0;i<100;i++)Check(raster.Submit(L"replaced",800+i,600),"replace pending work");
        Check(raster.Submit(L"latest",777,480),"latest request");Check(Wait(raster,777).has_value(),"latest request survives queue coalescing");
        raster.Stop();Check(!raster.Submit(L"stopped",1280,1080),"no work after shutdown");
    }
    puts("PASS worker pixels, paused notification, reset, bounded queue and shutdown");
    auto params=pl_d3d11_default_params;auto d3d=pl_d3d11_create(nullptr,&params);Check(d3d!=nullptr,"D3D11 device");
    {
        StatisticsBitmap bitmap;
        auto update=[&](int w,int h){const auto until=GetTickCount64()+3000;while(GetTickCount64()<until){if(bitmap.Update(d3d->gpu,L"statistics",w,h))return true;Sleep(1);}return false;};
        Check(update(3840,2160),"actual GPU upload of completed image");pl_overlay overlay={};pl_overlay_part part={};bitmap.Describe(overlay,part);
        Check(overlay.tex&&overlay.tex->params.w==int(part.src.x1)&&part.dst.x0==16,"GPU texture dimensions and left margin");
        Check(!bitmap.Update(d3d->gpu,L"statistics",640,360),"old viewport is not displayed after resize");Check(update(640,360),"replacement viewport uploaded");
        bitmap.Reset();Check(update(1280,720),"GPU reset supports another image");
    }
    pl_d3d11_destroy(&d3d);DestroyWindow(window);
    const DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);Check(after<=before,"no GDI handles leaked");
    puts("PASS D3D11 upload, resize, device reset and GDI cleanup");
}
