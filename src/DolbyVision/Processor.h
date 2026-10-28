#pragma once
#include "../MediaFormat.h"
#include <wrl/client.h>
#include <memory>

// Internal local-file supplementation. No registered DirectShow filter is needed.
class DvProcessor
{
    struct State;
    std::unique_ptr<State> state;

  public:
    DvProcessor();
    ~DvProcessor();
    HRESULT CheckFormat(IFilterGraph*, IPin*, const MediaFormat&);
    HRESULT Prepare(IFilterGraph*,
                    IPin*,
                    IMediaSample*,
                    const MediaFormat&,
                    REFERENCE_TIME,
                    ID3D11Device*,
                    Microsoft::WRL::ComPtr<IMediaSample>&);
    HRESULT SetDevice(ID3D11Device*, HANDLE);
    void Cancel();
    void Resume();
    void ResetTimeline();
    void RefreshSource(IFilterGraph*, IPin*);
    void Disconnect();
    bool Active() const;
    bool ShouldProcess() const;
};
