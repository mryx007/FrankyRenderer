#pragma once
#include "Renderer/MediaSampleSideData.h"

// The actual MPC/LAV side-data sample and MPC allocation loop are reused.
// No MPC pixel processing or decoder implementation is involved.
class SideDataSample final : public CMediaSampleSideData
{
  public:
    SideDataSample(LPCTSTR name, CBaseAllocator* allocator, HRESULT* hr, LPBYTE bytes, LONG length)
        : CMediaSampleSideData(name, allocator, hr, bytes, length)
    {
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return CMediaSample::AddRef();
    }
};

class SideDataAllocator final : public CMemAllocator
{
    // A media type the upstream filter accepted in QueryAccept, handed to it
    // with the next buffer (DirectShow dynamic format change from downstream).
    CMediaType pending;
    bool hasPending = false;

  protected:
    HRESULT Alloc() override;

  public:
    explicit SideDataAllocator(HRESULT* hr) : CMemAllocator(NAME("libplacebo side-data allocator"), nullptr, hr) {}

    void SetPendingType(const CMediaType& type)
    {
        CAutoLock guard(this);
        pending = type;
        hasPending = true;
    }

    STDMETHODIMP GetBuffer(IMediaSample** sample, REFERENCE_TIME* start, REFERENCE_TIME* end, DWORD flags) override
    {
        HRESULT hr = CMemAllocator::GetBuffer(sample, start, end, flags);
        if (FAILED(hr) || !sample || !*sample)
            return hr;
        CAutoLock guard(this);
        if (hasPending && SUCCEEDED((*sample)->SetMediaType(&pending)))
            hasPending = false;
        return hr;
    }
};
