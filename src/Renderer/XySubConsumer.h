#pragma once
// Subtitle consumer side of SubRenderIntf.h (XySubFilter and other subtitle
// renderers). The provider renders premultiplied RGBA bitmaps for the video
// output rectangle; they are drawn into the renderer's display-sized overlay,
// so text subtitles stay sharp at display resolution.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "DirectShow/Include/SubRenderIntf.h"
#include "build/OverlayVS.h"
#include "build/OverlayPS.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

class XySubBridge
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

  public:
    struct Info
    {
        SIZE original = {}, arAdjusted = {}, display = {};
        RECT video = {};
        REFERENCE_TIME frameDuration = 0;
    };

    XySubBridge() : delivered(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

    ~XySubBridge()
    {
        if (delivered)
            CloseHandle(delivered);
    }

    void Connect(ISubRenderProvider* next)
    {
        std::lock_guard guard(lock);
        provider = next;
        frame.Reset();
    }

    // Returns the provider so the caller releases it outside any lock.
    ComPtr<ISubRenderProvider> Disconnect()
    {
        std::lock_guard guard(lock);
        frame.Reset();
        return std::move(provider);
    }

    bool Connected()
    {
        std::lock_guard guard(lock);
        return !!provider;
    }

    void Deliver(REFERENCE_TIME start, LPVOID context, ISubRenderFrame* next)
    {
        const auto id = uint64_t(uintptr_t(context));
        {
            std::lock_guard guard(lock);
            // Frames arrive in request order; an older one never replaces a newer.
            if (id < frameRequest)
                return;
            frame = next;
            frameRequest = id;
            frameStart = start;
        }
        SetEvent(delivered);
    }

    void Clear()
    {
        std::lock_guard guard(lock);
        frame.Reset();
    }

    void Update(const Info& next)
    {
        std::lock_guard guard(lock);
        info = next;
    }

    Info Current()
    {
        std::lock_guard guard(lock);
        return info;
    }

    // Draws the subtitles for one presented picture into the bound overlay
    // target. "inverted" is the host callback's inverted-alpha target.
    HRESULT Paint(ID3D11Device* device,
                  ID3D11DeviceContext* context,
                  int width,
                  int height,
                  const RECT& video,
                  REFERENCE_TIME start,
                  REFERENCE_TIME stop,
                  bool inverted)
    {
        ComPtr<ISubRenderProvider> source;
        uint64_t id = 0;
        {
            std::lock_guard guard(lock);
            info.video = video;
            info.display = {width, height};
            source = provider;
            id = ++requested;
        }
        if (!source)
            return S_FALSE;
        // XySubFilter delivers inside RequestFrame; other providers may deliver
        // from their own thread, so wait briefly and fall back to the last frame.
        if (FAILED(source->RequestFrame(start, stop, reinterpret_cast<LPVOID>(uintptr_t(id)))))
            return S_FALSE;
        ComPtr<ISubRenderFrame> current;
        const ULONGLONG deadline = GetTickCount64() + 40;
        for (;;)
        {
            {
                std::lock_guard guard(lock);
                // A late frame is only reused for the same moment, never across a seek.
                if (frameRequest >= id || GetTickCount64() >= deadline)
                {
                    if (frameRequest >= id || std::llabs(frameStart - start) < 5000000)
                        current = frame;
                    break;
                }
            }
            WaitForSingleObject(delivered, DWORD(deadline - std::min(deadline, GetTickCount64())));
        }
        if (!current)
            return S_FALSE;
        HRESULT hr = Prepare(device);
        if (FAILED(hr))
            return hr;
        return Draw(context, current.Get(), width, height, video, inverted);
    }

    // ISubRenderOptions fields the consumer answers; others are E_INVALIDARG.
    static HRESULT Get(LPCSTR field, int* value)
    {
        if (!field || !value)
            return E_POINTER;
        if (_stricmp(field, "supportedLevels"))
            return E_INVALIDARG;
        *value = 0; // PC levels only
        return S_OK;
    }

    HRESULT Get(LPCSTR field, SIZE* value)
    {
        if (!field || !value)
            return E_POINTER;
        const auto i = Current();
        if (!_stricmp(field, "originalVideoSize"))
            *value = i.original;
        else if (!_stricmp(field, "arAdjustedVideoSize"))
            *value = i.arAdjusted;
        else if (!_stricmp(field, "displayModeSize"))
            *value = i.display;
        else
            return E_INVALIDARG;
        return S_OK;
    }

    HRESULT Get(LPCSTR field, RECT* value)
    {
        if (!field || !value)
            return E_POINTER;
        const auto i = Current();
        if (!_stricmp(field, "videoOutputRect") || !_stricmp(field, "subtitleTargetRect") ||
            !_stricmp(field, "croppedVideoOutputRect"))
            *value = i.video;
        else if (!_stricmp(field, "fullscreenRect"))
            *value = {0, 0, i.display.cx, i.display.cy};
        else
            return E_INVALIDARG;
        return S_OK;
    }

    HRESULT Get(LPCSTR field, ULONGLONG* value)
    {
        if (!field || !value)
            return E_POINTER;
        if (_stricmp(field, "frameRate"))
            return E_INVALIDARG;
        *value = ULONGLONG(std::max<REFERENCE_TIME>(0, Current().frameDuration));
        return S_OK;
    }

    HRESULT Get(LPCSTR field, double* value)
    {
        if (!field || !value)
            return E_POINTER;
        if (_stricmp(field, "refreshRate"))
            return E_INVALIDARG;
        *value = 0; // allowed: unknown
        return S_OK;
    }

    static HRESULT Get(LPCSTR field, LPWSTR* value, int* chars)
    {
        if (!field || !value)
            return E_POINTER;
        const wchar_t* text = !_stricmp(field, "name")      ? L"FrankyRenderer"
                              : !_stricmp(field, "version") ? L"1.1"
                                                            : nullptr;
        if (!text)
            return E_INVALIDARG;
        const size_t length = wcslen(text);
        *value = static_cast<LPWSTR>(LocalAlloc(LMEM_FIXED, (length + 1) * sizeof(wchar_t)));
        if (!*value)
            return E_OUTOFMEMORY;
        memcpy(*value, text, (length + 1) * sizeof(wchar_t));
        if (chars)
            *chars = int(length);
        return S_OK;
    }

  private:
    HRESULT Prepare(ID3D11Device* device)
    {
        if (this->device.Get() == device && blend)
            return S_OK;
        bitmaps.clear();
        this->device = device;
        vertexShader.Reset();
        pixelShader.Reset();
        layout.Reset();
        vertices.Reset();
        sampler.Reset();
        blend.Reset();
        invertedBlend.Reset();
        rasterizer.Reset();
        HRESULT hr = device->CreateVertexShader(OverlayVS, sizeof(OverlayVS), nullptr, &vertexShader);
        if (SUCCEEDED(hr))
            hr = device->CreatePixelShader(OverlayPS, sizeof(OverlayPS), nullptr, &pixelShader);
        if (SUCCEEDED(hr))
        {
            const D3D11_INPUT_ELEMENT_DESC elements[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            hr = device->CreateInputLayout(elements, 2, OverlayVS, sizeof(OverlayVS), &layout);
        }
        if (SUCCEEDED(hr))
        {
            D3D11_BUFFER_DESC buffer = {};
            buffer.ByteWidth = 4 * 5 * sizeof(float);
            buffer.Usage = D3D11_USAGE_DYNAMIC;
            buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = device->CreateBuffer(&buffer, nullptr, &vertices);
        }
        if (SUCCEEDED(hr))
        {
            D3D11_SAMPLER_DESC sample = {};
            sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sample.MaxLOD = D3D11_FLOAT32_MAX;
            hr = device->CreateSamplerState(&sample, &sampler);
        }
        if (SUCCEEDED(hr))
        {
            // Premultiplied "over". The host's target stores 1 - alpha until the
            // renderer inverts it afterwards: there coverage multiplies instead.
            D3D11_BLEND_DESC desc = {};
            auto& target = desc.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D11_BLEND_ONE;
            target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D11_BLEND_ONE;
            target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            hr = device->CreateBlendState(&desc, &blend);
            if (SUCCEEDED(hr))
            {
                target.SrcBlendAlpha = D3D11_BLEND_ZERO;
                hr = device->CreateBlendState(&desc, &invertedBlend);
            }
        }
        if (SUCCEEDED(hr))
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            desc.CullMode = D3D11_CULL_NONE;
            desc.ScissorEnable = TRUE;
            desc.DepthClipEnable = TRUE;
            hr = device->CreateRasterizerState(&desc, &rasterizer);
        }
        if (FAILED(hr))
            blend.Reset();
        return hr;
    }

    HRESULT
    Draw(ID3D11DeviceContext* context, ISubRenderFrame* source, int width, int height, const RECT& video, bool inverted)
    {
        RECT output = {}, clip = {};
        int count = 0;
        if (FAILED(source->GetOutputRect(&output)) || FAILED(source->GetClipRect(&clip)) ||
            FAILED(source->GetBitmapCount(&count)) || output.right <= output.left || output.bottom <= output.top)
            return S_FALSE;
        // Bitmaps are positioned for "output"; map that onto the drawn video.
        const double sx = double(video.right - video.left) / (output.right - output.left);
        const double sy = double(video.bottom - video.top) / (output.bottom - output.top);
        auto mapX = [&](double x) { return video.left + (x - output.left) * sx; };
        auto mapY = [&](double y) { return video.top + (y - output.top) * sy; };
        D3D11_RECT scissor = {LONG(std::floor(std::max(0., mapX(clip.left)))),
                              LONG(std::floor(std::max(0., mapY(clip.top)))),
                              LONG(std::ceil(std::min(double(width), mapX(clip.right)))),
                              LONG(std::ceil(std::min(double(height), mapY(clip.bottom))))};
        if (scissor.right <= scissor.left || scissor.bottom <= scissor.top)
            return S_FALSE;

        const UINT stride = 5 * sizeof(float), offset = 0;
        auto buffer = vertices.Get();
        auto sampling = sampler.Get();
        context->IASetInputLayout(layout.Get());
        context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(vertexShader.Get(), nullptr, 0);
        context->PSSetShader(pixelShader.Get(), nullptr, 0);
        context->PSSetSamplers(0, 1, &sampling);
        context->RSSetState(rasterizer.Get());
        context->RSSetScissorRects(1, &scissor);
        context->OMSetBlendState(inverted ? invertedBlend.Get() : blend.Get(), nullptr, 0xffffffff);

        std::map<ULONGLONG, ComPtr<ID3D11ShaderResourceView>> used;
        for (int index = 0; index < count; ++index)
        {
            ULONGLONG id = 0;
            POINT position = {};
            SIZE size = {};
            LPCVOID pixels = nullptr;
            int pitch = 0;
            if (FAILED(source->GetBitmap(index, &id, &position, &size, &pixels, &pitch)) || size.cx <= 0 ||
                size.cy <= 0 || !pixels || pitch < size.cx * 4)
                continue;
            auto view = bitmaps.count(id) ? bitmaps[id] : nullptr;
            if (!view)
            {
                // Premultiplied RGBA in memory order B, G, R, A.
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = UINT(size.cx);
                desc.Height = UINT(size.cy);
                desc.MipLevels = desc.ArraySize = 1;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Usage = D3D11_USAGE_IMMUTABLE;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA initial = {pixels, UINT(pitch), 0};
                ComPtr<ID3D11Texture2D> texture;
                if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)) ||
                    FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)))
                    continue;
            }
            used[id] = view;
            const float x0 = float(mapX(position.x) / width * 2 - 1),
                        x1 = float(mapX(position.x + size.cx) / width * 2 - 1);
            const float y0 = float(1 - mapY(position.y) / height * 2),
                        y1 = float(1 - mapY(position.y + size.cy) / height * 2);
            const float quad[] = {x0, y0, 0, 0, 0, x1, y0, 0, 1, 0, x0, y1, 0, 0, 1, x1, y1, 0, 1, 1};
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                continue;
            memcpy(mapped.pData, quad, sizeof(quad));
            context->Unmap(buffer, 0);
            auto resource = view.Get();
            context->PSSetShaderResources(0, 1, &resource);
            context->Draw(4, 0);
        }
        ID3D11ShaderResourceView* none = nullptr;
        context->PSSetShaderResources(0, 1, &none);
        // Keep only what this picture used; unchanged bitmaps keep their IDs.
        bitmaps = std::move(used);
        return S_OK;
    }

    std::mutex lock;
    ComPtr<ISubRenderProvider> provider;
    ComPtr<ISubRenderFrame> frame;
    uint64_t requested = 0, frameRequest = 0;
    REFERENCE_TIME frameStart = 0;
    HANDLE delivered = nullptr;
    Info info;

    // Render thread only.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend, invertedBlend;
    ComPtr<ID3D11RasterizerState> rasterizer;
    std::map<ULONGLONG, ComPtr<ID3D11ShaderResourceView>> bitmaps;
};
