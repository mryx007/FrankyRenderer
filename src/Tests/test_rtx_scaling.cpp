#define NOMINMAX
#include "Renderer/RtxScaling.h"
#include "Renderer/ShaderWarmup.h"
#include <libplacebo/shaders/custom.h>
#include <libplacebo/cache.h>
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
                if (hdr)
                    Require(scaling.Status().find(L"prepared") != std::wstring::npos,
                            "prepared HDR picture goes to the driver as P010");
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
                // Issue #48: SDR goes to the driver as 10-bit RGB, also from 8 bits.
                Require(actual.Format == DXGI_FORMAT_R10G10B10A2_UNORM, "upscaled in 10 bits");
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
                {
                    // 40 more video frames: the 16-frame cache recycles its
                    // output textures instead of creating one per frame.
                    const auto created = scaling.TexturesCreated();
                    for (uint64_t frame = 200; frame < 240; ++frame)
                        Require(
                            bool(scaling.Get(d3d, context.Get(), source, frame, 1920, 1080, pl_render_default_params)),
                            "long run of video frames");
                    Require(scaling.TexturesCreated() - created <= 16, "output textures are reused");
                    printf("RTX output textures created for 40 frames: %llu\n",
                           (unsigned long long)(scaling.TexturesCreated() - created));
                }
                {
                    // Black bars move the crop and the zoom changes the picture
                    // size: the driver's processor and cache stay, the frame is
                    // evaluated once more and keeps its number.
                    const pl_rect2df whole = source.crop;
                    const auto params = pl_render_default_params;
                    auto older = scaling.Get(
                        d3d, context.Get(), source, 299, 1920, 1080, params, true, false, &whole, 1920, 1080);
                    auto first = scaling.Get(
                        d3d, context.Get(), source, 300, 1920, 1080, params, true, false, &whole, 1920, 1080);
                    Require(older && first && first->index > older->index, "frames get rising numbers");
                    const auto evaluated = scaling.Evaluations();
                    const auto textures = scaling.TexturesCreated();
                    pl_frame cropped = source;
                    cropped.crop = {0, 8, float(w), float(h) - 8};
                    auto moved = scaling.Get(
                        d3d, context.Get(), cropped, 300, 1920, 1048, params, true, false, &whole, 1920, 1080);
                    Require(moved && scaling.Evaluations() == evaluated + 1 && moved->index == first->index,
                            "a new crop evaluates the frame once and keeps its number");
                    Require(moved->image.crop.x1 == 1920 && moved->image.crop.y1 == 1048, "picture of the new size");
                    Require(scaling.TexturesCreated() - textures <= 1, "a new crop needs one texture of the new size");
                    Require(scaling.Get(
                                d3d, context.Get(), source, 299, 1920, 1080, params, true, false, &whole, 1920, 1080) ==
                                    older &&
                                scaling.Evaluations() == evaluated + 1,
                            "a new crop keeps the driver's processor and the other cached frames");
                    auto next = scaling.Get(
                        d3d, context.Get(), cropped, 301, 1920, 1048, params, true, false, &whole, 1920, 1080);
                    Require(next && next->index > moved->index, "the next frame gets the next number");
                }
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
            {
                // RTX Video HDR: an SDR source leaves the driver as HDR10, at
                // the source size alone or upscaled together with VSR.
                constexpr int w = 960, h = 540;
                std::vector<float> pixels(w * h * 4);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        auto p = &pixels[(y * w + x) * 4];
                        p[0] = p[1] = p[2] = float(x) / (w - 1);
                        p[3] = 1;
                    }
                pl_tex_params texture = {};
                texture.w = w;
                texture.h = h;
                texture.sampleable = true;
                texture.format = pl_find_fmt(d3d->gpu, PL_FMT_FLOAT, 4, 32, 0, PL_FMT_CAP_SAMPLEABLE);
                texture.initial_data = pixels.data();
                auto input = pl_tex_create(d3d->gpu, &texture);
                Require(input != nullptr, "SDR source GPU texture");
                pl_frame source = {};
                source.num_planes = 1;
                source.planes[0].texture = input;
                source.planes[0].components = 3;
                for (int i = 0; i < 4; ++i)
                    source.planes[0].component_mapping[i] = i < 3 ? i : -1;
                source.crop = {0, 0, float(w), float(h)};
                source.repr = pl_color_repr_rgb;
                source.repr.bits.sample_depth = source.repr.bits.color_depth = 8;
                source.color = pl_color_space_bt709;
                for (const bool upscale : {false, true})
                {
                    auto processed = scaling.Get(d3d,
                                                 context.Get(),
                                                 source,
                                                 upscale ? 201 : 200,
                                                 1920,
                                                 1080,
                                                 pl_render_default_params,
                                                 upscale,
                                                 true);
                    if (!processed)
                        wprintf(L"RTX HDR status: %ls\n", scaling.Status().c_str());
                    Require(bool(processed), "driver accepts the RTX Video HDR request");
                    Require(processed->image.color.transfer == PL_COLOR_TRC_PQ &&
                                processed->image.color.primaries == PL_COLOR_PRIM_BT_2020,
                            "RTX Video HDR output is described as HDR10");
                    D3D11_TEXTURE2D_DESC actual;
                    processed->pixels->GetDesc(&actual);
                    Require(actual.Format == DXGI_FORMAT_R10G10B10A2_UNORM &&
                                int(actual.Width) == (upscale ? 1920 : w) && int(actual.Height) == (upscale ? 1080 : h),
                            "RTX Video HDR output format and size");
                    Require(scaling.Status().starts_with(upscale ? L"Super Resolution + HDR" : L"HDR ·"),
                            "RTX Video HDR status");
                    actual.Usage = D3D11_USAGE_STAGING;
                    actual.BindFlags = actual.MiscFlags = 0;
                    actual.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
                    Require(SUCCEEDED(d3d->device->CreateTexture2D(&actual, nullptr, &readback)), "RTX HDR readback");
                    context->CopyResource(readback.Get(), processed->pixels.Get());
                    D3D11_MAPPED_SUBRESOURCE mapped;
                    Require(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "RTX HDR map");
                    const auto row = reinterpret_cast<const UINT*>(static_cast<const BYTE*>(mapped.pData) +
                                                                   actual.Height / 2 * mapped.RowPitch);
                    const auto nits = [&](UINT x)
                    { return pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, float((row[x] >> 10) & 1023) / 1023.f); };
                    const float dark = nits(actual.Width / 20), white = nits(actual.Width - 2);
                    context->Unmap(readback.Get(), 0);
                    printf("RTX Video HDR%s: dark %.1f nits, SDR white -> %.1f nits\n",
                           upscale ? " + VSR" : "",
                           dark,
                           white);
                    Require(white > 50 && white > dark * 4, "RTX Video HDR keeps the picture");
                }
                pl_frame hdrSource = source;
                hdrSource.color = pl_color_space_hdr10;
                auto passthrough =
                    scaling.Get(d3d, context.Get(), hdrSource, 202, 1920, 1080, pl_render_default_params, true, true);
                Require(bool(passthrough) && scaling.Status().find(L"HDR") == std::wstring::npos,
                        "HDR sources get Super Resolution only");
                passthrough.reset();
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
        for (int variant : {0, 1, 2})
        {
            // A decoder picture goes to the driver as it is: no prepared
            // picture, the crop is a rectangle in the decoder texture.
            // 0: P010 SDR, 1: P010 HDR10, 2: NV12 SDR.
            const bool hdrSource = variant == 1, eightBit = variant == 2;
            RtxScaling scaling;
            constexpr int w = 960, h = 540;
            std::vector<uint8_t> pixels(size_t(w) * h * 3 / 2 * (eightBit ? 1 : 2));
            if (eightBit)
            {
                std::fill(pixels.begin(), pixels.begin() + size_t(w) * h, uint8_t(128));
                std::fill(pixels.begin() + size_t(w) * h, pixels.end(), uint8_t(128));
            }
            else
            {
                auto words = reinterpret_cast<uint16_t*>(pixels.data());
                std::fill(words, words + size_t(w) * h * 3 / 2, uint16_t(512 << 6));
            }
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = w;
            desc.Height = h;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = eightBit ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010;
            desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            const D3D11_SUBRESOURCE_DATA initial = {pixels.data(), UINT(w * (eightBit ? 1 : 2)), 0};
            Microsoft::WRL::ComPtr<ID3D11Texture2D> decoded;
            Require(SUCCEEDED(d3d->device->CreateTexture2D(&desc, &initial, &decoded)), "decoder picture");
            pl_tex planes[2] = {};
            for (int i = 0; i < 2; ++i)
            {
                pl_d3d11_wrap_params wrap = {};
                wrap.tex = decoded.Get();
                wrap.fmt = eightBit ? (i ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8_UNORM)
                                    : (i ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R16_UNORM);
                wrap.w = w / (i ? 2 : 1);
                wrap.h = h / (i ? 2 : 1);
                planes[i] = pl_d3d11_wrap(d3d->gpu, &wrap);
                Require(planes[i] != nullptr, "decoder plane wrap");
            }
            pl_frame source = {};
            source.num_planes = 2;
            source.planes[0].texture = planes[0];
            source.planes[0].components = 1;
            source.planes[0].component_mapping[0] = 0;
            source.planes[1].texture = planes[1];
            source.planes[1].components = 2;
            source.planes[1].component_mapping[0] = 1;
            source.planes[1].component_mapping[1] = 2;
            source.repr.sys = hdrSource ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
            source.repr.levels = PL_COLOR_LEVELS_LIMITED;
            source.repr.bits = eightBit ? pl_bit_encoding{8, 8, 0} : pl_bit_encoding{16, 10, 6};
            source.color = hdrSource ? pl_color_space_hdr10 : pl_color_space_bt709;
            source.crop = {0, 0, float(w), float(h)};
            auto processed = scaling.Get(d3d,
                                         context.Get(),
                                         source,
                                         1,
                                         1920,
                                         1080,
                                         pl_render_default_params,
                                         true,
                                         false,
                                         nullptr,
                                         0,
                                         0,
                                         decoded.Get());
            if (!processed)
                wprintf(L"RTX status: %ls\n", scaling.Status().c_str());
            Require(bool(processed), "decoder picture through RTX");
            if (hdrSource)
            {
                // Smooth Motion alternates between adjacent pictures with their
                // own HDR10+ metadata. These change tone mapping, not RTX's
                // input format, and must not evict the other picture.
                auto following = source;
                source.color.hdr.scene_avg = 40;
                source.color.hdr.scene_max[0] = 350;
                following.color.hdr.scene_avg = 80;
                following.color.hdr.scene_max[0] = 700;
                scaling.Invalidate();
                auto first = scaling.Get(d3d,
                                         context.Get(),
                                         source,
                                         40,
                                         1920,
                                         1080,
                                         pl_render_default_params,
                                         true,
                                         false,
                                         nullptr,
                                         0,
                                         0,
                                         decoded.Get());
                auto second = scaling.Get(d3d,
                                          context.Get(),
                                          following,
                                          41,
                                          1920,
                                          1080,
                                          pl_render_default_params,
                                          true,
                                          false,
                                          nullptr,
                                          0,
                                          0,
                                          decoded.Get());
                Require(first && second, "adjacent HDR10+ pictures");
                const auto evaluations = scaling.Evaluations(), textures = scaling.TexturesCreated();
                for (int tick = 0; tick < 20; ++tick)
                {
                    Require(scaling.Get(d3d,
                                        context.Get(),
                                        source,
                                        40,
                                        1920,
                                        1080,
                                        pl_render_default_params,
                                        true,
                                        false,
                                        nullptr,
                                        0,
                                        0,
                                        decoded.Get()) == first,
                            "HDR10+ metadata does not evict the adjacent RTX picture");
                    Require(scaling.Get(d3d,
                                        context.Get(),
                                        following,
                                        41,
                                        1920,
                                        1080,
                                        pl_render_default_params,
                                        true,
                                        false,
                                        nullptr,
                                        0,
                                        0,
                                        decoded.Get()) == second,
                            "HDR10+ next picture stays cached");
                }
                Require(scaling.Evaluations() == evaluations && scaling.TexturesCreated() == textures,
                        "HDR10+ refreshes neither resubmit nor allocate RTX pictures");
                Require(first->image.color.hdr.scene_avg == 40 && second->image.color.hdr.scene_avg == 80,
                        "each RTX picture keeps its own HDR10+ tone mapping metadata");
                printf("RTX HDR10+ alternating metadata: 40 cache hits, no resubmissions or allocations\n");
            }
            // Issue #48: only HDR decoder pictures go to the driver directly;
            // SDR ones are prepared as 10-bit RGB (the driver's YCbCr
            // conversion is off), and every output has 10 bits.
            Require(scaling.DecoderPicture() == hdrSource &&
                        scaling.Status().find(hdrSource ? L"decoder picture" : L"prepared") != std::wstring::npos,
                    "HDR decoder pictures direct, SDR ones prepared");
            D3D11_TEXTURE2D_DESC actual;
            processed->pixels->GetDesc(&actual);
            Require(actual.Format == DXGI_FORMAT_R10G10B10A2_UNORM, "10-bit output");
            actual.Usage = D3D11_USAGE_STAGING;
            actual.BindFlags = actual.MiscFlags = 0;
            actual.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
            Require(SUCCEEDED(d3d->device->CreateTexture2D(&actual, nullptr, &readback)), "direct readback");
            context->CopyResource(readback.Get(), processed->pixels.Get());
            D3D11_MAPPED_SUBRESOURCE mapped;
            Require(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "direct readback map");
            const UINT pixel =
                reinterpret_cast<const UINT*>(static_cast<const BYTE*>(mapped.pData) + 540 * mapped.RowPitch)[960];
            context->Unmap(readback.Get(), 0);
            // Mid grey: about 0.51 of full scale in every channel.
            const int bitsOut = 10, mask = (1 << bitsOut) - 1;
            const int r = pixel & mask, g = (pixel >> bitsOut) & mask, b = (pixel >> (2 * bitsOut)) & mask;
            const float scale = float(mask);
            printf("RTX direct %s: grey -> %.3f %.3f %.3f\n",
                   variant == 0   ? "P010 SDR"
                   : variant == 1 ? "P010 HDR10"
                                  : "NV12 SDR",
                   r / scale,
                   g / scale,
                   b / scale);
            Require(std::abs(r - g) < mask / 80 + 2 && std::abs(g - b) < mask / 80 + 2 &&
                        std::abs(g / scale - .511f) < .03f,
                    "decoder picture keeps its grey");
            if (!hdrSource)
            {
                // RTX Video HDR on the decoder picture: plausible PQ output.
                auto hdr = scaling.Get(d3d,
                                       context.Get(),
                                       source,
                                       2,
                                       1920,
                                       1080,
                                       pl_render_default_params,
                                       true,
                                       true,
                                       nullptr,
                                       0,
                                       0,
                                       decoded.Get());
                if (!hdr)
                    wprintf(L"RTX status: %ls\n", scaling.Status().c_str());
                Require(bool(hdr) && scaling.Status().find(L"Super Resolution + HDR") != std::wstring::npos,
                        "RTX Video HDR on the decoder picture");
                D3D11_TEXTURE2D_DESC hdrDesc;
                hdr->pixels->GetDesc(&hdrDesc);
                hdrDesc.Usage = D3D11_USAGE_STAGING;
                hdrDesc.BindFlags = hdrDesc.MiscFlags = 0;
                hdrDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> hdrReadback;
                Require(SUCCEEDED(d3d->device->CreateTexture2D(&hdrDesc, nullptr, &hdrReadback)), "HDR readback");
                context->CopyResource(hdrReadback.Get(), hdr->pixels.Get());
                Require(SUCCEEDED(context->Map(hdrReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "HDR map");
                const UINT code =
                    reinterpret_cast<const UINT*>(static_cast<const BYTE*>(mapped.pData) + 540 * mapped.RowPitch)[960];
                context->Unmap(hdrReadback.Get(), 0);
                const float nits = pl_hdr_rescale(PL_HDR_PQ, PL_HDR_NITS, float((code >> 10) & 1023) / 1023.f);
                printf("RTX direct %s with RTX Video HDR: grey -> %.1f nits\n", eightBit ? "NV12" : "P010", nits);
                // The same as for a prepared picture (10.7 nits on a GeForce
                // RTX 4080 SUPER): RTX Video HDR keeps mid grey dark.
                Require(nits > 3 && nits < 400, "RTX Video HDR grey at a plausible level");
                for (bool vsr : {false, true})
                {
                    RtxVideoProcessor reporterSize;
                    Require(SUCCEEDED(reporterSize.Configure(d3d->device,
                                                             w,
                                                             h,
                                                             1797,
                                                             1011,
                                                             eightBit ? RtxVideoProcessor::InputKind::Nv12
                                                                      : RtxVideoProcessor::InputKind::P010,
                                                             false,
                                                             false,
                                                             vsr,
                                                             true)),
                            "RTX HDR at issue 21's odd output size, with and without VSR");
                    Require(reporterSize.FailureDescription().empty(), "successful HDR has no failure detail");
                }
            }
            {
                // Issue #51: user shaders that work before scaling run in the
                // conversion pass, so the decoder picture goes through it too.
                const auto lumaShader = [&](const char* value)
                {
                    const std::string text = std::string("//!HOOK LUMA\n//!BIND HOOKED\n//!DESC test luma\n"
                                                         "vec4 hook() { return vec4(") +
                                             value + "); }\n";
                    const pl_hook* hook = pl_mpv_user_shader_parse(d3d->gpu, text.data(), text.size());
                    Require(hook != nullptr, "test LUMA shader parses");
                    return hook;
                };
                const pl_hook* bright = lumaShader("0.9");
                const pl_hook* dark = lumaShader("0.1");
                const auto centre = [&](const std::shared_ptr<RtxScaling::Frame>& frame)
                {
                    D3D11_TEXTURE2D_DESC desc;
                    frame->pixels->GetDesc(&desc);
                    desc.Usage = D3D11_USAGE_STAGING;
                    desc.BindFlags = desc.MiscFlags = 0;
                    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy;
                    Require(SUCCEEDED(d3d->device->CreateTexture2D(&desc, nullptr, &copy)), "shader readback");
                    context->CopyResource(copy.Get(), frame->pixels.Get());
                    D3D11_MAPPED_SUBRESOURCE map;
                    Require(SUCCEEDED(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &map)), "shader readback map");
                    const UINT code = reinterpret_cast<const UINT*>(static_cast<const BYTE*>(map.pData) +
                                                                    desc.Height / 2 * map.RowPitch)[desc.Width / 2];
                    context->Unmap(copy.Get(), 0);
                    return int((code >> 10) & 1023);
                };
                // SDR sources with RTX Video HDR (the issue), HDR with Super Resolution.
                const bool trueHdr = !hdrSource;
                const auto get = [&](const pl_hook* hook, uint64_t hookKey)
                {
                    return scaling.Get(d3d,
                                       context.Get(),
                                       source,
                                       50,
                                       1920,
                                       1080,
                                       pl_render_default_params,
                                       true,
                                       trueHdr,
                                       nullptr,
                                       0,
                                       0,
                                       decoded.Get(),
                                       hook ? &hook : nullptr,
                                       hook ? 1 : 0,
                                       hookKey);
                };
                auto plain = get(nullptr, 0);
                Require(bool(plain) && scaling.DecoderPicture(), "without shaders the decoder picture goes direct");
                const int plainCode = centre(plain);
                auto lifted = get(bright, 1);
                if (!lifted)
                    wprintf(L"RTX status: %ls\n", scaling.Status().c_str());
                Require(bool(lifted) && !scaling.DecoderPicture() &&
                            scaling.Status().find(L"shaders first") != std::wstring::npos,
                        "a LUMA shader takes the conversion pass");
                const int liftedCode = centre(lifted);
                const auto evaluations = scaling.Evaluations();
                Require(get(bright, 1) == lifted && scaling.Evaluations() == evaluations,
                        "the same shaders keep the RTX picture cached");
                auto lowered = get(dark, 2);
                Require(bool(lowered) && scaling.Evaluations() == evaluations + 1,
                        "changed shaders evaluate the cached picture again");
                const int loweredCode = centre(lowered);
                printf("RTX %s with a LUMA shader: grey %d, luma 0.9 -> %d, luma 0.1 -> %d\n",
                       variant == 0   ? "P010 SDR + HDR"
                       : variant == 1 ? "P010 HDR10"
                                      : "NV12 SDR + HDR",
                       plainCode,
                       liftedCode,
                       loweredCode);
                Require(liftedCode > plainCode + 20 && loweredCode < plainCode - 20,
                        "the LUMA shader's result reaches the RTX output");
                Require(get(nullptr, 0) && scaling.DecoderPicture(), "without shaders again: decoder picture");
                Require(!scaling.TakeHooksFailed(), "working shaders are not dropped");
                plain.reset();
                lifted.reset();
                lowered.reset();
                pl_mpv_user_shader_destroy(&bright);
                pl_mpv_user_shader_destroy(&dark);
                puts("PASS user shaders before scaling run before RTX and refresh its cache");
            }
            if (variant != 0)
            {
                // Issue #51: the shader warm-up compiles RTX's conversion pass
                // on its own device, so the shader's first use there compiles
                // nothing (NV12 with RTX Video HDR, P010 HDR10 prepared as P010).
                auto cacheParams = pl_cache_default_params;
                cacheParams.max_object_size = 16 << 20;
                cacheParams.max_total_size = 64 << 20;
                pl_cache cache = pl_cache_create(&cacheParams);
                pl_gpu_set_cache(d3d->gpu, cache);
                const bool trueHdr = !hdrSource;
                RtxScaling fresh;
                const auto get = [&](const pl_hook* hook, uint64_t frame, uint64_t hookKey)
                {
                    return fresh.Get(d3d,
                                     context.Get(),
                                     source,
                                     frame,
                                     1920,
                                     1080,
                                     pl_render_default_params,
                                     true,
                                     trueHdr,
                                     nullptr,
                                     0,
                                     0,
                                     decoded.Get(),
                                     hook ? &hook : nullptr,
                                     hook ? 1 : 0,
                                     hookKey);
                };
                Require(bool(get(nullptr, 60, 0)), "RTX before the warm-up");
                RtxScaling::Conversion conversion;
                Require(fresh.DescribeConversion(source, 1920, 1080, true, trueHdr, nullptr, conversion),
                        "the conversion pass is described");
                ShaderWarmupJob job;
                job.conversion = DescribeWarmupConversion(conversion);
                Require(job.conversion.targets.size() == (hdrSource ? 2u : 1u), "the prepared planes are described");
                const std::string text = "//!HOOK LUMA\n//!BIND HOOKED\n//!DESC warm-up luma\n"
                                         "vec4 hook() { return HOOKED_tex(HOOKED_pos) * 0.75; }\n";
                const size_t before = pl_cache_size(cache);
                {
                    std::vector<uint8_t> bytes(pl_cache_save(cache, nullptr, 0));
                    pl_cache_save(cache, bytes.data(), bytes.size());
                    pl_cache workerCache = pl_cache_create(&cacheParams);
                    pl_cache_load(workerCache, bytes.data(), bytes.size());
                    auto workerParams = pl_d3d11_default_params;
                    pl_d3d11 worker = pl_d3d11_create(nullptr, &workerParams);
                    Require(worker != nullptr, "warm-up device");
                    pl_gpu_set_cache(worker->gpu, workerCache);
                    const pl_hook* hook = pl_mpv_user_shader_parse(worker->gpu, text.data(), text.size());
                    pl_renderer renderer = pl_renderer_create(nullptr, worker->gpu);
                    const bool warmed = hook && renderer && WarmUpConversion(worker->gpu, renderer, hook, job);
                    pl_renderer_destroy(&renderer);
                    pl_mpv_user_shader_destroy(&hook);
                    pl_gpu_set_cache(worker->gpu, nullptr);
                    pl_d3d11_destroy(&worker);
                    Require(warmed, "the warm-up renders the conversion pass");
                    bytes.resize(pl_cache_save(workerCache, nullptr, 0));
                    pl_cache_save(workerCache, bytes.data(), bytes.size());
                    pl_cache_destroy(&workerCache);
                    pl_cache_load(cache, bytes.data(), bytes.size());
                }
                const pl_hook* hook = pl_mpv_user_shader_parse(d3d->gpu, text.data(), text.size());
                Require(hook != nullptr, "playing device shader");
                const size_t warmed = pl_cache_size(cache);
                Require(warmed > before, "the warm-up adds the compiled conversion pass");
                auto shaded = get(hook, 61, 7);
                Require(bool(shaded) && fresh.Status().find(L"shaders first") != std::wstring::npos,
                        "RTX with the warmed-up shader");
                printf("RTX %s conversion pass: warm-up cache %zu -> %zu bytes, first use -> %zu bytes\n",
                       hdrSource ? "P010 HDR10" : "NV12 SDR + HDR",
                       before,
                       warmed,
                       pl_cache_size(cache));
                Require(pl_cache_size(cache) == warmed, "the warmed-up conversion pass compiles nothing new");
                shaded.reset();
                pl_mpv_user_shader_destroy(&hook);
                fresh.Reset();
                pl_gpu_set_cache(d3d->gpu, nullptr);
                pl_cache_destroy(&cache);
                puts("PASS the shader warm-up covers RTX's conversion pass");
            }
            if (variant == 0)
            {
                // A refused RTX Video HDR leaves Super Resolution working.
                RtxScaling refused;
                refused.RefuseHdr(E_INVALIDARG, L"HDR extension");
                Require(!refused.HdrUsable(), "refused RTX Video HDR is not usable");
                auto upscaled = refused.Get(d3d,
                                            context.Get(),
                                            source,
                                            3,
                                            1920,
                                            1080,
                                            pl_render_default_params,
                                            true,
                                            true,
                                            nullptr,
                                            0,
                                            0,
                                            decoded.Get());
                Require(bool(upscaled) && refused.Upscaling() && !pl_color_space_is_hdr(&upscaled->image.color),
                        "Super Resolution works without the refused RTX Video HDR");
                Require(refused.Status().find(L"RTX HDR unavailable") != std::wstring::npos,
                        "statistics name the refused RTX Video HDR");
                Require(refused.Status().find(L"HDR extension") != std::wstring::npos &&
                            refused.Status().find(L"80070057") != std::wstring::npos,
                        "statistics preserve the actual HDR error and failure stage");
                upscaled.reset();
                Require(!refused.Get(d3d,
                                     context.Get(),
                                     source,
                                     4,
                                     w,
                                     h,
                                     pl_render_default_params,
                                     true,
                                     true,
                                     nullptr,
                                     0,
                                     0,
                                     decoded.Get()),
                        "no upscaling and no RTX Video HDR leaves the picture to libplacebo");
                refused.Reset();
                Require(refused.HdrUsable(), "a new start tries RTX Video HDR again");
                puts("PASS refused RTX Video HDR keeps Super Resolution and is retried after a reset");
            }
            processed.reset();
            scaling.Reset();
            for (auto& plane : planes)
                pl_tex_destroy(d3d->gpu, &plane);
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
