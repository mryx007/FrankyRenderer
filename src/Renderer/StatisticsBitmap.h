#pragma once
#include <libplacebo/gpu.h>
#include <libplacebo/renderer.h>
#include "Renderer/StatisticsRasterizer.h"

// Independent sRGB overlay. Only texture uploads run on the video thread.
// Uploads rotate through three textures: the shown one is still read by the
// frames queued for display, and writing it would wait for them.
class StatisticsBitmap
{
    StatisticsRasterizer rasterizer;
    pl_gpu gpu = nullptr;
    pl_tex textures[3] = {};
    pl_tex texture = nullptr; // the one shown
    int next = 0;
    std::wstring requested;
    int width = 0, height = 0, lastViewWidth = 0, lastViewHeight = 0, requestedWidth = 0, requestedHeight = 0;

  public:
    ~StatisticsBitmap()
    {
        rasterizer.Stop();
        Reset();
    }

    void Reset()
    {
        rasterizer.Reset();
        for (auto& slot : textures)
            if (slot)
                pl_tex_destroy(gpu, &slot);
        texture = nullptr;
        next = 0;
        gpu = nullptr;
        requested.clear();
        width = height = lastViewWidth = lastViewHeight = requestedWidth = requestedHeight = 0;
    }

    bool Update(pl_gpu nextGpu, const std::wstring& text, int viewWidth, int viewHeight, HWND window = nullptr)
    {
        if (gpu != nextGpu)
            Reset();
        gpu = nextGpu;
        if (viewWidth <= 16 || viewHeight <= 0)
            return false;
        if (auto image = rasterizer.Take(); image && image->viewWidth == viewWidth && image->viewHeight == viewHeight)
        {
            pl_tex_params params = {};
            params.w = image->width;
            params.h = image->height;
            params.format = pl_find_named_fmt(gpu, "rgba8");
            params.sampleable = true;
            params.host_writable = true;
            auto& slot = textures[next];
            bool ok = params.format && pl_tex_recreate(gpu, &slot, &params);
            if (ok)
            {
                pl_tex_transfer_params upload = {};
                upload.tex = slot;
                upload.ptr = image->pixels.data();
                ok = pl_tex_upload(gpu, &upload);
            }
            if (ok)
            {
                texture = slot;
                next = (next + 1) % 3;
                width = image->width;
                height = image->height;
                lastViewWidth = viewWidth;
                lastViewHeight = viewHeight;
            }
            else
            {
                requested.clear();
                lastViewWidth = lastViewHeight = 0;
            }
        }
        if (text != requested || viewWidth != requestedWidth || viewHeight != requestedHeight)
        {
            if (rasterizer.Submit(text, viewWidth, viewHeight, window))
            {
                requested = text;
                requestedWidth = viewWidth;
                requestedHeight = viewHeight;
            }
        }
        return texture && lastViewWidth == viewWidth && lastViewHeight == viewHeight;
    }

    void Describe(pl_overlay& overlay, pl_overlay_part& part)
    {
        part.src = {0, 0, float(width), float(height)};
        part.dst = {16, 0, float(width + 16), float(height)};
        overlay = {};
        overlay.tex = texture;
        overlay.mode = PL_OVERLAY_NORMAL;
        overlay.coords = PL_OVERLAY_COORDS_DST_FRAME;
        overlay.repr.sys = PL_COLOR_SYSTEM_RGB;
        overlay.repr.levels = PL_COLOR_LEVELS_FULL;
        overlay.repr.alpha = PL_ALPHA_INDEPENDENT;
        overlay.color = pl_color_space_srgb;
        overlay.parts = &part;
        overlay.num_parts = 1;
    }
};
