#pragma once
// Field order measured from the picture (GitHub issue 53). Some broadcast
// streams mark every picture bottom field first although their fields are
// top first; deinterlacing in the flagged order then moves the picture
// back and forth (a news ticker judders).
//
// Two consecutive frames n and n+1 tell the order apart: for top field first
// the bottom field of n and the top field of n+1 are neighbours in time, so
// weaving them combs less than the top field of n with the bottom field of
// n+1, which lie three fields apart. For bottom field first it is the other
// way round. A still picture combs the same both ways and does not vote.
#include <libplacebo/renderer.h>
#include <libplacebo/d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>

// Mean comb (0-255) of the odd rows of `odd` against the mean of the even rows
// of `even` above and below them; both are 8-bit pictures of the same layout.
inline double
FieldComb(const uint8_t* odd, const uint8_t* even, int width, int height, size_t pitch, int step, int channel)
{
    double sum = 0;
    size_t count = 0;
    for (int y = 1; y + 1 < height; y += 2)
    {
        const uint8_t* line = odd + y * pitch;
        const uint8_t* above = even + (y - 1) * pitch;
        const uint8_t* below = even + (y + 1) * pitch;
        for (int x = 0; x < width; x++)
        {
            const int i = x * step + channel;
            sum += std::abs(2 * int(line[i]) - int(above[i]) - int(below[i]));
        }
        count += width;
    }
    return count ? sum / (2.0 * count) : 0;
}

// Votes of recent frame pairs and the field order they agree on.
class FieldOrderVotes
{
    std::deque<int> window; // +1 top field first, -1 bottom field first

  public:
    // The first decision takes a few unanimous pairs, so a wrongly flagged
    // stream is right after a few frames; changing it later takes most of
    // a longer window.
    static constexpr size_t First = 3, Window = 16, Needed = 12;
    // A clear difference only: still pictures, cuts and noise do not vote.
    static constexpr double MinimumDifference = .3, Ratio = 1.15;
    int decided = 0; // 0 unknown, 1 top field first, 2 bottom field first

    // bottomTop: comb of bottom(n) with top(n+1); topBottom: top(n) with bottom(n+1).
    void Observe(double bottomTop, double topBottom)
    {
        if (!std::isfinite(bottomTop) || !std::isfinite(topBottom) ||
            std::abs(topBottom - bottomTop) < MinimumDifference)
            return;
        const double ratio = topBottom / std::max(bottomTop, 1e-6);
        const int vote = ratio > Ratio ? 1 : ratio < 1 / Ratio ? -1 : 0;
        if (!vote)
            return;
        window.push_back(vote);
        if (window.size() > Window)
            window.pop_front();
        const auto top = size_t(std::count(window.begin(), window.end(), 1));
        if (!decided && window.size() >= First && (top == window.size() || top == 0))
            decided = top ? 1 : 2;
        else if (top >= Needed)
            decided = 1;
        else if (window.size() - top >= Needed)
            decided = 2;
    }

    void Reset()
    {
        window.clear();
        decided = 0;
    }
};

// Renders two consecutive frames at full height and reduced width, reads them
// back and votes. While the order is unknown the filter measures each new
// frame as it arrives (MeasureNow, waiting for the GPU for a few frames);
// afterwards the presented pairs are read back asynchronously twice a second
// to follow a change.
class FieldOrderAnalysis
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> targets[2], readbacks[2];
    pl_tex wrapped[2] = {};
    pl_renderer renderer = nullptr;
    pl_gpu gpu = nullptr;
    bool pending = false;
    int width = 0, height = 0, sw = 0, sh = 0;
    uint64_t measuredCurrent = 0, measuredNext = 0;
    ULONGLONG last = 0;
    int waited = 0;

    // A new picture size starts over; false when nothing can be measured.
    bool Usable(int sourceW, int sourceH)
    {
        if (sw != sourceW || sh != sourceH)
        {
            Reset();
            sw = sourceW;
            sh = sourceH;
        }
        // Interlaced video is at most 1080 lines high.
        return !failed && sourceW >= 16 && sourceH >= 16 && sourceH <= 1200;
    }

    // Reads the submitted pair and votes; false while the GPU is not done
    // (only without wait).
    bool Collect(ID3D11DeviceContext* context, bool wait)
    {
        if (!pending)
            return true;
        const UINT flags = wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT;
        D3D11_MAPPED_SUBRESOURCE mapped[2] = {};
        HRESULT hr = context->Map(readbacks[0].Get(), 0, D3D11_MAP_READ, flags, &mapped[0]);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
            return false;
        if (SUCCEEDED(hr))
        {
            hr = context->Map(readbacks[1].Get(), 0, D3D11_MAP_READ, flags, &mapped[1]);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
            {
                context->Unmap(readbacks[0].Get(), 0);
                return false;
            }
            if (FAILED(hr))
                context->Unmap(readbacks[0].Get(), 0);
        }
        pending = false;
        if (FAILED(hr))
        {
            failed = true;
            return true;
        }
        const auto* a = static_cast<const uint8_t*>(mapped[0].pData);
        const auto* b = static_cast<const uint8_t*>(mapped[1].pData);
        const size_t pitch = std::min(mapped[0].RowPitch, mapped[1].RowPitch);
        // Green carries most of the luma.
        votes.Observe(FieldComb(a, b, width, height, pitch, 4, 1), FieldComb(b, a, width, height, pitch, 4, 1));
        context->Unmap(readbacks[1].Get(), 0);
        context->Unmap(readbacks[0].Get(), 0);
        detected = votes.decided;
        return true;
    }

    // Renders both frames and starts their copy to the CPU.
    void Submit(pl_d3d11 d3d,
                ID3D11DeviceContext* context,
                const pl_frame& current,
                const pl_frame& next,
                uint64_t currentId,
                uint64_t nextId)
    {
        if (!targets[0])
        {
            gpu = d3d->gpu;
            width = std::min(sw, 480);
            height = sh;
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            HRESULT hr = S_OK;
            for (int i = 0; SUCCEEDED(hr) && i < 2; i++)
            {
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.CPUAccessFlags = 0;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                hr = d3d->device->CreateTexture2D(&desc, nullptr, &targets[i]);
                desc.BindFlags = 0;
                desc.Usage = D3D11_USAGE_STAGING;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                if (SUCCEEDED(hr))
                    hr = d3d->device->CreateTexture2D(&desc, nullptr, &readbacks[i]);
                if (SUCCEEDED(hr))
                {
                    pl_d3d11_wrap_params wrap = {};
                    wrap.tex = targets[i].Get();
                    wrapped[i] = pl_d3d11_wrap(gpu, &wrap);
                    if (!wrapped[i])
                        hr = E_FAIL;
                }
            }
            renderer = SUCCEEDED(hr) ? pl_renderer_create(nullptr, gpu) : nullptr;
            if (!renderer)
            {
                failed = true;
                return;
            }
        }
        // Both frames as stored, both fields woven, only narrower: the rows of
        // each field stay where they are.
        pl_render_params params = pl_render_fast_params;
        params.upscaler = params.downscaler = &pl_filter_bilinear;
        params.peak_detect_params = nullptr;
        params.sigmoid_params = nullptr;
        params.dither_params = nullptr;
        const pl_frame* sources[2] = {&current, &next};
        for (int i = 0; i < 2; i++)
        {
            pl_frame input = *sources[i];
            input.crop = {0, 0, float(sw), float(sh)};
            input.field = PL_FIELD_NONE;
            input.prev = input.next = nullptr;
            input.overlays = nullptr;
            input.num_overlays = 0;
            pl_frame output = {};
            output.num_planes = 1;
            output.planes[0].texture = wrapped[i];
            output.planes[0].components = 4;
            for (int c = 0; c < 4; c++)
                output.planes[0].component_mapping[c] = c;
            output.crop = {0, 0, float(width), float(height)};
            output.color = pl_color_space_srgb;
            output.color.hdr.max_luma = 203;
            output.repr.sys = PL_COLOR_SYSTEM_RGB;
            output.repr.levels = PL_COLOR_LEVELS_FULL;
            if (!pl_render_image(renderer, &input, &output, &params))
            {
                failed = true;
                return;
            }
        }
        pl_gpu_flush(gpu);
        context->CopyResource(readbacks[0].Get(), targets[0].Get());
        context->CopyResource(readbacks[1].Get(), targets[1].Get());
        pending = true;
        measuredCurrent = currentId;
        measuredNext = nextId;
        last = GetTickCount64();
    }

  public:
    // Frames measured while waiting, at most, before the asynchronous
    // measurement of presented frames takes over.
    static constexpr int MaxWaited = 12;
    FieldOrderVotes votes;
    std::atomic<int> detected = 0;
    bool failed = false;

    ~FieldOrderAnalysis()
    {
        Reset();
    }

    void Reset()
    {
        for (auto& tex : wrapped)
            if (gpu)
                pl_tex_destroy(gpu, &tex);
        pl_renderer_destroy(&renderer);
        for (int i = 0; i < 2; i++)
        {
            readbacks[i].Reset();
            targets[i].Reset();
        }
        gpu = nullptr;
        pending = failed = false;
        sw = sh = 0;
        measuredCurrent = measuredNext = 0;
        last = 0;
        waited = 0;
        votes.Reset();
        detected = 0;
    }

    // The presented pair: read back without waiting.
    void Analyze(pl_d3d11 d3d,
                 ID3D11DeviceContext* context,
                 const pl_frame& current,
                 const pl_frame& next,
                 uint64_t currentId,
                 uint64_t nextId,
                 int sourceW,
                 int sourceH)
    {
        if (!Usable(sourceW, sourceH) || !Collect(context, false))
            return;
        // The arriving frames are measured already.
        if (!votes.decided && waited > 0 && waited < MaxWaited)
            return;
        if (currentId == nextId || (currentId == measuredCurrent && nextId == measuredNext))
            return;
        if (votes.decided && last && GetTickCount64() - last < 500)
            return;
        Submit(d3d, context, current, next, currentId, nextId);
    }

    // A pair that just arrived, before it is shown: waits for the result, so
    // the order is known before the first frames are presented. Only while
    // the order is unknown, and only for the first frames of a stream.
    void MeasureNow(pl_d3d11 d3d,
                    ID3D11DeviceContext* context,
                    const pl_frame& current,
                    const pl_frame& next,
                    uint64_t currentId,
                    uint64_t nextId,
                    int sourceW,
                    int sourceH)
    {
        if (!Usable(sourceW, sourceH) || votes.decided || waited >= MaxWaited || currentId == nextId ||
            (currentId == measuredCurrent && nextId == measuredNext))
            return;
        ++waited;
        Collect(context, true);
        Submit(d3d, context, current, next, currentId, nextId);
        Collect(context, true);
    }
};
