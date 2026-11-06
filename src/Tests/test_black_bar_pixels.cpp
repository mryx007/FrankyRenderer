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
    {
        // End credits: a centred text column on black. Blurred by the probe
        // downscale, its lines and an occasional full-width heading pass the
        // row/column coverage checks, but the text fills little of its box.
        constexpr int width = 256, height = 144;
        auto credits = [&]
        {
            std::vector<uint8_t> pixels(width * height * 4, 0);
            for (int y = 10; y < height - 10; ++y)
            {
                const int line = (y - 10) / 8;
                if ((y - 10) % 8 >= 6)
                    continue; // spacing between text lines
                for (int x = 80; x < 176; ++x)
                    if (line % 6 == 0 || (x - 80) % 8 < 3)
                        pixels[(y * width + x) * 4 + 1] = 220;
            }
            return pixels;
        };
        auto letterbox = [&]
        {
            std::vector<uint8_t> pixels(width * height * 4, 0);
            for (int y = 18; y < height - 18; ++y)
                for (int x = 0; x < width; ++x)
                    pixels[(y * width + x) * 4] = uint8_t(60 + (x + y) % 120);
            return pixels;
        };
        BlackBarSettings settings;
        settings.enabled = TRUE;
        settings.delay = 0;
        BlackBarDetector detector;
        const auto text = credits();
        for (int sample = 0; sample < 6; ++sample)
            detector.Observe(text.data(), width, height, width * 4, 1920, 1080, settings, sample * 200);
        Require(!detector.measured, "credits alone never establish a crop");
        const auto film = letterbox();
        for (int sample = 0; sample < 3; ++sample)
            detector.Observe(film.data(), width, height, width * 4, 1920, 1080, settings, 2000 + sample * 200);
        const auto before = detector.Bounds(1920, 1080);
        Require(detector.measured && before.left == 0 && before.right == 1920, "letterboxed film crop");
        for (int sample = 0; sample < 20; ++sample)
            detector.Observe(text.data(), width, height, width * 4, 1920, 1080, settings, 3000 + sample * 200);
        const auto after = detector.Bounds(1920, 1080);
        Require(after.left == before.left && after.right == before.right && after.top == before.top &&
                    after.bottom == before.bottom,
                "end credits keep the film crop instead of zooming into the text column");
    }
    puts("PASS exact scalar/SSE2 equivalence across thresholds, sizes, pitches and alpha; detector integration; "
         "end credits");
}
