#pragma once

// Retains the decoded sample and matched layer textures until presentation completes.
struct DvPicture
{
    DvFrameInfo info = {};
    ComPtr<ID3D11Texture2D> planes[2][3];
};

class DvSample final : public IMediaSample, public IDvLayerSample
{
    std::atomic<ULONG> refs{1};
    ComPtr<IMediaSample> original;
    std::shared_ptr<DvPicture> picture;

  public:
    DvSample(IMediaSample* sample, std::shared_ptr<DvPicture> p) : original(sample), picture(std::move(p)) {}

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == IID_IMediaSample)
            *out = static_cast<IMediaSample*>(this);
        else if (id == __uuidof(IDvLayerSample))
            *out = static_cast<IDvLayerSample*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++refs;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE GetInfo(UINT v, UINT bytes, DvFrameInfo* info) override
    {
        if (!info)
            return E_POINTER;
        if (v != DvTransportVersion || bytes != sizeof(DvFrameInfo))
            return E_INVALIDARG;
        *info = picture->info;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPlane(UINT l, UINT p, ID3D11Texture2D** texture) override
    {
        if (!texture)
            return E_POINTER;
        *texture = nullptr;
        if (l >= 2 || p >= 3)
            return E_INVALIDARG;
        return picture->planes[l][p].CopyTo(texture);
    }

    STDMETHODIMP GetPointer(BYTE** p) override
    {
        return original->GetPointer(p);
    }

    STDMETHODIMP_(long) GetSize() override
    {
        return original->GetSize();
    }

    STDMETHODIMP GetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override
    {
        return original->GetTime(a, b);
    }

    STDMETHODIMP SetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override
    {
        return original->SetTime(a, b);
    }

    STDMETHODIMP IsSyncPoint() override
    {
        return original->IsSyncPoint();
    }

    STDMETHODIMP SetSyncPoint(BOOL b) override
    {
        return original->SetSyncPoint(b);
    }

    STDMETHODIMP IsPreroll() override
    {
        return original->IsPreroll();
    }

    STDMETHODIMP SetPreroll(BOOL b) override
    {
        return original->SetPreroll(b);
    }

    STDMETHODIMP_(long) GetActualDataLength() override
    {
        return original->GetActualDataLength();
    }

    STDMETHODIMP SetActualDataLength(long n) override
    {
        return original->SetActualDataLength(n);
    }

    STDMETHODIMP GetMediaType(AM_MEDIA_TYPE** t) override
    {
        return original->GetMediaType(t);
    }

    STDMETHODIMP SetMediaType(AM_MEDIA_TYPE* t) override
    {
        return original->SetMediaType(t);
    }

    STDMETHODIMP IsDiscontinuity() override
    {
        return original->IsDiscontinuity();
    }

    STDMETHODIMP SetDiscontinuity(BOOL b) override
    {
        return original->SetDiscontinuity(b);
    }

    STDMETHODIMP GetMediaTime(LONGLONG* a, LONGLONG* b) override
    {
        return original->GetMediaTime(a, b);
    }

    STDMETHODIMP SetMediaTime(LONGLONG* a, LONGLONG* b) override
    {
        return original->SetMediaTime(a, b);
    }
};
