#define NOMINMAX
#include "RtxScaling.h"
#include <cstdio>
#include <vector>
#include <stdexcept>

static void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        auto params = pl_d3d11_default_params;
        auto d3d = pl_d3d11_create(nullptr, &params);
        Require(d3d != nullptr, "D3D11 GPU");
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        d3d->device->GetImmediateContext(&context);
        {
            RtxScaling scaling;
            for (int mode : {0, 1, 2, 3})
            {
                const bool hdr = mode == 1;
                const bool tenBit = mode == 1 || mode == 2;
                constexpr int w = 960, h = 540;
                std::vector<float> pixels(w * h * 4);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const float nits[] = {100, 1000, 4000};
                        const float value = hdr ? pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits[std::min(2, x / (w / 3))])
                                                : .3f + .15f * std::sin(x * .6f) + .1f * std::cos(y * .7f);
                        auto p = &pixels[(y * w + x) * 4];
                        p[0] = value;
                        p[1] = hdr ? pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits[(std::min(2, x / (w / 3)) + 1) % 3])
                                   : value * .8f;
                        p[2] = hdr ? pl_hdr_rescale(PL_HDR_NITS, PL_HDR_PQ, nits[(std::min(2, x / (w / 3)) + 2) % 3])
                                   : value * .6f;
                        p[3] = 1;
                    }
                pl_tex_params texture = {};
                texture.w = w;
                texture.h = h;
                texture.sampleable = true;
                texture.format = pl_find_fmt(d3d->gpu, PL_FMT_FLOAT, 4, 32, 0, PL_FMT_CAP_SAMPLEABLE);
                texture.initial_data = pixels.data();
                auto input = pl_tex_create(d3d->gpu, &texture);
                Require(input != nullptr, "source GPU texture");
                pl_frame source = {};
                source.num_planes = 1;
                source.planes[0].texture = input;
                source.planes[0].components = 3;
                for (int i = 0; i < 4; ++i)
                    source.planes[0].component_mapping[i] = i < 3 ? i : -1;
                source.crop = {0, 0, float(w), float(h)};
                source.repr = pl_color_repr_rgb;
                source.repr.bits.sample_depth = source.repr.bits.color_depth = tenBit ? 10 : 8;
                source.color = hdr ? pl_color_space_hdr10 : pl_color_space_srgb;
                if (mode >= 2)
                    source.color.primaries = PL_COLOR_PRIM_BT_601_625;
                if (hdr)
                {
                    source.color.hdr.max_luma = 4000;
                    source.color.hdr.max_cll = 1000;
                    source.color.hdr.max_fall = 200;
                }
                const auto before = scaling.Evaluations();
                auto processed = scaling.Get(d3d, context.Get(), source, 123, 1920, 1080, pl_render_default_params);
                if (!processed)
                    wprintf(L"RTX status: %ls\n", scaling.Status().c_str());
                Require(bool(processed), "libplacebo to RTX to libplacebo conversion");
                Require(processed->image.color.primaries == source.color.primaries &&
                            processed->image.color.transfer == source.color.transfer &&
                            processed->image.color.hdr.max_cll == source.color.hdr.max_cll,
                        "HDR transfer and metadata preserved");
                Require(processed->image.field == PL_FIELD_NONE && processed->image.repr.sys == PL_COLOR_SYSTEM_RGB,
                        "processed frame is RGB without a second deinterlace");
                D3D11_TEXTURE2D_DESC actual;
                processed->pixels->GetDesc(&actual);
                Require(actual.Format == (tenBit ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM),
                        "10-bit HDR is not quantized to 8-bit");
                if (hdr)
                {
                    actual.Usage = D3D11_USAGE_STAGING;
                    actual.BindFlags = actual.MiscFlags = 0;
                    actual.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
                    Require(SUCCEEDED(d3d->device->CreateTexture2D(&actual, nullptr, &readback)), "HDR readback");
                    context->CopyResource(readback.Get(), processed->pixels.Get());
                    D3D11_MAPPED_SUBRESOURCE mapped;
                    Require(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "HDR readback map");
                    const auto row =
                        reinterpret_cast<const UINT*>(static_cast<const BYTE*>(mapped.pData) + 540 * mapped.RowPitch);
                    const float expected[] = {100, 1000, 4000};
                    bool preserved = true;
                    for (int band = 0; band < 3; ++band)
                        for (int channel = 0; channel < 3; ++channel)
                        {
                            const float target = expected[(band + channel) % 3];
                            const float code = float((row[320 + band * 640] >> (channel * 10)) & 1023) / 1023.f;
                            const float nits = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, code);
                            printf("RTX HDR channel %d target %.0f nits -> %.1f nits\n", channel, target, nits);
                            preserved &= std::abs(nits - target) < target * .06f;
                        }
                    context->Unmap(readback.Get(), 0);
                    Require(preserved, "HDR PQ luminance survives RTX without SDR conversion");
                }
                for (int tick = 0; tick < 144; ++tick)
                {
                    auto again = scaling.Get(d3d, context.Get(), source, 123, 1920, 1080, pl_render_default_params);
                    Require(again == processed, "repeated refresh reuses processed video image");
                }
                Require(scaling.Evaluations() == before + 1, "144 refreshes cause one RTX evaluation");
                Require(source.crop.x0 == 0 && source.crop.x1 == w, "source geometry unchanged");
                auto changed = scaling.Get(d3d, context.Get(), source, 124, 1920, 1080, pl_render_default_params);
                Require(bool(changed) && scaling.Evaluations() == before + 2, "new video frame is evaluated");
                Require(!scaling.Get(d3d, context.Get(), source, 124, w, h, pl_render_default_params),
                        "native-size output bypasses RTX");
                source.crop = {3, 5, 952, 532};
                Require(bool(scaling.Get(d3d, context.Get(), source, 125, 1920, 1080, pl_render_default_params)),
                        "odd cropped dimensions and P010 padding");
                source.color.transfer = PL_COLOR_TRC_HLG;
                Require(!scaling.Get(d3d, context.Get(), source, 126, 1920, 1080, pl_render_default_params),
                        "unsupported transfer bypasses without changing the source");
                processed.reset();
                changed.reset();
                scaling.Invalidate();
                pl_tex_destroy(d3d->gpu, &input);
            }
            scaling.Reset();
            auto warpParams = pl_d3d11_default_params;
            warpParams.force_software = true;
            auto warp = pl_d3d11_create(nullptr, &warpParams);
            Require(warp != nullptr, "WARP fallback GPU");
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> warpContext;
            warp->device->GetImmediateContext(&warpContext);
            pl_frame unsupported = {};
            unsupported.crop = {0, 0, 32, 32};
            unsupported.color = pl_color_space_srgb;
            Require(!scaling.Get(warp, warpContext.Get(), unsupported, 1, 64, 64, pl_render_default_params),
                    "non-NVIDIA device keeps normal scaling");
            Require(scaling.Status().find(L"unavailable") != std::wstring::npos, "fallback status");
            scaling.Reset();
            pl_d3d11_destroy(&warp);
        }
        pl_d3d11_destroy(&d3d);
        puts(
            "PASS RTX pipeline: libplacebo RGB preparation, 8/10-bit driver processing, HDR metadata, cache reuse across 144 refreshes, new source frames, bypass and lifetime cleanup");
        return 0;
    }
    catch (const std::exception& ex)
    {
        printf("FAIL RTX pipeline: %s\n", ex.what());
        return 1;
    }
}
