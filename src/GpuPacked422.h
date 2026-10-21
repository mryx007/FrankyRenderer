#pragma once
#include <d3d11_1.h>
#include <d3d10.h>
#include <libplacebo/d3d11.h>
#include <wrl/client.h>
#include "build/Unpack422.h"
// Pure GPU channel unpacking. Keeps the original luma depth and 4:2:2 chroma.
class GpuPacked422 {
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> targets[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> views[2];
public:
    bool HasFrame()const{return input.Get()!=nullptr;}
    void ResetFrame(){input.Reset();for(auto& v:views)v.Reset();for(auto& t:targets)t.Reset();}
    void Reset(){ResetFrame();shader.Reset();state.Reset();}
    HRESULT Run(pl_d3d11 d,ID3D11DeviceContext* context,ID3D11Texture2D* source,DXGI_FORMAT format,pl_tex* planes){
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device1> device;ComPtr<ID3D11DeviceContext1> ctx;
        HRESULT hr=d->device->QueryInterface(IID_PPV_ARGS(&device));if(FAILED(hr))return hr;
        hr=context->QueryInterface(IID_PPV_ARGS(&ctx));if(FAILED(hr))return hr;
        if(!shader){hr=d->device->CreateComputeShader(Unpack422,sizeof(Unpack422),nullptr,&shader);if(FAILED(hr))return hr;}
        if(!state){const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};hr=device->CreateDeviceContextState(0,levels,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&state);if(FAILED(hr))return hr;}
        D3D11_TEXTURE2D_DESC desc;source->GetDesc(&desc);const bool eight=format==DXGI_FORMAT_YUY2;
        if(!input){D3D11_SHADER_RESOURCE_VIEW_DESC srv={};srv.Format=eight?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R16G16B16A16_UNORM;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;hr=d->device->CreateShaderResourceView(source,&srv,&input);if(FAILED(hr))return hr;
            for(int i=0;i<2;i++){D3D11_TEXTURE2D_DESC output={};output.Width=desc.Width/(i?2:1);output.Height=desc.Height;output.MipLevels=output.ArraySize=1;output.SampleDesc.Count=1;output.Format=eight?(i?DXGI_FORMAT_R8G8_UNORM:DXGI_FORMAT_R8_UNORM):(i?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R16_UNORM);output.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
                hr=d->device->CreateTexture2D(&output,nullptr,&targets[i]);if(FAILED(hr))return hr;hr=d->device->CreateUnorderedAccessView(targets[i].Get(),nullptr,&views[i]);if(FAILED(hr))return hr;
                pl_d3d11_wrap_params wrap={};wrap.tex=targets[i].Get();planes[i]=pl_d3d11_wrap(d->gpu,&wrap);if(!planes[i])return E_FAIL;
            }
        }
        ComPtr<ID3D10Multithread> protection;hr=d->device->QueryInterface(IID_PPV_ARGS(&protection));if(FAILED(hr))return hr;
        // State switching and dispatch must be atomic with respect to a decoder
        // using the same immediate context, not merely protected per D3D call.
        protection->Enter();
        ComPtr<ID3DDeviceContextState> previous;ctx->SwapDeviceContextState(state.Get(),&previous);
        auto srv=input.Get();ID3D11UnorderedAccessView* outputs[]={views[0].Get(),views[1].Get()};ctx->CSSetShader(shader.Get(),nullptr,0);ctx->CSSetShaderResources(0,1,&srv);ctx->CSSetUnorderedAccessViews(0,2,outputs,nullptr);ctx->Dispatch((desc.Width/2+15)/16,(desc.Height+15)/16,1);
        // Unbind before restoring libplacebo/decoder state and sampling results.
        srv=nullptr;outputs[0]=outputs[1]=nullptr;ctx->CSSetShaderResources(0,1,&srv);ctx->CSSetUnorderedAccessViews(0,2,outputs,nullptr);ctx->SwapDeviceContextState(previous.Get(),nullptr);protection->Leave();
        return S_OK;
    }
};
