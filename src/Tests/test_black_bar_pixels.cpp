#define NOMINMAX
#include "Renderer/BlackBarDetector.h"
#include <cstring>
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
    {
        // A dark scene in a letterboxed film: the left part of the picture is
        // black. Only the bars above and below may be cropped, never a side.
        constexpr int width = 256, height = 144;
        std::vector<uint8_t> pixels(width * height * 4, 0);
        for (int y = 18; y < height - 18; ++y)
            for (int x = 40; x < width; ++x)
                pixels[(y * width + x) * 4] = uint8_t(60 + (x + y) % 120);
        BlackBarSettings settings;
        settings.enabled = TRUE;
        settings.delay = 0;
        BlackBarDetector detector;
        for (int sample = 0; sample < 6; ++sample)
            detector.Observe(pixels.data(), width, height, width * 4, 1920, 1080, settings, sample * 200);
        const auto bounds = detector.Bounds(1920, 1080);
        Require(detector.measured && bounds.left == 0 && bounds.right == 1920 && bounds.top > 0 && bounds.bottom < 1080,
                "a dark side of the picture is never cropped as a bar");
        // The same with a dark right side.
        std::vector<uint8_t> right(width * height * 4, 0);
        for (int y = 18; y < height - 18; ++y)
            for (int x = 0; x < width - 60; ++x)
                right[(y * width + x) * 4] = uint8_t(60 + (x + y) % 120);
        BlackBarDetector other;
        for (int sample = 0; sample < 6; ++sample)
            other.Observe(right.data(), width, height, width * 4, 1920, 1080, settings, sample * 200);
        const auto rightBounds = other.Bounds(1920, 1080);
        Require(other.measured && rightBounds.left == 0 && rightBounds.right == 1920, "nor its right side");
    }
    {
        // A flash or an object passing into a bar must not move the crop;
        // a real change of format and a subtitle that stays must.
        constexpr int width = 256, height = 144;
        auto film = [&](int top, int bottom, bool flash, bool caption)
        {
            std::vector<uint8_t> pixels(width * height * 4, 0);
            for (int y = top; y < height - bottom; ++y)
                for (int x = 0; x < width; ++x)
                    pixels[(y * width + x) * 4] = uint8_t(60 + (x + y) % 120);
            if (flash)
                for (int y = 4; y < 16; ++y)
                    for (int x = 90; x < 160; ++x)
                        pixels[(y * width + x) * 4] = 255;
            if (caption)
                for (int y = height - 12; y < height - 6; ++y)
                    for (int x = 70; x < 186; ++x)
                        if (x % 5 < 3)
                            pixels[(y * width + x) * 4 + 1] = 230;
            return pixels;
        };
        BlackBarSettings settings;
        settings.enabled = TRUE;
        settings.delay = 2;
        settings.keepSubtitles = TRUE;
        BlackBarDetector detector;
        const auto plain = film(18, 18, false, false), flashed = film(18, 18, true, false),
                   captioned = film(18, 18, false, true), full = film(0, 0, false, false);
        uint64_t now = 0;
        auto show = [&](const std::vector<uint8_t>& pixels, int samples, uint64_t step)
        {
            for (int i = 0; i < samples; ++i, now += step)
                detector.Observe(pixels.data(), width, height, width * 4, 1920, 1080, settings, now);
        };
        show(plain, 20, 200);
        const auto crop = detector.Bounds(1920, 1080);
        Require(detector.measured && crop.top > 0 && crop.bottom < 1080, "letterbox crop established");
        auto same = [&](const RECT& a) { return std::memcmp(&a, &crop, sizeof(RECT)) == 0; };
        show(flashed, 1, 200);
        show(plain, 20, 200);
        Require(same(detector.Bounds(1920, 1080)), "a flash in one sample keeps the crop");
        show(flashed, 2, 200);
        show(plain, 20, 200);
        Require(same(detector.Bounds(1920, 1080)), "a flash under 0.6 s keeps the crop");
        show(captioned, 1, 200);
        Require(same(detector.Bounds(1920, 1080)) && !detector.subtitleProtection,
                "a subtitle is not yet shown at once");
        show(captioned, 4, 200);
        Require(detector.subtitleProtection && detector.Bounds(1920, 1080).bottom > crop.bottom,
                "a subtitle that stays opens the bottom bar");
        show(plain, 30, 200);
        Require(same(detector.Bounds(1920, 1080)) && !detector.subtitleProtection, "and the crop returns after it");
        show(full, 6, 200);
        const auto opened = detector.Bounds(1920, 1080);
        Require(opened.top == 0 && opened.bottom == 1080, "a real change to full height is followed");
    }
    puts("PASS black bars: flashes under 0.6 s keep the crop, staying subtitles and format changes are followed");
    puts("PASS exact scalar/SSE2 equivalence across thresholds, sizes, pitches and alpha; detector integration; "
         "end credits; dark picture sides are not cropped");
}
