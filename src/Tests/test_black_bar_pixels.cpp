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
        // A film without bars whose top turns dark (a night sky growing and
        // shrinking): one dark side is no bar, the picture must not move.
        constexpr int width = 256, height = 128;
        auto scene = [&](int dark)
        {
            std::vector<uint8_t> pixels(width * height * 4, 0);
            for (int y = dark; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    pixels[(y * width + x) * 4] = uint8_t(60 + (x + y) % 120);
            return pixels;
        };
        BlackBarSettings settings;
        settings.enabled = TRUE;
        BlackBarDetector detector;
        uint64_t now = 0;
        bool moved = false;
        for (int dark : {0, 0, 0, 0, 12, 11, 10, 9, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11, 0, 0, 0})
            for (int i = 0; i < 4; ++i, now += 200)
            {
                const auto pixels = scene(dark);
                detector.Observe(pixels.data(), width, height, width * 4, 3840, 1920, settings, now);
                const auto bounds = detector.Bounds(3840, 1920);
                moved |= detector.measured && (bounds.top != 0 || bounds.bottom != 1920);
            }
        Require(detector.measured && !moved, "a dark top without a bottom bar is never cropped");
        // Real bars that differ by a line or two are still cropped.
        BlackBarDetector letterbox;
        for (int sample = 0; sample < 4; ++sample)
        {
            auto pixels = scene(16);
            for (int y = height - 15; y < height; ++y)
                std::fill_n(pixels.begin() + y * width * 4, width * 4, uint8_t(0));
            letterbox.Observe(pixels.data(), width, height, width * 4, 3840, 1920, settings, sample * 200);
        }
        const auto bars = letterbox.Bounds(3840, 1920);
        Require(letterbox.measured && bars.top > 0 && bars.bottom < 1920, "slightly uneven letterbox bars are cropped");
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
        Require(detector.subtitleProtection && detector.Bounds(1920, 1080).bottom > crop.bottom &&
                    same(detector.ContentBounds(1920, 1080)),
                "a subtitle shows its bar at once and leaves the crop alone");
        show(captioned, 4, 200);
        Require(detector.subtitleProtection && detector.Bounds(1920, 1080).bottom > crop.bottom,
                "a subtitle that stays keeps its bar shown");
        // Compression noise a few rows into the top bar is no text, and a
        // subtitle in the bottom bar does not open the top one.
        auto noisy = [&](std::vector<uint8_t> pixels)
        {
            for (int y = 16; y < 18; ++y)
                for (int x = 0; x < width; x += 3)
                    pixels[(y * width + x) * 4] = 30;
            return pixels;
        };
        show(noisy(captioned), 1, 200);
        Require(detector.subtitleProtection && detector.Bounds(1920, 1080).top == crop.top &&
                    detector.Bounds(1920, 1080).bottom > crop.bottom,
                "edge noise in the top bar does not open it beside a bottom subtitle");
        show(plain, 2, 200);
        show(noisy(plain), 1, 200);
        Require(!detector.subtitleProtection && same(detector.Bounds(1920, 1080)), "edge noise alone shows no bar");
        show(plain, 30, 200);
        Require(same(detector.Bounds(1920, 1080)) && !detector.subtitleProtection, "and the crop returns after it");
        show(full, 6, 200);
        const auto opened = detector.Bounds(1920, 1080);
        Require(opened.top == 0 && opened.bottom == 1080, "a real change to full height is followed");
    }
    {
        // Measured once, then kept: after the first 30 s a dark scene never
        // makes the crop tighter, more picture (IMAX) makes it larger, and it
        // stays larger when the bars return. A seek keeps the crop.
        constexpr int width = 256, height = 144;
        auto film = [&](int bars)
        {
            std::vector<uint8_t> pixels(width * height * 4, 0);
            for (int y = bars; y < height - bars; ++y)
                for (int x = 0; x < width; ++x)
                    pixels[(y * width + x) * 4] = uint8_t(60 + (x + y) % 120);
            return pixels;
        };
        BlackBarSettings settings;
        settings.enabled = TRUE;
        BlackBarDetector detector;
        uint64_t now = 0;
        auto show = [&](const std::vector<uint8_t>& pixels, int samples, uint64_t step = 200)
        {
            for (int i = 0; i < samples; ++i, now += step)
                detector.Observe(pixels.data(), width, height, width * 4, 3840, 2160, settings, now);
        };
        auto equal = [](const RECT& a, const RECT& b) { return std::memcmp(&a, &b, sizeof(RECT)) == 0; };
        const auto loose = film(10), scope = film(18), dark = film(30), imax = film(0);
        show(loose, 3);
        const auto first = detector.ContentBounds(3840, 2160);
        Require(detector.measured && first.top > 0, "first crop after three matching samples");
        show(scope, 15);
        const auto corrected = detector.ContentBounds(3840, 2160);
        Require(corrected.top > first.top && corrected.bottom < first.bottom,
                "within the first 30 s a stable tighter crop replaces a loose first measurement");
        show(scope, 150);
        show(dark, 100);
        Require(equal(detector.ContentBounds(3840, 2160), corrected),
                "after 30 s a dark scene never tightens the crop");
        // A long pause does not count as playback time for the 30 s.
        BlackBarDetector paused;
        paused.Observe(loose.data(), width, height, width * 4, 3840, 2160, settings, 0);
        paused.Observe(loose.data(), width, height, width * 4, 3840, 2160, settings, 200);
        paused.Observe(loose.data(), width, height, width * 4, 3840, 2160, settings, 400);
        for (uint64_t at = 600000; at < 603000; at += 200)
            paused.Observe(scope.data(), width, height, width * 4, 3840, 2160, settings, at);
        Require(paused.ContentBounds(3840, 2160).top > first.top, "a pause does not end the correction window");
        detector.Discontinuity();
        show(dark, 30);
        Require(equal(detector.ContentBounds(3840, 2160), corrected), "a seek keeps the crop");
        show(imax, 2);
        Require(equal(detector.ContentBounds(3840, 2160), corrected), "more picture under 0.6 s keeps the crop");
        show(scope, 5);
        show(imax, 5);
        const auto opened = detector.ContentBounds(3840, 2160);
        Require(opened.top == 0 && opened.bottom == 2160, "an IMAX scene opens the crop");
        show(scope, 100);
        Require(equal(detector.ContentBounds(3840, 2160), opened), "and the crop stays open when the bars return");
        detector.Reset();
        show(scope, 3);
        Require(equal(detector.ContentBounds(3840, 2160), corrected), "a new file measures again");
    }
    {
        // DVD line 21 captions: a dotted data line on the outermost rows above
        // a letterboxed film is no subtitle, the top bar is still cropped.
        // Text inside a bar (not touching the edge) is still protected.
        constexpr int width = 720, height = 480;
        auto picture = [&](int dataRows, bool text)
        {
            std::vector<uint8_t> pixels(size_t(width) * height * 4, 0);
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    uint8_t* p = &pixels[(size_t(y) * width + x) * 4];
                    p[3] = 255;
                    if (y >= 60 && y < height - 60)
                        p[0] = p[1] = p[2] = uint8_t(80 + (x * 7 + y * 3) % 120);
                    if (y < dataRows)
                        p[0] = p[1] = p[2] = (x / 13) % 2 ? 180 : 16;
                    if (text && y >= height - 40 && y < height - 30 && x >= 200 && x < 520 && x % 6 < 4)
                        p[0] = p[1] = p[2] = 230;
                }
            return pixels;
        };
        BlackBarSettings settings;
        settings.enabled = TRUE;
        settings.keepSubtitles = TRUE;
        for (int dataRows : {1, 2})
        {
            BlackBarDetector detector;
            const auto pixels = picture(dataRows, false);
            for (int sample = 0; sample < 6; ++sample)
                detector.Observe(pixels.data(), width, height, width * 4, width, height, settings, sample * 400);
            const auto shown = detector.Bounds(width, height);
            Require(detector.measured && !detector.subtitleProtection && shown.top > 40 && shown.bottom < height - 40,
                    "line 21 caption dots on the top rows do not keep the top bar");
        }
        BlackBarDetector detector;
        const auto captions = picture(2, true);
        for (int sample = 0; sample < 6; ++sample)
            detector.Observe(captions.data(), width, height, width * 4, width, height, settings, sample * 400);
        const auto shown = detector.Bounds(width, height);
        const auto crop = detector.ContentBounds(width, height);
        Require(detector.subtitleProtection && shown.bottom >= height - 32 && shown.bottom > crop.bottom + 20 &&
                    shown.top == crop.top,
                "subtitles in the bottom bar are still shown, line 21 dots are not");
    }
    puts("PASS line 21 captions on the outermost rows are no subtitles; subtitles in a bar are still shown");
    puts("PASS black bars: measured once, a stable correction only in the first 30 s of playback, dark scenes never "
         "tighten, IMAX opens and stays open, seeks keep the crop, subtitles show their bar without moving the crop, "
         "edge noise is no text");
    puts("PASS exact scalar/SSE2 equivalence across thresholds, sizes, pitches and alpha; detector integration; "
         "end credits; dark picture sides and a dark top alone are not cropped");
}
