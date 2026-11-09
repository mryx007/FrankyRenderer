#define NOMINMAX
#include "Renderer/StatisticsBitmap.h"
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
