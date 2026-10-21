#pragma once
#include <windows.h>
#include <libplacebo/gpu.h>
#include <libplacebo/renderer.h>
#include <string>
#include <vector>
#include <algorithm>

// Own text bitmap, composed as an independent sRGB overlay by libplacebo.
// No host subtitle surface is modified. White glyphs have coverage alpha.
class StatisticsBitmap {
    pl_gpu gpu=nullptr;pl_tex texture=nullptr;std::wstring last;int width=0,height=0,lastViewWidth=0,lastViewHeight=0;
public:
    ~StatisticsBitmap(){Reset();}
    void Reset(){if(texture)pl_tex_destroy(gpu,&texture);gpu=nullptr;last.clear();width=height=lastViewWidth=lastViewHeight=0;}
    bool Update(pl_gpu nextGpu,const std::wstring& text,int viewWidth,int viewHeight){
        if(gpu!=nextGpu)Reset();gpu=nextGpu;
        const int lines=1+static_cast<int>(std::count(text.begin(),text.end(),L'\n'));
        int fontHeight=std::clamp(viewHeight/45,10,24);
        // Leave a third of the font size between lines. Reduce text size only
        // when the complete panel would otherwise exceed the viewport.
        while(fontHeight>10&&lines*(fontHeight+std::max(4,fontHeight/3))>viewHeight-16)--fontHeight;
        const int lineHeight=fontHeight+std::max(4,fontHeight/3);
        const int h=std::min(viewHeight,lines*lineHeight+16);
        if(viewWidth<=16||h<=0)return false;
        if(texture&&text==last&&lastViewWidth==viewWidth&&lastViewHeight==viewHeight)return true;
        HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;
        HFONT font=CreateFontW(-fontHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!font){DeleteDC(dc);return false;}auto oldFont=SelectObject(dc,font);
        int textWidth=0;
        for(size_t at=0;at<text.size();){auto end=text.find(L'\n',at);if(end==std::wstring::npos)end=text.size();SIZE extent={};
            if(!GetTextExtentPoint32W(dc,text.data()+at,static_cast<int>(end-at),&extent)){SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);return false;}
            textWidth=std::max(textWidth,static_cast<int>(extent.cx));at=end+1;
        }
        const int w=std::min(viewWidth-16,textWidth+16);
        BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* data=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
        if(!bitmap){SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);return false;}
        auto oldBitmap=SelectObject(dc,bitmap);memset(data,0,static_cast<size_t>(w)*h*4);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        size_t pos=0;int y=8;
        while(pos<text.size()&&y<h){auto end=text.find(L'\n',pos);if(end==std::wstring::npos)end=text.size();TextOutW(dc,8,y,text.data()+pos,static_cast<int>(end-pos));y+=lineHeight;pos=end+1;}
        GdiFlush();auto bytes=static_cast<unsigned char*>(data);
        // A translucent black panel keeps the text readable on bright video.
        for(size_t i=0;i<static_cast<size_t>(w)*h;i++){const unsigned coverage=std::max({bytes[i*4],bytes[i*4+1],bytes[i*4+2]});unsigned alpha=150+coverage*105/255;unsigned color=alpha?coverage*255/alpha:0;bytes[i*4]=bytes[i*4+1]=bytes[i*4+2]=static_cast<unsigned char>(color);bytes[i*4+3]=static_cast<unsigned char>(alpha);}
        pl_tex_params params={};params.w=w;params.h=h;params.format=pl_find_named_fmt(gpu,"rgba8");params.sampleable=true;params.host_writable=true;
        bool ok=params.format&&pl_tex_recreate(gpu,&texture,&params);
        if(ok){pl_tex_transfer_params upload={};upload.tex=texture;upload.ptr=data;ok=pl_tex_upload(gpu,&upload);}
        SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);
        if(ok){last=text;width=w;height=h;lastViewWidth=viewWidth;lastViewHeight=viewHeight;}return ok;
    }
    void Describe(pl_overlay& overlay,pl_overlay_part& part){part.src={0,0,float(width),float(height)};part.dst={16,0,float(width+16),float(height)};overlay={};overlay.tex=texture;overlay.mode=PL_OVERLAY_NORMAL;overlay.coords=PL_OVERLAY_COORDS_DST_FRAME;overlay.repr.sys=PL_COLOR_SYSTEM_RGB;overlay.repr.levels=PL_COLOR_LEVELS_FULL;overlay.repr.alpha=PL_ALPHA_INDEPENDENT;overlay.color=pl_color_space_srgb;overlay.parts=&part;overlay.num_parts=1;}
};
