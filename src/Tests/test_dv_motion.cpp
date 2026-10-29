#include "Renderer/Backend.h"
#include "DolbyVision/Transport.h"
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#define OutputDebugStringA(message) fputs(message, stderr)
#include "Renderer/Backend.cpp"
#undef OutputDebugStringA

using Microsoft::WRL::ComPtr;

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

class LayerSample final : public IDvLayerSample
{
    ULONG refs = 1;
    DvFrameInfo info = {};
    ComPtr<ID3D11Texture2D> planes[2][3];

  public:
    LayerSample(ID3D11Device* device, unsigned w, unsigned h, unsigned ew, unsigned eh, unsigned residual)
    {
        info.version = DvTransportVersion;
        info.placeboApi = PL_API_VER;
        info.bytes = sizeof(info);
        info.width[0] = w;
        info.height[0] = h;
        info.width[1] = ew;
        info.height[1] = eh;
        info.color = pl_color_space_hdr10;
        info.dovi.nlq_active = ew != 0;
        for (int p = 0; p < 3; ++p)
        {
            info.dovi.nonlinear.m[p][p] = info.dovi.linear.m[p][p] = 1;
            info.dovi.comp[p].num_pivots = 2;
            info.dovi.comp[p].pivots[1] = 1;
            info.dovi.comp[p].poly_coeffs[0][1] = 1;
            info.dovi.nlq[p] = {512, .001f, 0, 1};
        }
        for (int l = 0; l < (ew ? 2 : 1); ++l)
            for (int p = 0; p < 3; ++p)
            {
                const unsigned width = p ? (info.width[l] + 1) / 2 : info.width[l];
                const unsigned height = p ? (info.height[l] + 1) / 2 : info.height[l];
                std::vector<uint16_t> data(size_t(width) * height, uint16_t(l ? residual : 400));
                D3D11_TEXTURE2D_DESC desc = {};
                desc.Width = width;
                desc.Height = height;
                desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
                desc.Format = DXGI_FORMAT_R16_UNORM;
                desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA initial = {data.data(), width * 2, 0};
                Require(SUCCEEDED(device->CreateTexture2D(&desc, &initial, &planes[l][p])), "sample plane allocation");
            }
    }

    VideoFrame Description() const
    {
        VideoFrame frame;
        frame.width = info.width[0];
        frame.height = info.height[0];
        frame.stride = frame.width * 2;
        frame.format = PixelFormat::P010;
        frame.color = info.color;
        frame.dovi = std::make_shared<pl_dovi_metadata>(info.dovi);
        return frame;
    }

    STDMETHODIMP QueryInterface(REFIID id, void** out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != __uuidof(IDvLayerSample))
            return E_NOINTERFACE;
        *out = this;
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

    STDMETHODIMP GetInfo(UINT version, UINT bytes, DvFrameInfo* out) override
    {
        if (version != info.version || bytes != sizeof(info) || !out)
            return E_INVALIDARG;
        *out = info;
        return S_OK;
    }

    STDMETHODIMP GetPlane(UINT l, UINT p, ID3D11Texture2D** out) override
    {
        if (!out || l >= 2 || p >= 3)
            return E_INVALIDARG;
        return planes[l][p].CopyTo(out);
    }
};

static std::vector<uint8_t> Pixels(PlaceboBackend& backend)
{
    std::vector<uint8_t> pixels;
    const auto result = backend.RenderOffscreen(128, 72, pixels);
    if (FAILED(result))
        printf("Render failed %08lx: %s\n", result, backend.LastError().c_str());
    Require(SUCCEEDED(result), "render actual DV/mixed GPU pixels");
    return pixels;
}

static void Run(unsigned w, unsigned h, unsigned ew, unsigned eh)
{
    ComPtr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,
                                        D3D_DRIVER_TYPE_HARDWARE,
                                        nullptr,
                                        0,
                                        levels,
                                        UINT(std::size(levels)),
                                        D3D11_SDK_VERSION,
                                        &device,
                                        nullptr,
                                        nullptr)),
            "D3D11 device");
    PlaceboBackend backend;
    Require(SUCCEEDED(backend.SetDevice(device.Get())), "backend device");
    RenderSettings settings;
    settings.smoothMotion = true;
    settings.peakDetect = false;
    settings.ditherMethod = 3;
    Require(SUCCEEDED(backend.SetSettings(settings)), "motion settings");
    uint64_t ids[2] = {};
    std::vector<uint8_t> expected[2];
    for (int i = 0; i < 2; ++i)
    {
        ComPtr<LayerSample> sample;
        sample.Attach(new LayerSample(device.Get(), w, h, ew, eh, i ? 600 : 512));
        const auto frame = sample->Description();
        Require(SUCCEEDED(backend.ImportDolbyVision(frame, sample.Get())), "direct DV import");
        expected[i] = Pixels(backend);
        Require(SUCCEEDED(backend.CaptureDolbyVision(frame, sample.Get(), ids[i])),
                "capture BL/EL into temporal history");
        sample.Reset();
        Require(Pixels(backend) == expected[i], "capture preserves directly displayed BL/EL ownership");
    }
    if (ew)
        Require(expected[0] != expected[1], "actual FEL residual changes rendered pixels");
    for (int i = 0; i < 2; ++i)
    {
        Require(SUCCEEDED(backend.SelectField(ids[i], 0, 0, PL_FIELD_NONE, PL_FIELD_NONE)), "select stored DV frame");
        Require(Pixels(backend) == expected[i], "stored frame keeps its own EL and metadata after sample release");
    }
    Require(SUCCEEDED(backend.SelectMix(ids[0], 0, ids[1], PL_FIELD_NONE, PL_FIELD_NONE, PL_FIELD_NONE, .95f, .25f)),
            "select layered oversample mix");
    const auto mixed = Pixels(backend);
    if (ew)
        Require(mixed != expected[0] && mixed != expected[1], "oversample blends actual FEL pictures");
    for (int i = 0; i < 20; ++i)
    {
        ComPtr<LayerSample> sample;
        sample.Attach(new LayerSample(device.Get(), w, h, ew, eh, 550));
        uint64_t id = 0;
        Require(SUCCEEDED(backend.CaptureDolbyVision(sample->Description(), sample.Get(), id)),
                "capture while displaying mix");
        sample.Reset();
        Require(Pixels(backend) == mixed, "history eviction/capture must preserve both displayed FEL frames");
    }
    std::vector<TemporalFrame> temporal{{ids[0], 0, 400000, PL_FIELD_NONE}, {ids[1], 400000, 800000, PL_FIELD_NONE}};
    settings.scaling.emplace();
    strcpy_s(settings.scaling->filters[4].filter, "mitchell");
    Require(SUCCEEDED(backend.SetSettings(settings)), "temporal filter settings");
    Pixels(backend); // Apply the selected filter and its temporal radius.
    Require(
        SUCCEEDED(backend.SelectTemporalMix(temporal, ids[0], 0, ids[1], PL_FIELD_NONE, PL_FIELD_NONE, 380000, .25f)),
        "select temporal DV mixer");
    const auto temporalPixels = Pixels(backend);
    if (ew)
        Require(temporalPixels != expected[0] && temporalPixels != expected[1],
                "temporal filter blends actual FEL pictures");
    backend.Flush();
    printf("PASS DV temporal GPU ownership and pixels: BL %ux%u EL %ux%u\n", w, h, ew, eh);
}

int main()
{
    try
    {
        Run(128, 72, 64, 36);
        Run(64, 64, 64, 64);
        Run(66, 34, 33, 17);
        Run(128, 72, 0, 0);
        return 0;
    }
    catch (const std::exception& error)
    {
        puts(error.what());
        return 1;
    }
}
