#pragma once
// Runtime support for the decoded-base DV supplement. No renderer or test exports.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <stdexcept>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}
#include <libplacebo/d3d11.h>
#include <libplacebo/colorspace.h>
#include "dovi-map.inc"

static void check(bool good, const char* message)
{
    if (!good)
    {
        throw std::runtime_error(message);
    }
}

static void avcheck(int code, const char* message)
{
    if (code < 0)
    {
        throw std::runtime_error(message);
    }
}

struct Layer
{
    AVCodecContext* decoder = nullptr;
    AVStream* stream = nullptr;
    std::deque<AVFrame*> frames;
    bool hardware = false;

    static AVPixelFormat selectFormat(AVCodecContext* ctx, const AVPixelFormat* formats)
    {
        for (auto f = formats; *f != AV_PIX_FMT_NONE; ++f)
        {
            if (*f != AV_PIX_FMT_D3D11)
                continue;
            AVBufferRef* pool = nullptr;
            int status = avcodec_get_hw_frames_parameters(ctx, ctx->hw_device_ctx, *f, &pool);
            avcheck(status, "D3D11 frame pool parameters");
            auto frames = reinterpret_cast<AVHWFramesContext*>(pool->data);
            auto d3d = reinterpret_cast<AVD3D11VAFramesContext*>(frames->hwctx);
            d3d->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            frames->initial_pool_size += 8;
            avcheck(av_hwframe_ctx_init(pool), "D3D11 shader-readable frame pool");
            ctx->hw_frames_ctx = pool;
            return *f;
        }
        return AV_PIX_FMT_NONE; // No silent software fallback in hardware test.
    }

    ~Layer()
    {
        for (auto f : frames)
            av_frame_free(&f);
        avcodec_free_context(&decoder);
    }

    void init(AVStream* s, ID3D11Device* device = nullptr)
    {
        stream = s;
        auto codec = avcodec_find_decoder(s->codecpar->codec_id);
        check(codec != nullptr, "HEVC decoder available");
        decoder = avcodec_alloc_context3(codec);
        avcheck(avcodec_parameters_to_context(decoder, s->codecpar), "decoder parameters");
        decoder->pkt_timebase = s->time_base;
        decoder->thread_count = 1;
        if (device)
        {
            hardware = true;
            decoder->hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
            check(decoder->hw_device_ctx != nullptr, "D3D11 decoder context allocation");
            auto ctx = reinterpret_cast<AVHWDeviceContext*>(decoder->hw_device_ctx->data);
            auto d3d = reinterpret_cast<AVD3D11VADeviceContext*>(ctx->hwctx);
            device->AddRef();
            d3d->device = device;
            avcheck(av_hwdevice_ctx_init(decoder->hw_device_ctx), "D3D11 decoder device");
            decoder->get_format = selectFormat;
        }
        avcheck(avcodec_open2(decoder, codec, nullptr), "open HEVC decoder");
    }

    void drain()
    {
        while (true)
        {
            AVFrame* f = av_frame_alloc();
            int status = avcodec_receive_frame(decoder, f);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF)
            {
                av_frame_free(&f);
                return;
            }
            avcheck(status, "receive decoded frame");
            if (hardware)
            {
                check(f->format == AV_PIX_FMT_D3D11, "actual D3D11 decoded surface");
            }
            check(frames.size() < 128, "bounded layer queue");
            frames.push_back(f);
        }
    }

    void push(const AVPacket* packet)
    {
        int status = avcodec_send_packet(decoder, packet);
        if (status == AVERROR(EAGAIN))
        {
            drain();
            status = avcodec_send_packet(decoder, packet);
        }
        avcheck(status, "send packet");
        drain();
    }
};

static void
UploadDvPlanes(AVFrame* f, ID3D11ComputeShader* splitShader, Microsoft::WRL::ComPtr<ID3D11Texture2D>* nativeOut)
{
    check(f->format == AV_PIX_FMT_D3D11 && nativeOut, "native HEVC surface and plane output required");
    using Microsoft::WRL::ComPtr;
    auto texture = reinterpret_cast<ID3D11Texture2D*>(f->data[0]);
    D3D11_TEXTURE2D_DESC description = {};
    texture->GetDesc(&description);
    check(description.Format == DXGI_FORMAT_P010, "hardware 10-bit P010 surface");
    UINT slice = static_cast<UINT>(reinterpret_cast<intptr_t>(f->data[1]));
    ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(device->CreateDeferredContext(0, &context)), "deferred plane conversion context");
    ComPtr<ID3D11Texture2D> readable;
    if (!(description.BindFlags & D3D11_BIND_SHADER_RESOURCE))
    {
        auto copy = description;
        copy.ArraySize = 1;
        copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy.MiscFlags = copy.CPUAccessFlags = 0;
        copy.Usage = D3D11_USAGE_DEFAULT;
        check(SUCCEEDED(device->CreateTexture2D(&copy, nullptr, &readable)), "shader-readable decoder copy");
        context->CopySubresourceRegion(readable.Get(), 0, 0, 0, 0, texture, slice, nullptr);
        texture = readable.Get();
        slice = 0;
    }
    ComPtr<ID3D11ShaderResourceView> views[2];
    ID3D11ShaderResourceView* inputs[2] = {};
    for (int p = 0; p < 2; ++p)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.Format = p ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R16_UNORM;
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipLevels = 1;
        desc.Texture2DArray.FirstArraySlice = slice;
        desc.Texture2DArray.ArraySize = 1;
        check(SUCCEEDED(device->CreateShaderResourceView(texture, &desc, &views[p])), "GPU plane input view");
        inputs[p] = views[p].Get();
    }
    ComPtr<ID3D11UnorderedAccessView> outputs[3];
    ID3D11UnorderedAccessView* writes[3] = {};
    for (int p = 0; p < 3; ++p)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = p ? (f->width + 1) / 2 : f->width;
        desc.Height = p ? (f->height + 1) / 2 : f->height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> plane;
        check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &plane)), "canonical GPU plane");
        check(SUCCEEDED(device->CreateUnorderedAccessView(plane.Get(), nullptr, &outputs[p])), "GPU plane output view");
        writes[p] = outputs[p].Get();
        nativeOut[p] = plane;
    }
    context->CSSetShader(splitShader, nullptr, 0);
    context->CSSetShaderResources(0, 2, inputs);
    context->CSSetUnorderedAccessViews(0, 3, writes, nullptr);
    context->Dispatch((f->width + 15) / 16, (f->height + 15) / 16, 1);
    ComPtr<ID3D11CommandList> commands;
    check(SUCCEEDED(context->FinishCommandList(FALSE, &commands)), "finish plane conversion commands");
    ComPtr<ID3D11DeviceContext> immediate;
    device->GetImmediateContext(&immediate);
    immediate->ExecuteCommandList(commands.Get(), TRUE);
}

struct DvGpu
{
    pl_d3d11 d3d = nullptr;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> splitShader;

    explicit DvGpu(ID3D11Device* externalDevice)
    {
        try
        {
            auto deviceParams = pl_d3d11_default_params;
            deviceParams.device = externalDevice;
            deviceParams.flags |= D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
            d3d = pl_d3d11_create(nullptr, &deviceParams);
            check(d3d != nullptr, "D3D11 GPU");
            const char* shader = R"(
Texture2DArray<float> yInput : register(t0);
Texture2DArray<float2> uvInput : register(t1);
RWTexture2D<float> yOutput : register(u0);
RWTexture2D<float> uOutput : register(u1);
RWTexture2D<float> vOutput : register(u2);
[numthreads(16,16,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint w,h; yOutput.GetDimensions(w,h);
    if(id.x<w && id.y<h)
        yOutput[id.xy]=floor(yInput.Load(int4(id.xy,0,0))*65535.0+0.5)/64.0/65535.0;
    uOutput.GetDimensions(w,h);
    if(id.x<w && id.y<h) {
        float2 uv=floor(uvInput.Load(int4(id.xy,0,0))*65535.0+0.5)/64.0/65535.0;
        uOutput[id.xy]=uv.x;vOutput[id.xy]=uv.y;
    }
})";
            Microsoft::WRL::ComPtr<ID3DBlob> code, error;
            auto compiled =
                D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &error);
            if (FAILED(compiled) && error)
                fprintf(stderr, "%s", static_cast<char*>(error->GetBufferPointer()));
            check(SUCCEEDED(compiled), "compile canonical GPU plane conversion");
            check(SUCCEEDED(d3d->device->CreateComputeShader(
                      code->GetBufferPointer(), code->GetBufferSize(), nullptr, &splitShader)),
                  "create canonical GPU plane conversion");
        }
        catch (...)
        {
            pl_d3d11_destroy(&d3d);
            throw;
        }
    }

    ~DvGpu()
    {
        pl_d3d11_destroy(&d3d);
    }

    DvGpu(const DvGpu&) = delete;
    DvGpu& operator=(const DvGpu&) = delete;
};
