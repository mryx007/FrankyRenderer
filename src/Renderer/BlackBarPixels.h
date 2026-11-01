#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <emmintrin.h>

// x64 guarantees SSE2. Count every RGB pixel exactly, ignoring alpha and row
// padding, while processing four pixels and four column counters together.
inline int
CountBlackBarPixels(const uint8_t* pixels, int width, int height, size_t pitch, int limit, int* rows, int* columns)
{
    std::fill_n(columns, width, 0);
    const auto rgbMask = _mm_set1_epi32(255);
    const auto threshold = _mm_set1_epi32(limit);
    const auto brightThreshold = _mm_set1_epi32(limit + 12);
    auto bright = _mm_setzero_si128();
    int tailBright = 0;
    for (int y = 0; y < height; ++y)
    {
        const auto* row = pixels + size_t(y) * pitch;
        auto hits = _mm_setzero_si128();
        int x = 0;
        for (; x + 4 <= width; x += 4)
        {
            const auto rgba = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + size_t(x) * 4));
            auto value = _mm_max_epu8(rgba, _mm_srli_epi32(rgba, 8));
            value = _mm_max_epu8(value, _mm_srli_epi32(rgba, 16));
            value = _mm_and_si128(value, rgbMask);
            const auto active = _mm_cmpgt_epi32(value, threshold);
            hits = _mm_sub_epi32(hits, active);
            auto counts = _mm_loadu_si128(reinterpret_cast<const __m128i*>(columns + x));
            counts = _mm_sub_epi32(counts, active);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(columns + x), counts);
            bright = _mm_sub_epi32(bright, _mm_cmpgt_epi32(value, brightThreshold));
        }
        hits = _mm_add_epi32(hits, _mm_srli_si128(hits, 8));
        hits = _mm_add_epi32(hits, _mm_srli_si128(hits, 4));
        int rowCount = _mm_cvtsi128_si32(hits);
        for (; x < width; ++x)
        {
            const auto* pixel = row + size_t(x) * 4;
            const int value = std::max({pixel[0], pixel[1], pixel[2]});
            const int active = value > limit;
            rowCount += active;
            columns[x] += active;
            tailBright += value > limit + 12;
        }
        rows[y] = rowCount;
    }
    bright = _mm_add_epi32(bright, _mm_srli_si128(bright, 8));
    bright = _mm_add_epi32(bright, _mm_srli_si128(bright, 4));
    return _mm_cvtsi128_si32(bright) + tailBright;
}
