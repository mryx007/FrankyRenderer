#pragma once
#include "BlackBarSettings.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// Probe is decoded RGB before adjustments and separate subtitles. Not OCR:
// isolated bright border bands are treated as possible embedded text/logos.
class BlackBarDetector
{
    RECT active = {}, base = {}, candidate = {}, subtitleBounds = {};
    uint64_t since = 0, subtitleUntil = 0;
    unsigned confirmations = 0;
    bool haveBase = false;

    static bool Near(const RECT& a, const RECT& b, int t = 2)
    {
        return std::abs(a.left - b.left) <= t && std::abs(a.top - b.top) <= t && std::abs(a.right - b.right) <= t &&
               std::abs(a.bottom - b.bottom) <= t;
    }

  public:
    bool measured = false, subtitleProtection = false;
    uint64_t samples = 0;

    void Reset()
    {
        active = base = candidate = subtitleBounds = {};
        since = subtitleUntil = 0;
        confirmations = 0;
        measured = haveBase = subtitleProtection = false;
        samples = 0;
    }

    // A seek invalidates observations in flight, not the known stream geometry.
    // Preserve the displayed crop; reconsider candidates from the new position.
    void Discontinuity()
    {
        candidate = {};
        since = 0;
        confirmations = 0;
        subtitleUntil = 0;
    }

    RECT Bounds(int w, int h) const
    {
        return measured ? active : RECT{0, 0, w, h};
    }

    bool Observe(const uint8_t* pixels,
                 int w,
                 int h,
                 size_t pitch,
                 int sourceW,
                 int sourceH,
                 const BlackBarSettings& settings,
                 uint64_t now)
    {
        ++samples;
        if (!pixels || w < 16 || h < 16 || sourceW < 16 || sourceH < 16 || pitch < size_t(w) * 4)
            return false;
        const int limit = static_cast<int>(settings.threshold * 2.55f);
        std::vector<int> rows(h), cols(w);
        int bright = 0;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                const auto p = pixels + size_t(y) * pitch + x * 4;
                const int v = std::max({p[0], p[1], p[2]});
                if (v > limit)
                {
                    ++rows[y];
                    ++cols[x];
                }
                if (v > limit + 12)
                    ++bright;
            }
        if (bright < std::max(16, w * h / 100))
        {
            confirmations = 0;
            return false;
        }
        // Skip short isolated bands only when separated by black space from
        // the main picture. Never ignore a continuous picture reaching an edge.
        auto edge = [](const std::vector<int>& counts)
        {
            int at = 0;
            const int n = static_cast<int>(counts.size());
            while (at < n && !counts[at])
                ++at;
            for (int attempt = 0; attempt < 3 && at < n * .35; attempt++)
            {
                int end = at;
                while (end < n && counts[end])
                    ++end;
                int after = end;
                while (after < n && !counts[after])
                    ++after;
                if (end - at <= std::max(2, n / 18) && after - end >= std::max(2, n / 100) && after < n * .35)
                    at = after;
                else
                    break;
            }
            return at;
        };
        const std::vector<int> rr(rows.rbegin(), rows.rend()), cc(cols.rbegin(), cols.rend());
        auto bounds = [&](bool ignoreText)
        {
            int top = 0, bottom = 0, left = 0, right = 0;
            if (ignoreText)
            {
                top = edge(rows);
                bottom = edge(rr);
                left = edge(cols);
                right = edge(cc);
            }
            else
            {
                while (top < h && !rows[top])
                    ++top;
                while (bottom < h && !rr[bottom])
                    ++bottom;
                while (left < w && !cols[left])
                    ++left;
                while (right < w && !cc[right])
                    ++right;
            }
            // One probe-pixel safety margin; chroma-safe outward rounding.
            return RECT{LONG(std::max(0, left - 1) * sourceW / w) & ~1L,
                        LONG(std::max(0, top - 1) * sourceH / h) & ~1L,
                        std::min<LONG>(sourceW, ((std::min(w, w - right + 1) * sourceW + w - 1) / w + 1) & ~1),
                        std::min<LONG>(sourceH, ((std::min(h, h - bottom + 1) * sourceH + h - 1) / h + 1) & ~1)};
        };
        RECT next = bounds(true), visible = bounds(false);
        if (next.left > sourceW * .35 || sourceW - next.right > sourceW * .35 || next.top > sourceH * .35 ||
            sourceH - next.bottom > sourceH * .35 || next.right - next.left < sourceW / 3 ||
            next.bottom - next.top < sourceH / 3)
        {
            confirmations = 0;
            return false;
        }
        // Sparse title cards and small illuminated objects do not establish
        // movie boundaries. Require substantial picture coverage on both axes.
        // Keep the known crop through ambiguous frames instead of cropping text.
        const int contentW = std::max(1, int(next.right - next.left) * w / sourceW);
        const int contentH = std::max(1, int(next.bottom - next.top) * h / sourceH);
        if (*std::max_element(rows.begin(), rows.end()) < contentW * .65 ||
            *std::max_element(cols.begin(), cols.end()) < contentH * .65)
        {
            confirmations = 0;
            return false;
        }
        const RECT old = Bounds(sourceW, sourceH);
        if (!Near(candidate, next))
        {
            candidate = next;
            since = now;
            confirmations = 1;
        }
        else
            ++confirmations;
        if (haveBase)
        {
            RECT expanded = {std::min(base.left, next.left),
                             std::min(base.top, next.top),
                             std::max(base.right, next.right),
                             std::max(base.bottom, next.bottom)};
            if (!Near(base, expanded))
                base = expanded;
        }
        if (confirmations >= 3 && (!haveBase || now - since >= uint64_t(settings.delay * 1000.f)))
        {
            if (!haveBase || !settings.stableZoom)
                base = candidate;
            haveBase = true;
        }
        if (!haveBase)
            return false;
        RECT wanted = base;
        bool text = false;
        if (settings.keepSubtitles)
        {
            wanted = {std::min(base.left, visible.left),
                      std::min(base.top, visible.top),
                      std::max(base.right, visible.right),
                      std::max(base.bottom, visible.bottom)};
            text = !Near(wanted, base);
        }
        if (text)
        {
            subtitleUntil = now + uint64_t(settings.delay * 1000.f);
            subtitleBounds = wanted;
        }
        else if (now < subtitleUntil)
            wanted = subtitleBounds;
        subtitleProtection = text || now < subtitleUntil;
        if (!measured || !Near(old, wanted))
        {
            active = wanted;
            measured = true;
            return true;
        }
        return false;
    }
};
