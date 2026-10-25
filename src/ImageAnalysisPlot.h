#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ImageAnalysisPlot
{
inline constexpr int SampleWidth = 384, SampleHeight = 216, Width = 720, Height = 624;
using Pixel = std::array<float, 4>; // Linear RGB and luminance, all in nominal nits.

struct Sample
{
    std::vector<Pixel> pixels;
    uint64_t key = 0;
    float reference = 203;
    bool hdr = false;
    bool mixed = false;
    float aspect = 16.f / 9;
};

struct Image
{
    std::vector<uint8_t> rgba;
};

inline double Axis(double nits, double maximum)
{
    return std::clamp(std::log1p(std::max(0., nits)) / std::log1p(maximum), 0., 1.);
}

inline std::array<uint8_t, 3> FalseColor(float nits)
{
    if (!std::isfinite(nits) || nits < 0 || nits > 10000)
        return {255, 0, 255};

    struct Stop
    {
        float nits;
        std::array<uint8_t, 3> color;
    };

    static constexpr Stop stops[] = {{0, {25, 35, 90}},
                                     {1, {35, 105, 210}},
                                     {10, {30, 180, 175}},
                                     {100, {65, 195, 75}},
                                     {203, {170, 210, 65}},
                                     {400, {235, 205, 55}},
                                     {1000, {245, 155, 45}},
                                     {2000, {245, 100, 45}},
                                     {4000, {235, 65, 65}},
                                     {10000, {255, 235, 235}}};
    for (size_t i = 1; i < std::size(stops); ++i)
    {
        if (nits > stops[i].nits)
            continue;
        const auto& a = stops[i - 1];
        const auto& b = stops[i];
        const float t = (nits - a.nits) / (b.nits - a.nits);
        std::array<uint8_t, 3> color;
        for (int c = 0; c < 3; ++c)
            color[c] = uint8_t(std::lround(a.color[c] + t * (int(b.color[c]) - int(a.color[c]))));
        return color;
    }
    return stops[std::size(stops) - 1].color;
}

struct Distribution
{
    std::array<uint32_t, 256> histogram = {};
    std::vector<uint16_t> waveform, parade;
    double peak = 0;
    size_t invalid = 0;
};

inline Distribution Measure(const Sample& s, double maximum, int w, int h)
{
    Distribution d;
    d.waveform.resize(size_t(w) * h);
    d.parade.resize(size_t(w) * h);
    for (size_t i = 0; i < s.pixels.size(); ++i)
    {
        const auto& p = s.pixels[i];
        if (!std::isfinite(p[3]) || p[3] < 0)
        {
            ++d.invalid;
            continue;
        }
        d.peak = std::max(d.peak, double(p[3]));
        const double y = Axis(p[3], maximum);
        ++d.histogram[std::min(255, int(y * 256))];
        const int row = h - 1 - int(y * (h - 1));
        const int x = int(i % SampleWidth) * w / SampleWidth;
        auto& density = d.waveform[size_t(row) * w + x];
        if (density < UINT16_MAX)
            ++density;
        for (int c = 0; c < 3; ++c)
        {
            if (!std::isfinite(p[c]))
                continue;
            const int column = std::min(w - 1, (c * w + x) / 3);
            const int r = h - 1 - int(Axis(p[c], maximum) * (h - 1));
            auto& channel = d.parade[size_t(r) * w + column];
            if (channel < UINT16_MAX)
                ++channel;
        }
    }
    return d;
}

// CPU plotting only; the renderer feeds bounded float samples from nonblocking
// GPU readback. Density, not connected point curves, preserves image structure.
inline Image Draw(const Sample& before, const Sample& after)
{
    Image result;
    result.rgba.resize(Width * Height * 4);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return {};
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = Width;
    info.bmiHeader.biHeight = -Height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    if (!bitmap)
    {
        DeleteDC(dc);
        return {};
    }
    auto oldBitmap = SelectObject(dc, bitmap);
    auto bytes = static_cast<uint8_t*>(data);
    for (int i = 0; i < Width * Height; ++i)
    {
        bytes[i * 4] = 22;
        bytes[i * 4 + 1] = 20;
        bytes[i * 4 + 2] = 18;
        bytes[i * 4 + 3] = 255;
    }
    auto font = CreateFontW(-16,
                            0,
                            0,
                            0,
                            FW_NORMAL,
                            FALSE,
                            FALSE,
                            FALSE,
                            DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY,
                            DEFAULT_PITCH,
                            L"Segoe UI");
    auto oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    auto text = [&](int x, int y, const std::wstring& s, COLORREF color = RGB(225, 230, 235))
    {
        SetTextColor(dc, color);
        TextOutW(dc, x, y, s.c_str(), int(s.size()));
    };
    auto dot = [&](int x, int y, std::array<uint8_t, 3> rgb)
    {
        if (x < 0 || y < 0 || x >= Width || y >= Height)
            return;
        auto p = bytes + (size_t(y) * Width + x) * 4;
        p[0] = rgb[2];
        p[1] = rgb[1];
        p[2] = rgb[0];
    };
    const bool hdr = before.hdr || after.hdr;
    const double maximum = hdr ? 10000 : std::max({203.f, before.reference, after.reference});
    constexpr int W = 286, H = 190;
    const auto pre = Measure(before, maximum, W, H), post = Measure(after, maximum, W, H);
    text(14, 8, L"IMAGE ANALYSIS  |  sampled image, not a display measurement");
    text(14, 31, L"Before color mapping", RGB(90, 210, 235));
    text(220, 31, L"After color mapping", RGB(245, 205, 90));
    text(14, 56, L"Waveform - image left to right");
    text(375, 56, L"Histogram - brightness distribution");
    text(14, 305, L"RGB parade - R / G / B, linear nits");
    text(375, 305, L"False color - before / after");
    GdiFlush();
    for (int panel = 0; panel < 3; ++panel)
    {
        const int left = panel == 1 ? 411 : 50, top = panel == 2 ? 331 : 83;
        for (double level : {0., 1., 2., 5., 10., 20., 50., 100., 203., 400., 1000., 2000., 4000., 10000.})
        {
            if (level > maximum)
                continue;
            if (panel == 1)
            {
                const int x = left + int(Axis(level, maximum) * (W - 1));
                for (int y = 0; y < H; y += 3)
                    dot(x, top + y, {62, 64, 68});
            }
            else
            {
                const int y = top + H - 1 - int(Axis(level, maximum) * (H - 1));
                for (int x = 0; x < W; x += 3)
                    dot(left + x, y, {62, 64, 68});
            }
        }
        if (panel == 1)
            continue;
        const auto& a = panel == 0 ? pre.waveform : pre.parade;
        const auto& b = panel == 0 ? post.waveform : post.parade;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const auto i = size_t(y) * W + x;
                const double p = std::min(1., std::log1p(a[i]) / 3.);
                const double q = std::min(1., std::log1p(b[i]) / 3.);
                if (!a[i] && !b[i])
                    continue;
                if (panel == 0)
                    dot(left + x,
                        top + y,
                        {uint8_t(std::min(255., 90 * p + 245 * q)),
                         uint8_t(std::min(255., 210 * p + 205 * q)),
                         uint8_t(std::min(255., 235 * p + 90 * q))});
                else
                {
                    std::array<uint8_t, 3> rgb = {uint8_t(100 * p), uint8_t(100 * p), uint8_t(100 * p)};
                    rgb[std::min(2, x * 3 / W)] = uint8_t(std::min(255., 100 * p + 255 * q));
                    dot(left + x, top + y, rgb);
                }
            }
    }
    const auto largest = std::max(*std::max_element(pre.histogram.begin(), pre.histogram.end()),
                                  *std::max_element(post.histogram.begin(), post.histogram.end()));
    for (int x = 0; x < W; ++x)
    {
        const int bin = std::min(255, x * 256 / W);
        const int p = largest ? int(double(pre.histogram[bin]) / largest * (H - 1)) : 0;
        const int q = largest ? int(double(post.histogram[bin]) / largest * (H - 1)) : 0;
        for (int y = 0; y < std::max(p, q); ++y)
            dot(411 + x,
                83 + H - 1 - y,
                y < p && y < q ? std::array<uint8_t, 3>{230, 230, 190}
                : y < p        ? std::array<uint8_t, 3>{90, 210, 235}
                               : std::array<uint8_t, 3>{245, 205, 90});
    }
    for (int side = 0; side < 2; ++side)
    {
        const auto& s = side ? after : before;
        if (s.pixels.size() != SampleWidth * SampleHeight)
            continue;
        const float aspect = std::isfinite(s.aspect) && s.aspect > 0 ? s.aspect : 16.f / 9;
        const int imageW = std::min(W / 2, std::max(1, int(H * aspect)));
        const int imageH = std::min(H, std::max(1, int(imageW / aspect)));
        for (int y = 0; y < imageH; ++y)
            for (int x = 0; x < imageW; ++x)
            {
                const int sx = x * SampleWidth / imageW, sy = y * SampleHeight / imageH;
                dot(411 + side * (W / 2) + (W / 2 - imageW) / 2 + x,
                    331 + (H - imageH) / 2 + y,
                    FalseColor(s.pixels[sy * SampleWidth + sx][3]));
            }
    }
    for (int x = 0; x < W; ++x)
    {
        const float nits = float(std::expm1(double(x) / (W - 1) * std::log1p(maximum)));
        for (int y = 0; y < 7; ++y)
            dot(411 + x, 526 + y, FalseColor(nits));
    }
    // Axis labels use their actual text extents. Prioritize black, diffuse
    // white and the end point, then fill free space with intermediate values.
    auto axisFont = CreateFontW(-12,
                                0,
                                0,
                                0,
                                FW_NORMAL,
                                FALSE,
                                FALSE,
                                FALSE,
                                DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS,
                                ANTIALIASED_QUALITY,
                                DEFAULT_PITCH,
                                L"Segoe UI");
    SelectObject(dc, axisFont);
    std::vector<std::pair<int, int>> vertical, horizontal;
    auto fits = [](const auto& intervals, int start, int end, int gap = 3)
    {
        for (const auto& range : intervals)
            if (start < range.second + gap && end + gap > range.first)
                return false;
        return true;
    };
    for (double level :
         {0., 203., maximum, 100., 1000., 4000., 10., 50., 400., 2000., 20., 5., 1., 2., 25., 75., 125., 150., 175.})
    {
        if (level > maximum)
            continue;
        wchar_t label[24];
        swprintf_s(label, L"%.0f", level);
        SIZE size = {};
        GetTextExtentPoint32W(dc, label, int(wcslen(label)), &size);
        const int y = std::clamp(H - 1 - int(Axis(level, maximum) * (H - 1)), 0, H - int(size.cy));
        if (fits(vertical, y, y + int(size.cy), 1))
        {
            vertical.emplace_back(y, y + int(size.cy));
            text(4, 83 + y, label);
            text(4, 331 + y, label);
        }
        const int x =
            std::clamp(411 + int(Axis(level, maximum) * (W - 1)) - int(size.cx) / 2, 411, Width - int(size.cx) - 8);
        if (fits(horizontal, x, x + int(size.cx)))
        {
            horizontal.emplace_back(x, x + int(size.cx));
            text(x, 276, label);
            text(x, 536, label);
        }
    }
    SelectObject(dc, font);
    DeleteObject(axisFont);
    text(50, 276, L"Luminance (nits, logarithmic scale)");
    text(50, 526, L"Before: gray   After: channel colors");
    wchar_t summary[160];
    swprintf_s(summary, L"Sampled peak: before %.1f nits / after %.1f nits | Gray ramp below", pre.peak, post.peak);
    text(14, 568, summary);
    if (before.pixels.empty() || after.pixels.empty())
        text(14, 589, L"Waiting for both image stages - missing data is not black.");
    else if (after.mixed)
        text(14, 589, L"Temporal mixing: before is one input frame, after is the mixed output.");
    else
        text(14, 589, L"384 x 216 samples | excludes overlays and gray ramp | no automatic pass/fail");
    GdiFlush();
    for (size_t i = 0; i < result.rgba.size(); i += 4)
    {
        result.rgba[i] = bytes[i + 2];
        result.rgba[i + 1] = bytes[i + 1];
        result.rgba[i + 2] = bytes[i];
        result.rgba[i + 3] = 245;
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return result;
}
}
