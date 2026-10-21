#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>
#include <vector>

// Explicit opt-in only: routine playback must not open/flush a log file for
// every host call. Restart the player with PLACEBO_RENDERER_DEBUG_LOG=1 to
// enable local diagnostics. No frame polling or network activity.
inline bool TraceEnabled(){
    static const bool enabled=[](){
        if(GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT",nullptr,0))return false;
        wchar_t value[2]={};return GetEnvironmentVariableW(L"PLACEBO_RENDERER_DEBUG_LOG",value,2)==1&&value[0]==L'1';
    }();
    return enabled;
}
inline void Trace(const char* format,...){
    if(!TraceEnabled())return;
    static std::mutex lock;std::lock_guard guard(lock);HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Trace),&module))return;
    wchar_t name[32768];DWORD length=GetModuleFileNameW(module,name,32768);if(!length||length>=32768)return;
    std::wstring path(name,length);path.resize(path.find_last_of(L"\\")+1);path+=L"PlaceboRenderer.log";
    FILE* file=nullptr;if(_wfopen_s(&file,path.c_str(),L"ab")||!file)return;
    SYSTEMTIME now;GetLocalTime(&now);fprintf(file,"%04u-%02u-%02u %02u:%02u:%02u PID=%lu ",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    va_list args;va_start(args,format);vfprintf(file,format,args);va_end(args);fputc('\n',file);fclose(file);
}

inline void TraceInterface(const char* object,REFIID id){
    if(!TraceEnabled())return;
    struct Seen {std::string object;GUID id;};static std::mutex mutex;static std::vector<Seen> seen;bool fresh=false;
    {std::lock_guard guard(mutex);bool found=false;for(const auto& entry:seen)if(entry.object==object&&entry.id==id){found=true;break;}if(!found&&seen.size()<80){seen.push_back({object,id});fresh=true;}}
    if(fresh){wchar_t text[40]={};StringFromGUID2(id,text,40);Trace("Interface query on %s: %ls",object,text);}
}
