#pragma once
#include "MediaSampleSideData.h"

// The actual MPC/LAV side-data sample and MPC allocation loop are reused.
// No MPC pixel processing or decoder implementation is involved.
class SideDataSample final:public CMediaSampleSideData {
public:
    SideDataSample(LPCTSTR name,CBaseAllocator* allocator,HRESULT* hr,LPBYTE bytes,LONG length):CMediaSampleSideData(name,allocator,hr,bytes,length){}
    STDMETHODIMP_(ULONG) AddRef()override{return CMediaSample::AddRef();}
};
class SideDataAllocator final:public CMemAllocator {
protected:
    HRESULT Alloc()override;
public:
    explicit SideDataAllocator(HRESULT* hr):CMemAllocator(NAME("libplacebo side-data allocator"),nullptr,hr){}
};
