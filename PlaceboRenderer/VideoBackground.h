#pragma once
#include <windows.h>
#include <cwchar>
// The video owner outlives individual DirectShow graphs. Give that owner a
// plain black background child, below video and player controls, so removing
// one renderer does not expose stale owner contents before the next arrives.
// The class uses only DefWindowProcW and a stock black brush, registered under
// the host EXE. No callback or object pointer leads into this renderer DLL.
// The owner's UI thread owns and destroys the child.
namespace VideoBackground {
inline constexpr wchar_t Key[]=L"libplacebo.VideoBackground.{7EECF495-2399-457B-A6E5-71847BC29C44}";
inline constexpr wchar_t Mark[]=L"libplacebo.VideoBackground.SystemDrawn";
inline constexpr wchar_t ClassName[]=L"libplacebo.OwnerBackground.{7EECF495-2399-457B-A6E5-71847BC29C44}";
inline HWND Find(HWND parent){
    HWND child=reinterpret_cast<HWND>(GetPropW(parent,Key));if(!child)return nullptr;
    wchar_t cls[128]={};GetClassNameW(child,cls,128);
    return IsWindow(child)&&GetParent(child)==parent&&GetPropW(child,Mark)&&!wcscmp(cls,ClassName)?child:nullptr;
}
inline void Repaint(HWND parent){
    // Invalidation alone waits for the host UI thread's next paint dispatch.
    // Restore exposed pixels now without sending synchronous paint messages
    // to that thread during graph teardown. Sibling clipping preserves video
    // and controls above this background.
    if(HWND child=Find(parent)){
        RECT client={};
        if(GetClientRect(child,&client)){
            if(HDC dc=GetDCEx(child,nullptr,DCX_CACHE|DCX_CLIPSIBLINGS|DCX_CLIPCHILDREN)){
                FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                GdiFlush();ReleaseDC(child,dc);
            }
        }
        RedrawWindow(child,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_NOCHILDREN);
    }
}
inline bool Resize(HWND parent){
    HWND child=Find(parent);RECT client={};if(!child||!GetClientRect(parent,&client))return false;
    // Size changes can arrive on the renderer's graph thread. Queue those to
    // the owner's UI thread rather than wait on it from a window callback.
    UINT flags=SWP_NOACTIVATE|SWP_SHOWWINDOW;
    if(GetWindowThreadProcessId(child,nullptr)!=GetCurrentThreadId())flags|=SWP_ASYNCWINDOWPOS;
    if(!SetWindowPos(child,HWND_BOTTOM,0,0,client.right,client.bottom,flags))return false;
    if(!(flags&SWP_ASYNCWINDOWPOS))UpdateWindow(child);
    return true;
}
inline HWND Ensure(HWND parent){
    DWORD process=0;const DWORD thread=GetWindowThreadProcessId(parent,&process);
    // Never create a background owned by the short-lived graph thread, and
    // never inject work into an arbitrary host's thread. PotPlayer assigns
    // IVideoWindow::Owner from its UI thread, which is the creation point.
    if(!parent||process!=GetCurrentProcessId()||thread!=GetCurrentThreadId())return nullptr;
    HWND child=Find(parent);
    if(!child){
        const HINSTANCE host=GetModuleHandleW(nullptr);WNDCLASSEXW cls={};cls.cbSize=sizeof(cls);
        if(!GetClassInfoExW(host,ClassName,&cls)){
            cls.lpfnWndProc=DefWindowProcW;cls.hInstance=host;cls.lpszClassName=ClassName;cls.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
            if(!GetClassInfoExW(host,ClassName,&cls))return nullptr;
        }
        if(cls.lpfnWndProc!=DefWindowProcW||cls.hbrBackground!=GetStockObject(BLACK_BRUSH))return nullptr;
        child=CreateWindowExW(WS_EX_NOPARENTNOTIFY|WS_EX_NOACTIVATE,ClassName,L"",
            WS_CHILD|WS_DISABLED|WS_CLIPSIBLINGS,0,0,0,0,parent,nullptr,host,nullptr);
        if(!child)return nullptr;
        if(!SetPropW(child,Mark,reinterpret_cast<HANDLE>(1))||!SetPropW(parent,Key,child)){DestroyWindow(child);return nullptr;}
    }
    Resize(parent);return child;
}
}
