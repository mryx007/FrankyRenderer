// Real swapchain output of "Tone Map HDR to Display Peak" with Windows HDR on:
// a 4000-nit HDR10 ramp must end at the peak Windows reports for the display
// (HDR Target Peak 0) or at HDR Target Peak. Skips when Windows HDR is off.
#include "Renderer/Backend.h"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using Microsoft::WRL::ComPtr;

static void Check(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

static double PqToNits(double code)
{
    const double m1 = 2610. / 16384, m2 = 2523. / 4096 * 128, c1 = 3424. / 4096, c2 = 2413. / 4096 * 32,
                 c3 = 2392. / 4096 * 32;
    const double p = std::pow(code, 1 / m2);
    return 10000 * std::pow(std::max(p - c1, 0.) / (c2 - c3 * p), 1 / m1);
}

static double NitsToPq(double nits)
{
    const double m1 = 2610. / 16384, m2 = 2523. / 4096 * 128, c1 = 3424. / 4096, c2 = 2413. / 4096 * 32,
                 c3 = 2392. / 4096 * 32;
    const double y = std::pow(nits / 10000, m1);
    return std::pow((c1 + c2 * y) / (1 + c3 * y), m2);
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrankyHdrDisplayPeakTest";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(
        0, wc.lpszClassName, L"HDR display peak test", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "test window");
    ShowWindow(window, SW_SHOWNOACTIVATE);

    VideoFrame f;
    f.width = f.height = 64;
    f.stride = 128;
    f.format = PixelFormat::P010;
    f.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
    f.color = pl_color_space_hdr10;
    f.color.hdr.max_luma = 4000;
    f.color.hdr.max_cll = 4000;
    std::vector<uint16_t> data(64 * 64 * 3 / 2, 512 << 6);
    for (int x = 0; x < 64; x++)
    {
        const int code = int(std::lround(64 + NitsToPq(4000. * x / 63) * 876));
        for (int y = 0; y < 64; y++)
            data[y * 64 + x] = uint16_t(code << 6);
    }

    const auto run = [&](int mode, float targetPeak, float& displayPeak, bool& advanced)
    {
        PlaceboBackend b;
        RenderSettings s;
        s.hdrMode = mode;
        s.hdrPassthrough = true;
        s.hdrNits = targetPeak;
        s.toneMapping = "bt2390";
        s.peakDetect = false;
        s.colorAdjustment = false;
        s.ditherMethod = 3;
        Check(SUCCEEDED(b.SetSettings(s)), "settings");
        Check(SUCCEEDED(b.SetWindow(window)), "window");
        Check(SUCCEEDED(b.Upload(f, reinterpret_cast<uint8_t*>(data.data()), data.size() * 2)), "HDR source");
        double brightest = -1;
        b.SetPresentProbeForTest(
            [&](ID3D11Texture2D* texture)
            {
                ComPtr<ID3D11Device> device;
                texture->GetDevice(&device);
                ComPtr<ID3D11DeviceContext> context;
                device->GetImmediateContext(&context);
                D3D11_TEXTURE2D_DESC d;
                texture->GetDesc(&d);
                const auto format = d.Format;
                d.Usage = D3D11_USAGE_STAGING;
                d.BindFlags = 0;
                d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                d.MiscFlags = 0;
                ComPtr<ID3D11Texture2D> staging;
                Check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "capture texture");
                context->CopyResource(staging.Get(), texture);
                D3D11_MAPPED_SUBRESOURCE map = {};
                Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "capture pixels");
                Check(format == DXGI_FORMAT_R10G10B10A2_UNORM, "HDR10 swapchain");
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map.pData) +
                                                                    map.RowPitch * (d.Height / 2));
                brightest = PqToNits(((row[d.Width - 2] >> 10) & 1023) / 1023.);
                context->Unmap(staging.Get(), 0);
                ComPtr<IDXGIDevice> dxgi;
                ComPtr<IDXGIAdapter> adapter;
                device.As(&dxgi);
                dxgi->GetAdapter(&adapter);
                const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
                ComPtr<IDXGIOutput> output;
                for (UINT i = 0; SUCCEEDED(adapter->EnumOutputs(i, &output)); ++i, output.Reset())
                {
                    ComPtr<IDXGIOutput6> output6;
                    DXGI_OUTPUT_DESC1 desc = {};
                    if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)) &&
                        desc.Monitor == monitor)
                    {
                        advanced = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                        displayPeak = desc.MaxLuminance;
                    }
                }
            });
        Check(SUCCEEDED(b.Present()), "present");
        b.SetWindow(nullptr);
        return brightest;
    };

    float displayPeak = 0;
    bool advanced = false;
    const double automatic = run(3, 0, displayPeak, advanced);
    if (!advanced)
    {
        puts("SKIP Windows HDR is off on this display");
        return 0;
    }
    printf("display reports %.0f nits; 4000-nit source -> %.0f nits\n", displayPeak, automatic);
    Check(std::abs(automatic - displayPeak) < .04 * displayPeak, "Auto ends at the peak Windows reports");
    const double fixed = run(3, 400, displayPeak, advanced);
    printf("HDR Target Peak 400: 4000-nit source -> %.0f nits\n", fixed);
    Check(std::abs(fixed - 400) < 16, "HDR Target Peak overrides the reported peak");
    puts("PASS Tone Map HDR to Display Peak on a real HDR10 swapchain");
    DestroyWindow(window);
    return 0;
}
