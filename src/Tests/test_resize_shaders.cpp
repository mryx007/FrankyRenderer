// Issue #49: playback in a window smaller than the video, then full screen,
// player panels and nearby sizes (a window dragged a little, other panels).
// With dynamic constants sizes are runtime values: only a new scaling ratio
// step of a polar (EWA) scaler or a new black bar layout compiles shaders
// (once, into the disk cache); a nearby size and a size seen before must not
// wait for a shader compile. Without a shader cache on disk (test settings)
// every run starts cold.
// Run through Tests/run_tests.py --resize-shaders (it uses test settings).
#include "Renderer/Backend.h"
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace
{
void Check(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

double Now()
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) * 1000 / f.QuadPart;
}
} // namespace

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrankyResizeShadersTest";
    RegisterClassW(&wc);
    double worst = 0;
    for (int preset : {1, 2})
    {
        HWND window = CreateWindowExW(
            0, wc.lpszClassName, L"Resize shaders", WS_POPUP, 0, 0, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
        Check(window != nullptr, "hidden test window");
        PlaceboBackend b;
        RenderSettings s;
        if (preset == 2)
        {
            // High Quality: polar upscaler and debanding
            ScalingSettings scaling;
            strcpy_s(scaling.filters[0].filter, "ewa_lanczossharp");
            strcpy_s(scaling.filters[2].filter, "ewa_lanczossharp");
            s.scaling = scaling;
            s.deband = true;
        }
        Check(SUCCEEDED(b.SetSettings(s)), "settings");
        Check(SUCCEEDED(b.SetWindow(window)), "window");
        ID3D11Device* device = b.EnsureDevice();
        Check(device != nullptr, "device");
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> done;
        device->CreateQuery(&qd, &done);
        auto present = [&]
        {
            const double start = Now();
            Check(SUCCEEDED(b.Present()), "present");
            context->End(done.Get());
            BOOL flag;
            while (context->GetData(done.Get(), &flag, sizeof(flag), 0) == S_FALSE)
                ;
            return Now() - start;
        };
        VideoFrame f;
        f.width = 1920;
        f.height = 1080;
        f.stride = 1920;
        f.format = PixelFormat::NV12;
        f.color = pl_color_space_bt709;
        std::vector<uint8_t> data(1920 * 1080 * 3 / 2, 128);
        for (size_t i = 0; i < 1920 * 1080; i++)
            data[i] = uint8_t(16 + i * 7 % 219);
        // Playback in the window (the video is scaled down) for a second.
        for (int i = 0; i < 25; i++)
        {
            Check(SUCCEEDED(b.Upload(f, data.data(), data.size())), "upload");
            present();
            Sleep(40);
        }

        // The first full screen and the first black bars (player panels) may
        // compile; the nearby sizes stay within the same ratio steps.
        const struct
        {
            int w, h;
            bool first;
        } sizes[] = {{2400, 1350, true},
                     {2380, 1339, false},
                     {2420, 1361, false},
                     {2400, 1300, true},
                     {2400, 1290, false},
                     {2400, 1350, false},
                     {1280, 720, false},
                     {2400, 1300, false}};

        for (const auto& size : sizes)
        {
            SetWindowPos(window, nullptr, 0, 0, size.w, size.h, SWP_NOZORDER | SWP_NOACTIVATE);
            Check(SUCCEEDED(b.Upload(f, data.data(), data.size())), "upload");
            const double first = present();
            printf("%s: %dx%d first picture %.1f ms%s\n",
                   preset == 1 ? "Default" : "High Quality",
                   size.w,
                   size.h,
                   first,
                   size.first ? " (new)" : "");
            if (!size.first)
                worst = std::max(worst, first);
            for (int i = 0; i < 5; i++)
            {
                Sleep(40);
                Check(SUCCEEDED(b.Upload(f, data.data(), data.size())), "upload");
                present();
            }
        }
        b.SetWindow(nullptr);
        DestroyWindow(window);
    }
    // A shader compile takes several hundred milliseconds; a refresh or two is fine.
    Check(worst < 100, "nearby and known sizes wait for no shader compile");
    puts("PASS nearby and known window sizes without shader compile stalls");
    return 0;
}
