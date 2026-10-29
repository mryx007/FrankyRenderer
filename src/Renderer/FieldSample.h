#pragma once
#include "Renderer/Backend.h"
#include <dshow.h>
#include <atomic>

struct QueuedVideoFrame
{
    uint64_t id = 0;
    VideoFrame frame;
    REFERENCE_TIME start = 0, stop = 0, nominalDuration = 0;
    pl_field first = PL_FIELD_TOP;
    bool hardware = false;
};

inline constexpr GUID IID_InternalFieldSample = {
    0x15007b1c, 0x0ee9, 0x49e9, {0x90, 0xf5, 0xc8, 0x2c, 0x50, 0xcb, 0x62, 0x8f}};

// Timing-only sample. All pixel planes are already renderer-owned on the GPU.
// DirectShow schedules this sample with its ordinary reference-clock path.
class FieldSample final : public IMediaSample
{
    std::atomic<ULONG> refs = 1;
    BYTE dummy = 0;
    REFERENCE_TIME start, stop;

  public:
    QueuedVideoFrame frame;
    uint64_t previous = 0, next = 0;
    pl_field field;
    bool motion = false;
    pl_field following = PL_FIELD_NONE;
    float position = 0, duration = 0;
    uint64_t afterNext = 0;
    std::shared_ptr<const std::vector<TemporalFrame>> temporal;

    FieldSample(const QueuedVideoFrame& value, uint64_t past, uint64_t future, int parity)
        : frame(value), previous(past), next(future), field(parity ? pl_field_other(value.first) : value.first)
    {
        const auto middle = value.start + (value.stop - value.start) / 2;
        start = parity ? middle : value.start;
        stop = parity ? value.stop : middle;
    }

    FieldSample(const QueuedVideoFrame& value,
                uint64_t past,
                uint64_t future,
                pl_field which,
                pl_field after,
                REFERENCE_TIME a,
                REFERENCE_TIME b,
                float pos,
                float span,
                uint64_t later = 0)
        : start(a), stop(b), frame(value), previous(past), next(future), field(which), motion(true), following(after),
          position(pos), duration(span), afterNext(later)
    {
    }

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IMediaSample && id != IID_InternalFieldSample)
            return E_NOINTERFACE;
        *out = static_cast<IMediaSample*>(this);
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++refs;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        auto count = --refs;
        if (!count)
            delete this;
        return count;
    }

    STDMETHODIMP GetPointer(BYTE** out) override
    {
        if (!out)
            return E_POINTER;
        *out = &dummy;
        return S_OK;
    }

    STDMETHODIMP_(long) GetSize() override
    {
        return 1;
    }

    STDMETHODIMP GetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override
    {
        if (!a || !b)
            return E_POINTER;
        *a = start;
        *b = stop;
        return S_OK;
    }

    STDMETHODIMP SetTime(REFERENCE_TIME* a, REFERENCE_TIME* b) override
    {
        if (!a || !b)
            return E_POINTER;
        start = *a;
        stop = *b;
        return S_OK;
    }

    STDMETHODIMP IsSyncPoint() override
    {
        return S_OK;
    }

    STDMETHODIMP SetSyncPoint(BOOL) override
    {
        return S_OK;
    }

    STDMETHODIMP IsPreroll() override
    {
        return S_FALSE;
    }

    STDMETHODIMP SetPreroll(BOOL) override
    {
        return S_OK;
    }

    STDMETHODIMP_(long) GetActualDataLength() override
    {
        return 1;
    }

    STDMETHODIMP SetActualDataLength(long) override
    {
        return S_OK;
    }

    STDMETHODIMP GetMediaType(AM_MEDIA_TYPE** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        return S_FALSE;
    }

    STDMETHODIMP SetMediaType(AM_MEDIA_TYPE*) override
    {
        return E_NOTIMPL;
    }

    STDMETHODIMP IsDiscontinuity() override
    {
        return S_FALSE;
    }

    STDMETHODIMP SetDiscontinuity(BOOL) override
    {
        return S_OK;
    }

    STDMETHODIMP GetMediaTime(LONGLONG*, LONGLONG*) override
    {
        return VFW_E_MEDIA_TIME_NOT_SET;
    }

    STDMETHODIMP SetMediaTime(LONGLONG*, LONGLONG*) override
    {
        return S_OK;
    }
};
