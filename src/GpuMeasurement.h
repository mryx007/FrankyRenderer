#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "PresentationMeasurement.h"

// Bounded timestamp ring. Never wait for a result or flush to obtain one.
class GpuMeasurement
{
    using Query = Microsoft::WRL::ComPtr<ID3D11Query>;

    struct Slot
    {
        Query disjoint, start, end;
        bool pending = false;
    };

    Slot slots[8];
    int active = -1;
    bool unsupported = false;

  public:
    DurationMeasurement duration;
    uint64_t skipped = 0, disjoint = 0;

    void Reset()
    {
        for (auto& s : slots)
            s = {};
        active = -1;
        unsupported = false;
        duration.Reset();
        skipped = disjoint = 0;
    }

    void Collect(ID3D11DeviceContext* context)
    {
        for (auto& s : slots)
            if (s.pending)
            {
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data = {};
                auto hr = context->GetData(s.disjoint.Get(), &data, sizeof(data), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (hr == S_FALSE)
                    continue;
                if (FAILED(hr))
                {
                    s.pending = false;
                    continue;
                }
                UINT64 a = 0, b = 0;
                auto ha = context->GetData(s.start.Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH),
                     hb = context->GetData(s.end.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (ha == S_FALSE || hb == S_FALSE)
                    continue;
                s.pending = false;
                if (data.Disjoint || !data.Frequency)
                {
                    ++disjoint;
                    continue;
                }
                if (SUCCEEDED(ha) && SUCCEEDED(hb) && b >= a)
                    duration.Add(GetTickCount64(), 1000. * static_cast<double>(b - a) / data.Frequency);
            }
    }

    void Begin(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        Collect(context);
        if (unsupported || active >= 0)
            return;
        for (int i = 0; i < 8; i++)
            if (!slots[i].pending)
            {
                auto& s = slots[i];
                if (!s.disjoint)
                {
                    D3D11_QUERY_DESC desc = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
                    HRESULT hr = device->CreateQuery(&desc, &s.disjoint);
                    desc.Query = D3D11_QUERY_TIMESTAMP;
                    if (SUCCEEDED(hr))
                        hr = device->CreateQuery(&desc, &s.start);
                    if (SUCCEEDED(hr))
                        hr = device->CreateQuery(&desc, &s.end);
                    if (FAILED(hr))
                    {
                        s = {};
                        unsupported = true;
                        return;
                    }
                }
                context->Begin(s.disjoint.Get());
                context->End(s.start.Get());
                active = i;
                return;
            }
        ++skipped;
    }

    void End(ID3D11DeviceContext* context)
    {
        if (active < 0)
            return;
        auto& s = slots[active];
        context->End(s.end.Get());
        context->End(s.disjoint.Get());
        s.pending = true;
        active = -1;
    }
};
