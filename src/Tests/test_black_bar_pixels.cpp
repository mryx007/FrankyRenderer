#define NOMINMAX
#include "Renderer/BlackBarDetector.h"
#include <cstdio>
#include <cstdlib>
#include <random>

static void Require(bool ok, const char* label)
{
    if (!ok)
    {
        printf("FAIL %s\n", label);
        exit(1);
    }
}

int main()
{
    std::mt19937 random(1783);
    for (int width : {1, 3, 4, 7, 16, 63, 1023, 1024})
        for (int height : {1, 17, 576})
            for (int padding : {0, 3, 64})
            {
                const size_t pitch = size_t(width) * 4 + padding;
                std::vector<uint8_t> data(pitch * height + 1);
                for (auto& pixel : data)
                    pixel = uint8_t(random());
                for (int limit = 0; limit <= 25; ++limit)
                {
                    std::vector<int> rows(height), columns(width), expectedRows(height), expectedColumns(width);
                    const auto* pixels = data.data() + 1; // Deliberately unaligned.
                    int expectedBright = 0;
                    for (int y = 0; y < height; ++y)
                        for (int x = 0; x < width; ++x)
                        {
                            const auto* pixel = pixels + size_t(y) * pitch + x * 4;
                            const int value = std::max({pixel[0], pixel[1], pixel[2]});
                            if (value > limit)
                            {
                                ++expectedRows[y];
                                ++expectedColumns[x];
                            }
                            expectedBright += value > limit + 12;
                        }
                    const int bright =
                        CountBlackBarPixels(pixels, width, height, pitch, limit, rows.data(), columns.data());
                    Require(rows == expectedRows && columns == expectedColumns && bright == expectedBright,
                            "exact RGB counts, padded rows, unaligned input and SIMD tails");
                }
            }
    {
        constexpr int width = 64, height = 48;
        std::vector<uint8_t> pixels(width * height * 4, 0);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                pixels[(y * width + x) * 4 + 3] = 255;
        BlackBarSettings settings;
        settings.enabled = TRUE;
        settings.delay = 0;
        BlackBarDetector detector;
        detector.Observe(pixels.data(), width, height, width * 4, 640, 480, settings, 0);
        Require(!detector.measured, "alpha does not turn a black picture into content");
        for (int y = 8; y < height - 8; ++y)
            for (int x = 0; x < width; ++x)
                pixels[(y * width + x) * 4] = 200;
        for (int sample = 0; sample < 3; ++sample)
            detector.Observe(pixels.data(), width, height, width * 4, 640, 480, settings, sample * 200);
        const auto bounds = detector.Bounds(640, 480);
        Require(detector.measured && bounds.left == 0 && bounds.right == 640 && bounds.top > 0 && bounds.bottom < 480,
                "detector still finds top and bottom bars");
    }
    puts("PASS exact scalar/SSE2 equivalence across thresholds, sizes, pitches and alpha; detector integration");
}
