#pragma once
#include "Renderer/Trace.h"
#include "Renderer/RtxVideoProcessor.h"
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/filters.h>
#include <algorithm>
#include <map>
#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <cstring>

// Convert/deinterlace each decoded image once, upscale it, then let the normal
// renderer mix, color-map and composite subtitles at the presentation rate.
class RtxScaling
{
  public:
    struct Frame
    {
        pl_gpu gpu = nullptr;
        pl_tex texture = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pixels;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> view;
        pl_frame image = {};
        pl_rect2df sourceCrop = {};
        const pl_frame* previous = nullptr;
        const pl_frame* next = nullptr;
        pl_field firstField = PL_FIELD_NONE;
        // The driver's number for this video frame and the picture size.
        UINT index = 0;
        int width = 0, height = 0;

        ~Frame()
        {
            if (texture)
                pl_tex_destroy(gpu, &texture);
        }
    };

  private:
    RtxVideoProcessor processor;
    pl_gpu gpu = nullptr;
    pl_renderer converter = nullptr;
    pl_tex input = nullptr;
    pl_color_space sourceColor = {};
    std::map<uint64_t, std::shared_ptr<Frame>> frames;

    // Output textures of frames that left the cache, used again for new
    // frames instead of creating a texture per frame (as MPC Video Renderer
    // keeps its output). Cleared whenever the output size or format changes.
    struct Spare
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> pixels;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> view;
        pl_tex texture = nullptr;
        int width = 0, height = 0;
    };

    // Signals when libplacebo finished writing the RTX input texture.
    Microsoft::WRL::ComPtr<ID3D11Query> written;

    std::vector<Spare> spare;
    // The last video frame number given to the driver; each frame keeps its own.
    UINT inputIndex = 0;

    // Keeps the texture of a frame only the cache still holds.
    void Recycle(std::shared_ptr<Frame>& frame)
    {
        if (frame && frame.use_count() == 1 && frame->texture)
        {
            spare.push_back({std::move(frame->pixels),
                             std::move(frame->view),
                             std::exchange(frame->texture, nullptr),
                             frame->width,
                             frame->height});
        }
    }

    void ClearFrames()
    {
        frames.clear();
        nativeViews.clear();
        for (auto& texture : spare)
            pl_tex_destroy(gpu, &texture.texture);
        spare.clear();
    }

    int width = 0, height = 0, outputWidth = 0, outputHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    RtxVideoProcessor::InputKind inputKind = RtxVideoProcessor::InputKind::Rgba8;
    bool lastDirect = false;

    // Driver input views on recent decoder pictures, newest last; a view is
    // made once per picture, not once per evaluation.
    struct NativeView
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> view;
    };

    std::vector<NativeView> nativeViews;
    bool upscaling = false, hdr = false;
    bool failed = false;
    // Why the driver refused RTX Video HDR; Super Resolution stays available.
    HRESULT hdrRefused = S_OK;
    std::wstring hdrFailureDetail;
    uint64_t evaluations = 0, texturesCreated = 0;
    std::wstring status = L"off";

    void Fail(HRESULT result)
    {
        Trace("RTX Video failed: 0x%08X", unsigned(result));
        ClearFrames();
        if (input)
            pl_tex_destroy(gpu, &input);
        processor.Reset();
        wchar_t text[128];
        swprintf_s(text, L"unavailable (0x%08X); normal scaling", unsigned(result));
        status = text;
        failed = true;
    }

    std::wstring HdrRefusal() const
    {
        if (SUCCEEDED(hdrRefused))
            return {};
        wchar_t text[64];
        swprintf_s(text, L"; RTX HDR unavailable (0x%08X)", unsigned(hdrRefused));
        return std::wstring(text) + (hdrFailureDetail.empty() ? L"" : L" [" + hdrFailureDetail + L"]");
    }

    static pl_frame Describe(pl_tex texture, const pl_color_space& color, int bits)
    {
        pl_frame image = {};
        image.num_planes = 1;
        image.planes[0].texture = texture;
        image.planes[0].components = 3;
        for (int i = 0; i < 4; ++i)
            image.planes[0].component_mapping[i] = i < 3 ? i : -1;
        image.crop = {0, 0, float(texture->params.w), float(texture->params.h)};
        image.color = color;
        image.repr = pl_color_repr_rgb;
        image.repr.bits.sample_depth = image.repr.bits.color_depth = bits;
        return image;
    }

  public:
    ~RtxScaling()
    {
        Reset();
    }

    const std::wstring& Status() const
    {
        return status;
    }

    // The last frame was upscaled by RTX Video Super Resolution.
    bool Upscaling() const
    {
        return upscaling;
    }

    // The last frame went to the driver as the decoder picture: the driver,
    // not libplacebo, scaled its chroma.
    bool DecoderPicture() const
    {
        return lastDirect;
    }

    uint64_t Evaluations() const
    {
        return evaluations;
    }

    // Output textures created so far; frames that leave the cache give theirs back.
    uint64_t TexturesCreated() const
    {
        return texturesCreated;
    }

    void Invalidate()
    {
        ClearFrames();
        failed = false;
        hdrRefused = S_OK;
        hdrFailureDetail.clear();
        if (converter)
            pl_renderer_flush_cache(converter);
    }

    // RTX Video HDR may be requested: the driver accepted it so far.
    bool HdrUsable() const
    {
        return !failed && SUCCEEDED(hdrRefused);
    }

    // Stops requesting RTX Video HDR until the next Invalidate.
    void RefuseHdr(HRESULT reason, std::wstring detail = {})
    {
        Trace("RTX Video HDR refused: 0x%08X [%ls]", unsigned(reason), detail.c_str());
        hdrRefused = reason;
        hdrFailureDetail = std::move(detail);
    }

    void Reset()
    {
        Invalidate();
        if (input)
            pl_tex_destroy(gpu, &input);
        pl_renderer_destroy(&converter);
        processor.Reset();
        gpu = nullptr;
        width = height = outputWidth = outputHeight = 0;
        format = DXGI_FORMAT_UNKNOWN;
        upscaling = hdr = false;
        status = L"off";
    }

    // superResolution upscales to outW x outH; trueHdr turns an SDR source
    // into HDR10 (PQ, BT.2020) at that size or the source size.
    //
    // whole is the full picture (source.crop before black bars are cut) and
    // targetW x targetH the largest picture size: the driver's processor and
    // input are made for these, and source.crop and outW x outH only set its
    // rectangles. A crop that follows black bars or a zoom then costs one new
    // evaluation of the frame, not a new processor and new textures.
    std::shared_ptr<Frame> Get(pl_d3d11 d3d,
                               ID3D11DeviceContext* context,
                               const pl_frame& source,
                               uint64_t key,
                               int outW,
                               int outH,
                               const pl_render_params& options,
                               bool superResolution = true,
                               bool trueHdr = false,
                               const pl_rect2df* whole = nullptr,
                               int targetW = 0,
                               int targetH = 0,
                               ID3D11Texture2D* native = nullptr)
    {
        const int w = int(std::ceil(std::abs(source.crop.x1 - source.crop.x0)));
        const int h = int(std::ceil(std::abs(source.crop.y1 - source.crop.y0)));
        trueHdr = trueHdr && !pl_color_space_is_hdr(&source.color) && SUCCEEDED(hdrRefused);
        const bool upscale = superResolution && w >= 1 && h >= 1 && outW >= w && outH >= h && (outW > w || outH > h);
        if (w < 1 || h < 1 || (!upscale && !trueHdr))
        {
            status = L"bypassed (no upscaling)" + HdrRefusal();
            return {};
        }
        if (!upscale)
        {
            outW = w;
            outH = h;
        }
        // A progressive NV12 or P010 decoder picture goes to the driver as it is; the
        // picture texture is then the input and the crop is a rectangle in it.
        D3D11_TEXTURE2D_DESC nativeDesc = {};
        if (native)
            native->GetDesc(&nativeDesc);
        const bool direct = native &&
                            (nativeDesc.Format == DXGI_FORMAT_P010 || nativeDesc.Format == DXGI_FORMAT_NV12) &&
                            nativeDesc.ArraySize == 1 && source.field == PL_FIELD_NONE;
        const pl_rect2df full = whole ? *whole : source.crop;
        const float fullX = direct ? 0.f : std::min(full.x0, full.x1),
                    fullY = direct ? 0.f : std::min(full.y0, full.y1);
        const int inW = direct ? int(nativeDesc.Width) : std::max(w, int(std::ceil(std::abs(full.x1 - full.x0))));
        const int inH = direct ? int(nativeDesc.Height) : std::max(h, int(std::ceil(std::abs(full.y1 - full.y0))));
        const int maxW = std::max(outW, targetW), maxH = std::max(outH, targetH);
        // The part of the full picture to process.
        const LONG left =
            std::clamp(LONG(std::lround(std::min(source.crop.x0, source.crop.x1) - fullX)), 0L, LONG(inW));
        const LONG top = std::clamp(LONG(std::lround(std::min(source.crop.y0, source.crop.y1) - fullY)), 0L, LONG(inH));
        const RECT sourceRect = {left, top, std::min(LONG(inW), left + w), std::min(LONG(inH), top + h)};
        const RECT destinationRect = {0, 0, outW, outH};
        if (source.rotation || source.repr.dovi || source.repr.alpha != PL_ALPHA_UNKNOWN ||
            source.color.transfer == PL_COLOR_TRC_HLG || source.color.transfer == PL_COLOR_TRC_LINEAR ||
            (source.color.transfer == PL_COLOR_TRC_PQ && source.color.primaries != PL_COLOR_PRIM_BT_2020))
        {
            status = L"bypassed (unsupported source format)";
            return {};
        }
        if (failed)
            return {};
        // RTX Video HDR takes the 10-bit YCbCr input the driver path expects.
        const int bits = trueHdr || pl_color_space_is_hdr(&source.color) || source.repr.bits.color_depth > 8 ? 10 : 8;
        const auto type = bits == 10 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        const bool pq = source.color.transfer == PL_COLOR_TRC_PQ;
        const bool bt2020 = source.color.primaries == PL_COLOR_PRIM_BT_2020;
        const auto kind = !direct ? RtxVideoProcessor::PreparedInput(bits, pq, bt2020, trueHdr)
                          : nativeDesc.Format == DXGI_FORMAT_P010 ? RtxVideoProcessor::InputKind::P010
                                                                  : RtxVideoProcessor::InputKind::Nv12;
        // A larger picture than the processor was made for needs a new one;
        // a smaller one fits into it.
        // HDR10+ metadata belongs to each cached image and is used by the
        // subsequent tone mapping. It does not change RTX's signal format.
        // Comparing it here evicts both neighbours on every Smooth Motion
        // refresh when their scene metadata differs.
        if (width != inW || height != inH || outputWidth < outW || outputHeight < outH || format != type ||
            inputKind != kind || sourceColor.primaries != source.color.primaries ||
            sourceColor.transfer != source.color.transfer || upscaling != upscale || hdr != trueHdr)
        {
            ClearFrames();
            if (input)
                pl_tex_destroy(gpu, &input);
            width = inW;
            height = inH;
            outputWidth = maxW;
            outputHeight = maxH;
            sourceColor = source.color;
            format = type;
            inputKind = kind;
            upscaling = upscale;
            hdr = trueHdr;
        }
        // A frame evaluated again keeps its number; a new one gets the next.
        UINT index = inputIndex + 1;
        if (const auto found = frames.find(key); found != frames.end())
        {
            const auto& saved = found->second;
            const bool crop = !memcmp(&saved->sourceCrop, &source.crop, sizeof(source.crop));
            const bool size = saved->width == outW && saved->height == outH;
            if (crop && size && saved->previous == source.prev && saved->next == source.next &&
                saved->firstField == source.first_field)
            {
                status = lastDirect ? L"driver requested (cached), decoder picture"
                                    : L"driver requested (cached), prepared picture";
                status += HdrRefusal();
                return saved;
            }
            index = saved->index;
        }
        gpu = d3d->gpu;
        HRESULT hr =
            processor.Configure(d3d->device, inW, inH, outputWidth, outputHeight, kind, pq, bt2020, upscale, trueHdr);
        if (FAILED(hr) && trueHdr)
        {
            // Only RTX Video HDR was refused: go on without it.
            RefuseHdr(hr, processor.FailureDescription());
            return Get(d3d,
                       context,
                       source,
                       key,
                       outW,
                       outH,
                       options,
                       superResolution,
                       false,
                       whole,
                       targetW,
                       targetH,
                       native);
        }
        if (FAILED(hr))
        {
            Fail(hr);
            return {};
        }
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> picture;
        if (direct)
        {
            for (const auto& known : nativeViews)
                if (known.texture.Get() == native)
                    picture = known.view;
            if (!picture)
            {
                hr = processor.InputView(native, picture);
                if (FAILED(hr))
                {
                    Fail(hr);
                    return {};
                }
                if (nativeViews.size() >= 8)
                    nativeViews.erase(nativeViews.begin());
                nativeViews.push_back({native, picture});
            }
        }
        else
        {
            if (!converter)
                converter = pl_renderer_create(nullptr, gpu);
            if (!converter)
            {
                Fail(E_OUTOFMEMORY);
                return {};
            }
            D3D11_TEXTURE2D_DESC inputDesc;
            processor.Input()->GetDesc(&inputDesc);
            // One plane: RGBA for 8 bits, RGB or Y410 (written as R10G10B10A2) for 10.
            if (!input)
            {
                pl_d3d11_wrap_params wrap = {};
                wrap.tex = processor.Input();
                if (kind != RtxVideoProcessor::InputKind::Rgba8)
                {
                    wrap.fmt = DXGI_FORMAT_R10G10B10A2_UNORM;
                    wrap.w = inputDesc.Width;
                    wrap.h = inputDesc.Height;
                }
                input = pl_d3d11_wrap(gpu, &wrap);
                if (!input || !input->params.renderable)
                {
                    Fail(E_FAIL);
                    return {};
                }
            }
            auto prepared = Describe(input, source.color, bits);
            prepared.crop = {0, 0, float(inW), float(inH)};
            pl_frame picture = source;
            picture.crop = {fullX, fullY, fullX + float(inW), fullY + float(inH)};
            if (kind == RtxVideoProcessor::InputKind::Y410)
            {
                // Y410 keeps Cb, Y and Cr in the red, green and blue bits.
                prepared.planes[0].component_mapping[0] = 1;
                prepared.planes[0].component_mapping[1] = 0;
                prepared.planes[0].component_mapping[2] = 2;
                prepared.repr.sys = bt2020 ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
                prepared.repr.levels = PL_COLOR_LEVELS_LIMITED;
                prepared.repr.bits = {10, 10, 0};
            }
            pl_render_params conversion = pl_render_fast_params;
            conversion.upscaler = conversion.downscaler = &pl_filter_nearest;
            conversion.plane_upscaler = options.plane_upscaler ? options.plane_upscaler
                                        : options.upscaler     ? options.upscaler
                                                               : &pl_filter_bilinear;
            conversion.plane_downscaler = options.plane_downscaler ? options.plane_downscaler
                                          : options.downscaler     ? options.downscaler
                                                                   : &pl_filter_bilinear;
            conversion.antiringing_strength = options.antiringing_strength;
            conversion.deinterlace_params = options.deinterlace_params;
            pl_color_map_params mapping = pl_color_map_default_params;
            mapping.tone_mapping_function = &pl_tone_map_clip;
            mapping.gamut_mapping = &pl_gamut_map_clip;
            mapping.contrast_recovery = 0;
            conversion.color_map_params = &mapping;
            if (!pl_render_image(converter, &picture, &prepared, &conversion))
            {
                Fail(E_FAIL);
                return {};
            }
            pl_gpu_flush(gpu);
            // The driver's RTX work may run apart from libplacebo's rendering:
            // start it only once the input texture is completely written.
            if (!written)
            {
                D3D11_QUERY_DESC query = {D3D11_QUERY_EVENT, 0};
                d3d->device->CreateQuery(&query, &written);
            }
            if (written)
            {
                context->End(written.Get());
                const uint64_t deadline = GetTickCount64() + 100;
                while (context->GetData(written.Get(), nullptr, 0, 0) == S_FALSE && GetTickCount64() < deadline)
                    SwitchToThread();
            }
        }
        inputIndex = std::max(inputIndex, index);
        auto saved = std::make_shared<Frame>();
        saved->gpu = gpu;
        saved->sourceCrop = source.crop;
        saved->previous = source.prev;
        saved->next = source.next;
        saved->firstField = source.first_field;
        saved->index = index;
        saved->width = outW;
        saved->height = outH;
        if (frames.size() >= 16 && !frames.count(key))
        {
            auto oldest = frames.begin();
            Recycle(oldest->second);
            frames.erase(oldest);
        }
        // Textures of another picture size are of no further use.
        while (!spare.empty() && (spare.back().width != outW || spare.back().height != outH))
        {
            pl_tex_destroy(gpu, &spare.back().texture);
            spare.pop_back();
        }
        if (!spare.empty())
        {
            saved->pixels = std::move(spare.back().pixels);
            saved->view = std::move(spare.back().view);
            saved->texture = spare.back().texture;
            spare.pop_back();
        }
        else
        {
            D3D11_TEXTURE2D_DESC desc;
            processor.Output()->GetDesc(&desc);
            desc.Width = outW;
            desc.Height = outH;
            desc.MiscFlags = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            hr = d3d->device->CreateTexture2D(&desc, nullptr, &saved->pixels);
            ++texturesCreated;
            if (SUCCEEDED(hr))
                hr = processor.OutputView(saved->pixels.Get(), saved->view);
            if (FAILED(hr))
            {
                Fail(hr);
                return {};
            }
            pl_d3d11_wrap_params wrap = {};
            wrap.tex = saved->pixels.Get();
            saved->texture = pl_d3d11_wrap(gpu, &wrap);
            if (!saved->texture)
            {
                Fail(E_FAIL);
                return {};
            }
        }
        // Like MPC Video Renderer: the driver writes into the frame's own
        // texture; no copy that could start before RTX finished.
        hr = processor.Evaluate(index, saved->view.Get(), &sourceRect, &destinationRect, picture.Get());
        // A newly created processor can return broken lines once: process its
        // first frame twice and keep the second result.
        if (SUCCEEDED(hr) && processor.TakeFresh())
            hr = processor.Evaluate(index, saved->view.Get(), nullptr, nullptr, picture.Get());
        if (FAILED(hr))
        {
            Fail(hr);
            return {};
        }
        saved->image = Describe(saved->texture, trueHdr ? pl_color_space_hdr10 : source.color, trueHdr ? 10 : bits);
        if (const auto previous = frames.find(key); previous != frames.end())
            Recycle(previous->second);
        frames[key] = saved;
        ++evaluations;
        status = upscale && trueHdr ? L"driver requested (Super Resolution + HDR)"
                 : trueHdr          ? L"driver requested (HDR)"
                                    : L"driver requested";
        status += direct ? L", decoder picture" : L", prepared picture";
        status += HdrRefusal();
        lastDirect = direct;
        return saved;
    }
};
