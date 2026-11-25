#include "Renderer/Backend.h"
#include "Renderer/ImagePosition.h"
#include "Settings/SettingsIni.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <chrono>

static void Require(bool ok, const char* text)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", text);
        std::exit(1);
    }
}

int main(int argc, char** argv)
{
    for (int position : {0, 1, 2})
    {
        PlaceboSettings saved;
        saved.imagePosition = position;
        Require(SUCCEEDED(ValidateSettings(saved)), "valid position");
        Require(SettingsIni::Parse(SettingsIni::Serialize(saved)).imagePosition == position, "INI round trip");
        Require(BackendSettings(saved).imagePosition == position, "backend setting");
        Require(SUCCEEDED(SaveSettings(saved)) && LoadSettings().imagePosition == position, "saved position");
    }
    PlaceboSettings invalid;
    invalid.imagePosition = 3;
    Require(FAILED(ValidateSettings(invalid)), "invalid position rejected");
    Require(SettingsIni::Parse("[FrankyRenderer]\nFormatVersion=1\n").imagePosition == 0, "old INI defaults to center");
    for (int angle : {0, 90, 180, 270})
    {
        pl_rect2df source = {0, 0, 192, 108}, bottom = {0, 20, 192, 128};
        RECT active = {0, 0, 192, 108};
        AlignImageVertically(bottom, source, active, angle, 1, 160);
        Require(bottom.y1 == 160 && bottom.y0 == 52, "rotated bottom alignment");
        AlignImageVertically(bottom, source, active, angle, 2, 160);
        Require(bottom.y0 == 0 && bottom.y1 == 108, "top alignment does not resize");
    }
    PlaceboBackend backend;
    RenderSettings settings;
    settings.peakDetect = settings.colorAdjustment = false;
    settings.ditherMethod = 3;
    settings.imagePosition = 2;
    settings.blackBars.keepSubtitles = TRUE;
    settings.blackBars.delay = 0;
    Require(SUCCEEDED(backend.SetSettings(settings)), "settings");
    VideoFrame frame;
    frame.width = frame.stride = 192;
    frame.height = 108;
    frame.aspectX = 16;
    frame.aspectY = 9;
    frame.color = pl_color_space_bt709;
    std::vector<uint8_t> input(192 * 108 * 3 / 2, 128);
    auto upload = [&](bool caption, bool upper = false)
    {
        std::fill(input.begin(), input.begin() + 192 * 108, uint8_t(16));
        for (int y = 16; y < 92; ++y)
            for (int x = 0; x < 192; ++x)
                input[y * 192 + x] = uint8_t(110);
        if (caption)
            for (int y = 98; y < 104; ++y)
                for (int x = 72; x < 120; ++x)
                    input[y * 192 + x] = uint8_t(235);
        if (upper)
            for (int y = 4; y < 10; ++y)
                for (int x = 72; x < 120; ++x)
                    input[y * 192 + x] = uint8_t(235);
        Require(SUCCEEDED(backend.Upload(frame, input.data(), input.size())), "upload");
    };
    std::vector<uint8_t> pixels;
    // With crop, the player sends its default video frame (touch window from
    // inside) for the output size; other frames are drawn without cropping.
    bool insideFrame = false;
    auto render = [&](int h)
    {
        if (insideFrame)
            backend.SetGeometry(RECT{}, h == 108 ? RECT{0, 0, 192, 108} : RECT{25, 0, 167, h});
        Require(SUCCEEDED(backend.RenderOffscreen(192, h, pixels, false, 8, true)), "GPU render");
    };
    auto green = [&](int x, int y) { return int(pixels[(y * 192 + x) * 4 + 1]); };
    upload(false);
    // The player's zoom fills the scope screen by width, with the encoded
    // letterboxing extending beyond its edges.
    backend.SetGeometry(RECT{}, RECT{0, 0, 192, 108});
    for (int i = 0; i < 20 && !backend.BlackBars().measured; ++i)
    {
        render(80);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    Require(backend.BlackBars().measured, "automatic detection when crop is disabled");
    render(80);
    Require(green(96, 3) > 50, "top alignment removes encoded top bar");
    const auto stablePicture = pixels;
    settings.blackBars.delay = 2;
    Require(SUCCEEDED(backend.SetSettings(settings)), "change detection stability while playing");
    render(80);
    Require(backend.BlackBars().measured && pixels == stablePicture,
            "changing stability preserves measured crop and picture size");
    const int before = green(96, 72);
    upload(true);
    for (int i = 0; i < 15 && !backend.BlackBars().protectingSubtitles; ++i)
    {
        render(80);
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    }
    Require(backend.BlackBars().protectingSubtitles, "embedded subtitle detected");
    render(80);
    Require(green(96, 72) > before + 80, "original white subtitle pixels overlaid at bottom");
    Require(green(20, 72) < 8, "original black bar covers the movie behind subtitle");
    Require(green(96, 4) > 50 && green(96, 40) > 50, "movie remains shifted and unchanged outside bar");
    // A player can omit blank rows in its source rectangle. Embedded caption
    // protection still needs the full decoded bar, including the bottom margin.
    backend.SetGeometry(RECT{0, 0, 192, 105}, RECT{0, 0, 192, 108});
    render(80);
    Require(green(96, 77) < 8, "player source crop must not remove the subtitle bottom margin");
    insideFrame = true;
    // Cropping must use the movie bounds, not jump when captions appear.
    settings.blackBars.enabled = TRUE;
    Require(SUCCEEDED(backend.SetSettings(settings)), "enable automatic crop");
    for (int i = 0; i < 20; ++i)
    {
        render(80);
        if (backend.BlackBars().measured && backend.BlackBars().protectingSubtitles)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    render(80);
    Require(green(96, 72) > 180 && green(20, 72) < 8, "caption overlay with automatic crop");
    settings.imagePosition = 0;
    Require(SUCCEEDED(backend.SetSettings(settings)), "centered automatic crop");
    render(80);
    Require(green(96, 72) > 180 && green(96, 77) < 8,
            "centered automatic crop preserves original caption bottom padding");
    // On a 16:9 output the cropped movie leaves room for the original bar.
    // The bar's pixels still have to be rendered into that empty area.
    for (int tvPosition : {0, 1, 2})
    {
        settings.imagePosition = tvPosition;
        Require(SUCCEEDED(backend.SetSettings(settings)), "TV image position");
        render(108);
        Require(green(96, 100) > 180 && green(96, 106) < 8, "16:9 TV preserves captions and original bottom padding");
    }
    settings.imagePosition = 2;
    Require(SUCCEEDED(backend.SetSettings(settings)), "restore top alignment");
    upload(true, true);
    for (int i = 0; i < 18; ++i)
    {
        render(108);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    for (int upperPosition : {0, 1, 2})
    {
        settings.imagePosition = upperPosition;
        Require(SUCCEEDED(backend.SetSettings(settings)), "top and bottom subtitle positions");
        for (int height : {108, 80})
        {
            render(height);
            Require(green(96, 6) > 180 && green(96, 1) < 8,
                    "upper subtitle bar retains original top margin on TV and ultrawide");
            Require(green(96, height - 8) > 180 && green(96, height - 2) < 8,
                    "both subtitle bars remain visible simultaneously");
        }
    }
    upload(true);
    for (int i = 0; i < 18; ++i)
    {
        render(80);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    // HDR output also preserves the movie when compositing its SDR caption bar.
    Require(SUCCEEDED(backend.RenderOffscreen(192, 80, pixels, true, 8, true)), "HDR subtitle composite");
    Require(green(96, 72) > green(20, 72) + 30, "HDR output retains caption contrast");
    upload(false);
    for (int i = 0; i < 18 && backend.BlackBars().protectingSubtitles; ++i)
    {
        render(80);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    render(80);
    Require(!backend.BlackBars().protectingSubtitles && green(20, 72) > 50,
            "bar disappears without waiting for two-second crop stability");
    settings.blackBars.enabled = FALSE;
    settings.blackBars.keepSubtitles = FALSE;
    Require(SUCCEEDED(backend.SetSettings(settings)), "disable subtitle protection");
    insideFrame = false;
    backend.SetGeometry(RECT{}, RECT{0, 0, 192, 108});
    render(80);
    Require(green(20, 72) > 50, "subtitle bar can be disabled");
    // No encoded bars: moving a wide movie in a taller output still works.
    frame.height = 76;
    frame.aspectX = 48;
    frame.aspectY = 19;
    frame.color = pl_color_space_bt709;
    input.assign(192 * 76 * 3 / 2, 128);
    std::fill(input.begin(), input.begin() + 192 * 76, uint8_t(110));
    insideFrame = false;
    backend.SetGeometry(RECT{}, RECT{});
    Require(SUCCEEDED(backend.Upload(frame, input.data(), input.size())), "bar-free upload");
    for (int position : {0, 1, 2})
    {
        settings.imagePosition = position;
        Require(SUCCEEDED(backend.SetSettings(settings)), "bar-free settings");
        render(108);
        Require((green(96, 3) > 50) == (position == 2), "bar-free top edge");
        Require((green(96, 104) > 50) == (position == 1), "bar-free bottom edge");
    }
    {
        // MPC-HC's video frame modes with automatic crop: the player sends the
        // rectangle of the whole 16:9 video; the cropped 32:9 picture follows
        // its mode (touch window from inside or outside, normal size).
        PlaceboBackend framed;
        RenderSettings config;
        config.peakDetect = config.colorAdjustment = false;
        config.ditherMethod = 3;
        config.blackBars.enabled = config.blackBars.crop = TRUE;
        config.blackBars.keepSubtitles = FALSE;
        config.blackBars.delay = 0;
        Require(SUCCEEDED(framed.SetSettings(config)), "video frame settings");
        VideoFrame video = {};
        video.width = video.stride = 192;
        video.height = 108;
        video.aspectX = 16;
        video.aspectY = 9;
        video.color = pl_color_space_bt709;
        std::vector<uint8_t> letterboxed(192 * 108 * 3 / 2, 128);
        std::fill(letterboxed.begin(), letterboxed.begin() + 192 * 108, uint8_t(16));
        for (int y = 27; y < 81; ++y)
            std::fill_n(letterboxed.begin() + y * 192, 192, uint8_t(110));
        Require(SUCCEEDED(framed.Upload(video, letterboxed.data(), letterboxed.size())), "letterboxed upload");
        std::vector<uint8_t> out;
        auto draw = [&](RECT player, int outputHeight = 300)
        {
            framed.SetGeometry(RECT{}, player);
            Require(SUCCEEDED(framed.RenderOffscreen(400, outputHeight, out, false, 8, true)), "video frame render");
        };
        auto picture = [&](int x, int y) { return out[(size_t(y) * 400 + x) * 4 + 1] > 50; };
        // Touch window from inside in a wide 400x150 window: the 16:9 video's
        // rectangle is 267x150; the cropped 32:9 picture fills the width.
        const RECT inside = {67, 0, 334, 150}, outside = {-67, 0, 467, 300}, normal = {104, 96, 296, 204};
        for (int i = 0; i < 20 && !framed.BlackBars().measured; ++i)
        {
            draw(inside, 150);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        Require(framed.BlackBars().measured, "letterbox measured");
        draw(inside, 150);
        Require(picture(20, 75) && picture(380, 75) && !picture(200, 10) && !picture(200, 140),
                "touch window from inside crops the bars and fits the picture");
        // Any other video frame draws the whole video into the player's
        // rectangle, encoded bars included, as other renderers do.
        draw(outside);
        Require(picture(6, 150) && picture(393, 150) && picture(200, 80) && picture(200, 220) && !picture(200, 40) &&
                    !picture(200, 260),
                "touch window from outside draws the whole video without cropping");
        draw(normal);
        Require(picture(110, 128) && picture(290, 172) && !picture(200, 115) && !picture(200, 185) &&
                    !picture(95, 150) && !picture(305, 150),
                "normal size draws the video at the player's size");
        // Stretch to window: the whole 4:3 window for a 16:9 video.
        draw(RECT{0, 0, 400, 300});
        Require(picture(6, 150) && picture(393, 150) && picture(200, 80) && picture(200, 220) && !picture(200, 40) &&
                    !picture(200, 260),
                "stretch to window draws the whole video stretched");
        // The whole window in the video's shape is touching from inside: crop.
        draw(RECT{0, 0, 400, 225}, 225);
        Require(picture(200, 112) && !picture(200, 40) && !picture(200, 185),
                "a whole window in the video's shape keeps cropping");
        draw(inside, 150);
        Require(picture(20, 75), "back to touch window from inside crops again");
        // A player sends its rectangle first, then the window size, and the
        // drawing area follows with the next present. In between, the new
        // rectangle does not fit the old area: the decision must stay.
        framed.SetHostWindowSize(400, 150);
        draw(inside, 150);
        framed.SetGeometry(RECT{}, outside);
        Require(SUCCEEDED(framed.RenderOffscreen(400, 150, out, false, 8, true)), "rectangle before window size");
        Require(picture(20, 30), "a new rectangle before its window size keeps cropping");
        framed.SetHostWindowSize(400, 300);
        Require(SUCCEEDED(framed.RenderOffscreen(400, 150, out, false, 8, true)), "window size before the area");
        Require(picture(20, 30), "a drawing area of the old size keeps cropping");
        Require(SUCCEEDED(framed.RenderOffscreen(400, 300, out, false, 8, true)), "area of the new size");
        Require(picture(200, 80) && picture(200, 220) && !picture(200, 40),
                "the new size decides: touch window from outside draws the whole video");
        puts("PASS video frames with Crop Black Bars: inside crops; outside, normal size and stretch use the "
             "player's rectangle");
    }
    if (argc == 2)
        for (int actualPosition : {2, 0, 1})
            for (int outputHeight : {1440, 1080})
            {
                PlaceboBackend full;
                RenderSettings config;
                config.peakDetect = config.colorAdjustment = false;
                config.ditherMethod = 3;
                config.imagePosition = actualPosition;
                config.blackBars.enabled = config.blackBars.crop = config.blackBars.keepSubtitles = TRUE;
                config.blackBars.delay = 0;
                Require(SUCCEEDED(full.SetSettings(config)), "real clip settings");
                VideoFrame video;
                video.width = video.stride = 1920;
                video.height = 1080;
                video.aspectX = 16;
                video.aspectY = 9;
                video.color = pl_color_space_bt709;
                std::vector<uint8_t> decoded(1920 * 1080 * 3 / 2);
                std::ifstream file(argv[1], std::ios::binary);
                file.read(reinterpret_cast<char*>(decoded.data()), decoded.size());
                Require(bool(file), "read decoded real clip");
                Require(SUCCEEDED(full.Upload(video, decoded.data(), decoded.size())), "real clip upload");
                const int outputWidth = outputHeight == 1440 ? 3440 : 1920;
                std::vector<uint8_t> result;
                for (int i = 0; i < 24; ++i)
                {
                    Require(SUCCEEDED(full.RenderOffscreen(outputWidth, outputHeight, result)),
                            "real clip ultrawide render");
                    if (full.BlackBars().measured && full.BlackBars().protectingSubtitles)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                }
                Require(full.BlackBars().measured && full.BlackBars().protectingSubtitles, "real clip captions found");
                int lastText = 0;
                for (int y = outputHeight - 190; y < outputHeight; ++y)
                    for (int x = outputWidth / 5; x < outputWidth * 4 / 5; ++x)
                    {
                        const auto* pixel = result.data() + (size_t(y) * outputWidth + x) * 4;
                        if (pixel[0] > 200 && pixel[1] > 200 && pixel[2] > 200)
                            lastText = y;
                    }
                const int margin = outputHeight - 1 - lastText;
                std::printf("Real clip position %d: last caption row %d, margin %d at %dx%d\n",
                            actualPosition,
                            lastText,
                            margin,
                            outputWidth,
                            outputHeight);
                Require(lastText > outputHeight - 120 && margin >= (outputHeight == 1440 ? 60 : 30),
                        "real clip preserves TV and ultrawide caption bottom margins");
            }
    puts(
        "PASS image position: GPU pixels with and without encoded bars, original subtitle bar overlay, disable/reset, rotations, saved settings and INI compatibility");
}
