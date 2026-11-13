#define NOMINMAX
#include "Renderer/RtxScaling.h"
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
                    Require(scaling.Status().find(upscale ? L"Super Resolution + HDR" : L"(HDR)") != std::wstring::npos,
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
            Require(scaling.DecoderPicture() && scaling.Status().find(L"decoder picture") != std::wstring::npos,
                    "decoder picture is read by the driver directly");
            D3D11_TEXTURE2D_DESC actual;
            processed->pixels->GetDesc(&actual);
            Require(actual.Format == (eightBit ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R10G10B10A2_UNORM),
                    "output depth follows the decoder picture");
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
            const int bitsOut = eightBit ? 8 : 10, mask = (1 << bitsOut) - 1;
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
