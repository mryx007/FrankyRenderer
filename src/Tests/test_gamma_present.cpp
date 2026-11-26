#include "Renderer/Backend.h"
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;

// Display gamma and SDR gamma processing on an actual 10-bit swapchain:
// pure powers between libplacebo's curves must reach the exact gamma.
static void Check(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrankyGammaPresentTest";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(
        0, wc.lpszClassName, L"Display gamma test", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "hidden test window");
    PlaceboBackend b;
    RenderSettings s;
    s.autoHdr = false;
    s.hdrMode = 2;
    s.outputFormat = 2;
    s.ditherMethod = 3;
    s.peakDetect = false;
    s.colorAdjustment = false;
    Check(SUCCEEDED(b.SetSettings(s)), "settings");
    Check(SUCCEEDED(b.SetWindow(window)), "window");
    double average = 0;
    b.SetPresentProbeForTest(
        [&](ID3D11Texture2D* texture)
        {
            ComPtr<ID3D11Device> device;
            texture->GetDevice(&device);
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            D3D11_TEXTURE2D_DESC d;
            texture->GetDesc(&d);
            Check(d.Format == DXGI_FORMAT_R10G10B10A2_UNORM, "10-bit swapchain");
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            d.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> staging;
            Check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "capture texture");
            context->CopyResource(staging.Get(), texture);
            D3D11_MAPPED_SUBRESOURCE map = {};
            Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "capture pixels");
            double total = 0;
            for (UINT y = 16; y < 48; y++)
                for (UINT x = 16; x < 48; x++)
                {
                    auto row = static_cast<const uint8_t*>(map.pData) + y * map.RowPitch;
                    total += (reinterpret_cast<const uint32_t*>(row)[x] & 1023) / 1023.;
                }
            average = total / (32 * 32);
            context->Unmap(staging.Get(), 0);
        });
    auto upload = [&](bool hdr, int luma = 512)
    {
        VideoFrame f;
        f.width = f.height = 64;
        f.stride = 128;
        f.format = PixelFormat::P010;
        f.matrix = hdr ? PL_COLOR_SYSTEM_BT_2020_NC : PL_COLOR_SYSTEM_BT_709;
        f.color = hdr ? pl_color_space_hdr10 : pl_color_space_bt709;
        f.color.hdr.max_luma = hdr ? 1000 : 0;
        std::vector<uint16_t> data(64 * 64 * 3 / 2, 512 << 6);
        for (int i = 0; i < 64 * 64; i++)
            data[i] = static_cast<uint16_t>(luma << 6);
        Check(SUCCEEDED(b.Upload(f, reinterpret_cast<uint8_t*>(data.data()), data.size() * 2)), "source");
    };
    auto measure = [&](int display, int sdr)
    {
        s.displayGammaCurve = display;
        s.sdrGammaCurve = sdr;
        Check(SUCCEEDED(b.SetSettings(s)), "gamma settings");
        Check(SUCCEEDED(b.Present()), "present");
        return average;
    };
    auto srgbEncode = [](double l) { return l <= .0031308 ? 12.92 * l : 1.055 * std::pow(l, 1 / 2.4) - .055; };
    const double v = (512 - 64) / 876.;

    // Without SDR gamma processing the SDR signal reaches the display as it is
    // (issue #48: BT.1886 was converted to sRGB, shadows up to 2.3 8-bit steps
    // too dark). Dark patches of 8-bit limited video, any display gamma.
    for (int display : {0, 1, 240})
        for (int luma8 : {17, 18, 20, 24, 30, 45, 64, 128, 200})
        {
            upload(false, luma8 * 4);
            const double got = measure(display, 0), want = (luma8 - 16) / 219.;
            printf("SDR off, display %d, luma %d: %.5f expected %.5f\n", display, luma8, got, want);
            Check(std::abs(got - want) < 1.5 / 1023, "SDR without gamma processing passes unchanged");
        }

    upload(false);
    const double off = measure(235, 0);
    printf("SDR off (display 2.35): %.5f\n", off);
    Check(std::abs(off - v) < 1.5 / 1023, "SDR off leaves the signal unchanged");

    struct Case
    {
        int display, sdr;
        double expected;
    } cases[] = {
        {240, 240, v},
        {280, 280, v},
        {275, 275, v},                           // libplacebo curves on both sides: pass-through
        {235, 235, v},                           // exact powers on both sides: pass-through
        {250, 200, std::pow(v, 2.0 / 2.5)},      // look like 2.0 on a 2.5 display
        {185, 255, std::pow(v, 2.55 / 1.85)},    // look like 2.55 on a 1.85 display
        {0, 235, srgbEncode(std::pow(v, 2.35))}, // sRGB display
        {1, 225, std::pow(v, 2.25 / 2.4)},       // BT.1886 with infinite contrast is 2.4
    };

    for (const auto& c : cases)
    {
        const double got = measure(c.display, c.sdr);
        printf("SDR %d on display %d: %.5f expected %.5f\n", c.sdr, c.display, got, c.expected);
        Check(std::abs(got - c.expected) < 1.5 / 1023, "SDR gamma processing reaches the exact gamma");
    }

    upload(true);
    const double hdr24 = measure(240, 0), hdr235 = measure(235, 0), hdr26 = measure(260, 0), hdr255 = measure(255, 0);
    printf("HDR to SDR: 2.40 %.5f 2.35 %.5f 2.60 %.5f 2.55 %.5f\n", hdr24, hdr235, hdr26, hdr255);
    Check(std::abs(hdr235 - std::pow(hdr24, 2.4 / 2.35)) < 1.5 / 1023, "HDR encoded for a 2.35 display");
    Check(std::abs(hdr255 - std::pow(hdr26, 2.6 / 2.55)) < 1.5 / 1023, "HDR encoded for a 2.55 display");
    const double hdr28 = measure(280, 0), hdr275 = measure(275, 0);
    printf("HDR to SDR: 2.80 %.5f 2.75 %.5f\n", hdr28, hdr275);
    Check(hdr28 > hdr26, "2.80 display gets a brighter signal than 2.60");
    Check(std::abs(hdr275 - std::pow(hdr28, 2.8 / 2.75)) < 1.5 / 1023, "HDR encoded for a 2.75 display");
    Check(measure(235, 235) == hdr235, "SDR gamma processing leaves HDR alone");

    // HDR to SDR gamma processing (issue #55): the tone mapped picture looks
    // like the chosen gamma instead of the display's own.
    const double hdrSrgb = measure(0, 0);
    s.hdrSdrGammaCurve = 260;
    const double hdrDark = measure(240, 0), hdrDarkSrgb = measure(0, 0);
    printf("HDR to SDR as 2.60: display 2.40 %.5f expected %.5f, sRGB display %.5f expected %.5f\n",
           hdrDark,
           std::pow(hdr24, 2.6 / 2.4),
           hdrDarkSrgb,
           std::pow(hdrSrgb, 2.6 / 2.2));
    Check(std::abs(hdrDark - std::pow(hdr24, 2.6 / 2.4)) < 1.5 / 1023, "HDR to SDR looks like 2.60 on a 2.40 display");
    Check(std::abs(hdrDarkSrgb - std::pow(hdrSrgb, 2.6 / 2.2)) < 1.5 / 1023,
          "HDR to SDR looks like 2.60 on an sRGB display");
    s.hdrSdrGammaCurve = 240;
    Check(std::abs(measure(240, 0) - hdr24) < 1.5 / 1023, "the display's own gamma leaves HDR to SDR unchanged");
    s.hdrSdrGammaCurve = 1;
    Check(std::abs(measure(240, 260) - hdrDark) < 1.5 / 1023, "Like SDR follows SDR Gamma Processing");
    Check(measure(240, 0) == hdr24, "Like SDR with SDR Gamma Processing off changes nothing");
    s.hdrSdrGammaCurve = 260;
    upload(false);
    Check(std::abs(measure(240, 0) - v) < 1.5 / 1023, "HDR to SDR gamma processing leaves SDR alone");
    s.hdrSdrGammaCurve = 0;

    // With peak detection or colour adjustment (both on by default) libplacebo
    // took the exact gamma for resizing the output and switched it off after
    // one picture: later changes and in-between display gammas did nothing.
    s.peakDetect = s.colorAdjustment = true;
    upload(true);
    double settled = 0;
    for (int i = 0; i < 3; i++)
        settled = measure(240, 0);
    for (int curve : {280, 200, 260, 220, 280})
    {
        s.hdrSdrGammaCurve = curve;
        const double got = measure(240, 0), want = std::pow(settled, curve / 240.);
        printf("peak detection, HDR to SDR %d: %.5f expected %.5f\n", curve, got, want);
        Check(std::abs(got - want) < 1.5 / 1023, "HDR to SDR gamma keeps working with peak detection");
    }
    s.hdrSdrGammaCurve = 0;
    const double display235 = measure(235, 0);
    printf("peak detection, display 2.35: %.5f expected %.5f\n", display235, std::pow(settled, 2.4 / 2.35));
    Check(std::abs(display235 - std::pow(settled, 2.4 / 2.35)) < 1.5 / 1023,
          "display gamma 2.35 keeps working with peak detection");
    s.peakDetect = s.colorAdjustment = false;

    s.outputLevels = 2;
    upload(false);
    const double limited = measure(235, 235);
    const double expected = (64 + v * 876) / 1023;
    printf("TV levels: %.5f expected %.5f\n", limited, expected);
    Check(std::abs(limited - expected) < 1.5 / 1023, "output gamma keeps TV levels");

    // Ctrl+J names what the display gets for every video, not only for Dolby Vision.
    s.outputLevels = 0;
    b.ShowStatistics(true);
    for (bool hdr : {true, false})
    {
        upload(hdr);
        measure(240, 0);
        measure(240, 0);
        const auto text = b.StatisticsSnapshot();
        printf("statistics %s: output row %s\n",
               hdr ? "HDR" : "SDR",
               text.find(L"Output\tSDR") != text.npos ? "SDR" : "missing");
        Check(text.find(L"Output\tSDR") != text.npos,
              hdr ? "Ctrl+J shows HDR to SDR as SDR output" : "Ctrl+J shows SDR output");
    }

    b.SetWindow(nullptr);
    DestroyWindow(window);
    puts("PASS display gamma and SDR gamma processing reach exact pure powers on a 10-bit swapchain");
}
