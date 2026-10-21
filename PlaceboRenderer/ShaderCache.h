#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <libplacebo/cache.h>
#include <libplacebo/gpu.h>
#include <filesystem>
#include <vector>
#include <string>
#include <array>
#include <atomic>
#include <cstring>

// Disk access is confined to construction/destruction, never a render callback.
// A cache is an optional optimization: all I/O failures leave a working RAM cache.
class ShaderCache {
    static constexpr size_t Limit=64*1024*1024;
    struct Header {uint64_t magic=0x3145484341434c50ull;uint32_t version=1,api=PL_API_VER;uint64_t library=0,size=0,checksum=0;};
    pl_cache cache=nullptr;std::filesystem::path file;uint64_t saved=0,library=0;int loaded=0;
    static std::wstring Environment(const wchar_t* name){
        DWORD size=GetEnvironmentVariableW(name,nullptr,0);if(!size)return {};
        std::wstring value(size,L'\0');DWORD count=GetEnvironmentVariableW(name,value.data(),size);
        if(!count||count>=size)return {};value.resize(count);return value;
    }
    static uint64_t Hash(const void* data,size_t size,uint64_t value=14695981039346656037ull){
        auto bytes=static_cast<const uint8_t*>(data);for(size_t i=0;i<size;i++){value^=bytes[i];value*=1099511628211ull;}return value;
    }
    static uint64_t LibraryHash(){
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&pl_cache_create),&module))return 0;
        std::wstring path(32768,L'\0');DWORD length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));if(!length||length>=path.size())return 0;path.resize(length);
        HANDLE input=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(input==INVALID_HANDLE_VALUE)return 0;
        LARGE_INTEGER size={};uint64_t hash=14695981039346656037ull;bool good=GetFileSizeEx(input,&size)&&size.QuadPart>0&&size.QuadPart<512ll*1024*1024;
        std::array<uint8_t,65536> buffer;LONGLONG left=size.QuadPart;
        while(good&&left){DWORD read=0;good=ReadFile(input,buffer.data(),static_cast<DWORD>(std::min<LONGLONG>(left,buffer.size())),&read,nullptr)&&read>0;if(good){hash=Hash(buffer.data(),read,hash);left-=read;}}
        CloseHandle(input);return good?hash:0;
    }
    static std::filesystem::path Directory(){
        auto custom=Environment(L"PLACEBO_RENDERER_CACHE_DIR");if(!custom.empty())return custom;
        if(!Environment(L"PLACEBO_RENDERER_TEST_ROOT").empty())return {};
        auto local=Environment(L"LOCALAPPDATA");return local.empty()?std::filesystem::path{}:std::filesystem::path(local)/L"PlaceboRenderer"/L"Cache";
    }
    void Load(){
        HANDLE input=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(input==INVALID_HANDLE_VALUE)return;
        Header header;LARGE_INTEGER size={};DWORD read=0;
        bool good=GetFileSizeEx(input,&size)&&size.QuadPart>=sizeof(Header)&&size.QuadPart<=Limit+sizeof(Header)
            &&ReadFile(input,&header,sizeof(header),&read,nullptr)&&read==sizeof(header);
        Header expected;
        good=good&&header.magic==expected.magic&&header.version==expected.version&&header.api==PL_API_VER&&header.library==library&&header.size==size.QuadPart-sizeof(Header);
        std::vector<uint8_t> data;if(good){data.resize(static_cast<size_t>(header.size));good=ReadFile(input,data.data(),static_cast<DWORD>(data.size()),&read,nullptr)&&read==data.size()&&Hash(data.data(),data.size())==header.checksum;}
        CloseHandle(input);
        if(good){loaded=pl_cache_load(cache,data.data(),data.size());if(loaded<0){pl_cache_reset(cache);loaded=0;}}
    }
public:
    ShaderCache(){
        auto params=pl_cache_default_params;params.max_object_size=16*1024*1024;params.max_total_size=48*1024*1024;cache=pl_cache_create(&params);
        try {auto directory=Directory();if(!directory.empty()){
            static const uint64_t fingerprint=LibraryHash();library=fingerprint;
            if(library){file=directory/L"shader-cache-v1.bin";Load();}
        }}catch(...){file.clear();}
        saved=pl_cache_signature(cache);
    }
    ~ShaderCache(){Save();pl_cache_destroy(&cache);}
    ShaderCache(const ShaderCache&)=delete;ShaderCache& operator=(const ShaderCache&)=delete;
    void Attach(pl_gpu gpu){pl_gpu_set_cache(gpu,cache);}
    int Loaded()const{return loaded;}int Objects()const{return pl_cache_objects(cache);}
    size_t Bytes()const{return pl_cache_size(cache);}
    pl_cache Handle()const{return cache;}
    void Save()noexcept{
        try {
            if(file.empty()||pl_cache_signature(cache)==saved)return;
            std::vector<uint8_t> data;
            for(int attempt=0;attempt<3;attempt++){
                const size_t size=pl_cache_save(cache,nullptr,0);if(size>Limit)return;data.resize(size);
                if(pl_cache_save(cache,data.data(),data.size())<=data.size())break;
                if(attempt==2)return;
            }
            Header header;header.library=library;header.size=data.size();header.checksum=Hash(data.data(),data.size());
            std::error_code error;std::filesystem::create_directories(file.parent_path(),error);if(error)return;
            static std::atomic<unsigned> sequence=0;auto temporary=file;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence++);
            HANDLE output=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(output==INVALID_HANDLE_VALUE)return;
            DWORD written=0;bool good=WriteFile(output,&header,sizeof(header),&written,nullptr)&&written==sizeof(header)
                &&WriteFile(output,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(output);
            CloseHandle(output);
            if(good)good=!!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
            if(!good)DeleteFileW(temporary.c_str());else saved=pl_cache_signature(cache);
        }catch(...){/* Cache failures must never prevent playback or teardown. */}
    }
};
