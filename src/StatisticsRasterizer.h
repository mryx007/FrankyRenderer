#pragma once
#include <windows.h>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Keep at most one pending request and one completed image. All CPU text
// drawing runs outside the video thread; this worker never touches D3D11.
class StatisticsRasterizer {
public:
    static constexpr UINT ReadyMessage=WM_APP+0x392;
    struct Image {std::vector<unsigned char> pixels;int width=0,height=0,viewWidth=0,viewHeight=0;};
private:
    struct Request {std::wstring text;int width,height;uint64_t generation;HWND window;};
    std::mutex mutex;std::condition_variable wake;std::thread worker;
    std::optional<Request> pending;std::optional<Image> ready;
    uint64_t generation=0;bool stopping=false;
    static std::optional<Image> Draw(const Request& request){
        const auto& text=request.text;const int viewWidth=request.width,viewHeight=request.height;
        if(viewWidth<=16||viewHeight<=0)return {};
        const int lines=1+static_cast<int>(std::count(text.begin(),text.end(),L'\n'));
        int fontHeight=std::clamp(viewHeight/45,10,24);
        while(fontHeight>10&&lines*(fontHeight+std::max(4,fontHeight/3))>viewHeight-16)--fontHeight;
        const int lineHeight=fontHeight+std::max(4,fontHeight/3),h=std::min(viewHeight,lines*lineHeight+16);
        HDC dc=CreateCompatibleDC(nullptr);if(!dc)return {};
        HFONT font=CreateFontW(-fontHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!font){DeleteDC(dc);return {};}auto oldFont=SelectObject(dc,font);
        int textWidth=0;
        for(size_t at=0;at<text.size();){auto end=text.find(L'\n',at);if(end==std::wstring::npos)end=text.size();SIZE extent={};
            if(!GetTextExtentPoint32W(dc,text.data()+at,static_cast<int>(end-at),&extent)){SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);return {};}
            textWidth=std::max(textWidth,static_cast<int>(extent.cx));at=end+1;
        }
        const int w=std::min(viewWidth-16,textWidth+16);
        Image image;image.width=w;image.height=h;image.viewWidth=viewWidth;image.viewHeight=viewHeight;
        try{image.pixels.resize(static_cast<size_t>(w)*h*4);}catch(...){SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);throw;}
        BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* data=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
        if(!bitmap){SelectObject(dc,oldFont);DeleteObject(font);DeleteDC(dc);return {};}
        auto oldBitmap=SelectObject(dc,bitmap);memset(data,0,image.pixels.size());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
        size_t pos=0;int y=8;
        while(pos<text.size()&&y<h){auto end=text.find(L'\n',pos);if(end==std::wstring::npos)end=text.size();TextOutW(dc,8,y,text.data()+pos,static_cast<int>(end-pos));y+=lineHeight;pos=end+1;}
        GdiFlush();const auto bytes=static_cast<const unsigned char*>(data);
        for(size_t i=0;i<image.pixels.size();i+=4){const unsigned coverage=std::max({bytes[i],bytes[i+1],bytes[i+2]});const unsigned alpha=150+coverage*105/255,color=coverage*255/alpha;
            image.pixels[i]=image.pixels[i+1]=image.pixels[i+2]=static_cast<unsigned char>(color);image.pixels[i+3]=static_cast<unsigned char>(alpha);}
        SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);return image;
    }
    void Run(){
        for(;;){
            std::optional<Request> request;
            {std::unique_lock guard(mutex);wake.wait(guard,[&]{return stopping||pending.has_value();});if(stopping)return;request=std::move(pending);pending.reset();}
            std::optional<Image> image;try{image=Draw(*request);}catch(...){/* Retain the previous overlay on allocation failure. */}
            {std::lock_guard guard(mutex);if(!stopping&&request->generation==generation&&image){ready=std::move(image);if(request->window)PostMessageW(request->window,ReadyMessage,0,0);}}
        }
    }
public:
    ~StatisticsRasterizer(){Stop();}
    void Stop(){
        {std::lock_guard guard(mutex);stopping=true;pending.reset();}wake.notify_one();if(worker.joinable())worker.join();
    }
    void Reset(){std::lock_guard guard(mutex);++generation;pending.reset();ready.reset();}
    bool Submit(const std::wstring& text,int width,int height,HWND window=nullptr)noexcept{
        try{std::lock_guard guard(mutex);if(stopping)return false;
            if(!worker.joinable())worker=std::thread([this]{Run();});
            pending=Request{text,width,height,generation,window};wake.notify_one();return true;
        }catch(...){return false;}
    }
    std::optional<Image> Take(){std::lock_guard guard(mutex);auto result=std::move(ready);ready.reset();return result;}
};
