#define NOMINMAX
#include "Renderer/StatisticsBitmap.h"
#include "Renderer/StatisticsGamma.h"
#include <libplacebo/d3d11.h>
#include <cstdio>
#include <cstdlib>

static void Check(bool ok, const char* message)
{
    if (!ok)
    {
        printf("FAIL %s\n", message);
        exit(1);
    }
}

// The Gamma row: source gamma and what the display gets.
static void CheckGamma()
{
    using namespace StatisticsGamma;
    Output sdr;
    sdr.transfer = PL_COLOR_TRC_SRGB;
    Check(Text(PL_COLOR_TRC_BT_1886, sdr) == L"BT.1886 → unchanged", "SDR without gamma settings");
    auto processed = sdr;
    processed.sdrGamma = 240;
    processed.displayGamma = 235;
    Check(Text(PL_COLOR_TRC_BT_1886, processed) == L"BT.1886 → shown as 2.40 · display 2.35",
          "SDR gamma processing shows the chosen gammas");
    processed.displayGamma = 0;
    Check(Text(PL_COLOR_TRC_BT_1886, processed) == L"BT.1886 → shown as 2.40 · display sRGB", "automatic display");
    auto toneMapped = sdr;
    toneMapped.transfer = PL_COLOR_TRC_BT_1886;
    toneMapped.displayGamma = 1;
    toneMapped.displayContrast = 1000;
    Check(Text(PL_COLOR_TRC_PQ, toneMapped) == L"PQ → BT.1886 (1000:1)", "HDR to BT.1886 with contrast");
    toneMapped.displayContrast = 0;
    Check(Text(PL_COLOR_TRC_HLG, toneMapped) == L"HLG → BT.1886 (infinite contrast)", "HLG to BT.1886");
    Output hdr;
    hdr.transfer = PL_COLOR_TRC_PQ;
    hdr.hdr = true;
    Check(Text(PL_COLOR_TRC_PQ, hdr) == L"PQ → PQ (HDR output)", "HDR output");
    Output linear;
    linear.transfer = PL_COLOR_TRC_LINEAR;
    Check(Text(PL_COLOR_TRC_BT_1886, linear) == L"BT.1886 → linear", "16-bit float output");
    auto icc = processed;
    icc.icc = true;
    Check(Text(PL_COLOR_TRC_PQ, icc) == L"PQ → ICC profile", "ICC profile");
    Check(Text(PL_COLOR_TRC_UNKNOWN, sdr) == L"unknown → unchanged", "unknown source");
    puts("PASS statistics gamma: SDR unchanged and processed, exact display gamma, BT.1886 contrast, HDR and "
         "linear output, ICC, unknown source");
}

static std::optional<StatisticsRasterizer::Image> Wait(StatisticsRasterizer& raster, int viewWidth)
{
    const auto until = GetTickCount64() + 3000;
    while (GetTickCount64() < until)
    {
        auto image = raster.Take();
        if (image && image->viewWidth == viewWidth)
            return image;
        Sleep(1);
    }
    return {};
}

static unsigned notifications = 0;

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == StatisticsRasterizer::ReadyMessage)
    {
        ++notifications;
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    CheckGamma();
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrankyStatisticsTest";
    RegisterClassW(&wc);
    HWND window =
        CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "notification window");
    {
        StatisticsRasterizer raster;
        Check(raster.Submit(L"display 120 Hz\nwhite text", 1280, 1080, window), "queue actual GDI text");
        auto image = Wait(raster, 1280);
        Check(image.has_value(), "background rasterization completed");
        Check(image->width <= 1264 && image->height <= 1080 &&
                  image->pixels.size() == size_t(image->width) * image->height * 4,
              "bounded image dimensions");
        Check(image->pixels[0] == 0 && image->pixels[3] == 150, "unchanged translucent background");
        bool glyph = false;
        for (size_t i = 0; i < image->pixels.size(); i += 4)
            if (image->pixels[i] > 200 && image->pixels[i + 3] > 200)
                glyph = true;
        Check(glyph, "white antialiased glyph pixels");
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
        Check(notifications > 0, "completion wakes a paused window");
        // Text size: 100 % is the automatic size (at most 24 pixels), the
        // setting scales it, and it shrinks only when the view is too small.
        Check(raster.Submit(L"display 120 Hz\nwhite text", 3840, 2160), "automatic size");
        const auto automatic = Wait(raster, 3840);
        Check(automatic && automatic->fontHeight == 24, "automatic text size is at most 24 pixels");
        Check(raster.Submit(L"display 120 Hz\nwhite text", 3841, 2160, nullptr, 150), "larger size");
        const auto larger = Wait(raster, 3841);
        Check(larger && larger->fontHeight == 36, "150 percent of the automatic size");
        Check(raster.Submit(L"display 120 Hz\nwhite text", 3842, 2160, nullptr, 50), "smaller size");
        const auto smaller = Wait(raster, 3842);
        Check(smaller && smaller->fontHeight == 12, "50 percent of the automatic size");
        std::wstring tall;
        for (int i = 0; i < 20; i++)
            tall += L"line\n";
        Check(raster.Submit(tall, 801, 600, nullptr, 200), "larger size in a small view");
        const auto shrunk = Wait(raster, 801);
        Check(shrunk && shrunk->fontHeight < 26 && shrunk->height <= 600, "the text shrinks to fit the view");
        std::wstring large;
        for (int i = 0; i < 2000; i++)
            large += L"obsolete statistics\n";
        Check(raster.Submit(large, 3840, 2160), "queue old viewport");
        raster.Reset();
        Check(raster.Submit(L"latest", 320, 200), "queue after reset");
        image = Wait(raster, 320);
        Check(image && image->viewHeight == 200, "reset rejects stale worker results");
        for (int i = 0; i < 100; i++)
            Check(raster.Submit(L"replaced", 800 + i, 600), "replace pending work");
        Check(raster.Submit(L"latest", 777, 480), "latest request");
        Check(Wait(raster, 777).has_value(), "latest request survives queue coalescing");
        // Changing numbers must not make the box jitter: it keeps its largest
        // size for the same view and starts over after a reset.
        Check(raster.Submit(L"render 12.34ms (GPU) - 148% of 8.33ms, over budget\nline", 1000, 600), "wide text");
        const auto wide = Wait(raster, 1000);
        Check(raster.Submit(L"render 1.2ms", 1000, 600), "narrow text");
        const auto narrow = Wait(raster, 1000);
        Check(wide && narrow && narrow->width == wide->width && narrow->height == wide->height,
              "box keeps its size while the text gets shorter");
        raster.Reset();
        Check(raster.Submit(L"render 1.2ms", 1000, 600), "narrow text after reset");
        const auto fresh = Wait(raster, 1000);
        Check(fresh && fresh->width < wide->width, "box starts over after a reset");
        // Markup: headings and rows draw in their own colours, values can be
        // white, yellow or red; the overlay texture is RGB.
        Check(raster.Submit(L"\x01Performance\nDrops\t0 dropped\nRender\t\x02r120% of 8 ms", 1100, 600), "markup text");
        const auto colors = Wait(raster, 1100);
        Check(colors.has_value(), "markup rasterized");
        bool white = false, red = false, blue = false;
        for (size_t i = 0; i < colors->pixels.size(); i += 4)
        {
            const int r = colors->pixels[i], g = colors->pixels[i + 1], b = colors->pixels[i + 2];
            white |= r > 220 && g > 220 && b > 220;
            red |= r > 180 && r > g + 50 && r > b + 50;
            blue |= b > 200 && b > r + 60;
        }
        Check(white && red && blue, "white and red values and a blue heading in RGB order");
        raster.Stop();
        Check(!raster.Submit(L"stopped", 1280, 1080), "no work after shutdown");
    }
    // Statistics in the video area: start at the top of the picture, move up
    // into the top bar only as far as needed, never above the window.
    Check(OverlayTop(140, 940, 500) == 140, "box starts at the top of the picture");
    Check(OverlayTop(140, 940, 900) == 40, "taller box moves up so it ends with the picture");
    Check(OverlayTop(140, 640, 900) == 0, "box never leaves the window");
    puts("PASS worker pixels, paused notification, reset, bounded queue and shutdown, overlay placement");
    auto params = pl_d3d11_default_params;
    auto d3d = pl_d3d11_create(nullptr, &params);
    Check(d3d != nullptr, "D3D11 device");
    {
        StatisticsBitmap bitmap;
        auto update = [&](int w, int h)
        {
            const auto until = GetTickCount64() + 3000;
            while (GetTickCount64() < until)
            {
                if (bitmap.Update(d3d->gpu, L"statistics", w, h))
                    return true;
                Sleep(1);
            }
            return false;
        };
        Check(update(3840, 2160), "actual GPU upload of completed image");
        pl_overlay overlay = {};
        pl_overlay_part part = {};
        bitmap.Describe(overlay, part);
        Check(overlay.tex && overlay.tex->params.w == int(part.src.x1) && part.dst.x0 == 0,
              "GPU texture dimensions and left margin");
        Check(!bitmap.Update(d3d->gpu, L"statistics", 640, 360), "old viewport is not displayed after resize");
        Check(update(640, 360), "replacement viewport uploaded");
        bitmap.Reset();
        Check(update(1280, 720), "GPU reset supports another image");
    }
    pl_d3d11_destroy(&d3d);
    DestroyWindow(window);
    const DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    Check(after <= before, "no GDI handles leaked");
    puts("PASS D3D11 upload, resize, device reset and GDI cleanup");
}
