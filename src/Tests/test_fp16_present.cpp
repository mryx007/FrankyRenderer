// SDR video on a 16-bit float swapchain. The swapchain is scRGB (linear,
// 1.0 = 80 nits). With Windows HDR off, the desktop shows scRGB 1.0 as the
// display's white, so SDR white must be written as 1.0; with Windows HDR on,
// SDR white is written at 203 nits (scRGB 203 / 80).
#include "Renderer/Backend.h"
#include <DirectXPackedVector.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;

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
    wc.lpszClassName = L"FrankyFp16PresentTest";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0,
                                  wc.lpszClassName,
                                  L"16-bit SDR presentation test",
                                  WS_POPUP,
                                  0,
                                  0,
                                  64,
                                  64,
                                  nullptr,
                                  nullptr,
                                  wc.hInstance,
                                  nullptr);
    Check(window != nullptr, "hidden test window");
    PlaceboBackend b;
    RenderSettings s;
    s.autoHdr = false;
    s.hdrMode = 2;
    s.outputFormat = 3;
    s.ditherMethod = 3;
    s.peakDetect = false;
    s.colorAdjustment = false;
    Check(SUCCEEDED(b.SetSettings(s)), "settings");
    Check(SUCCEEDED(b.SetWindow(window)), "window");
    // Limited-range BT.709 NV12: left half white (Y 235), right half black (Y 16).
    VideoFrame f;
    f.width = f.height = 64;
    f.stride = 64;
    f.format = PixelFormat::NV12;
    f.matrix = PL_COLOR_SYSTEM_BT_709;
    f.levels = PL_COLOR_LEVELS_LIMITED;
    f.color = pl_color_space_bt709;
    std::vector<uint8_t> data(64 * 64 * 3 / 2, 128);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            data[y * 64 + x] = x < 32 ? 235 : 16;
    Check(SUCCEEDED(b.Upload(f, data.data(), data.size())), "SDR source");
    bool advancedColor = false;
    float white = -1, black = -1;
    DXGI_FORMAT presented = DXGI_FORMAT_UNKNOWN;
    b.SetPresentProbeForTest(
        [&](ID3D11Texture2D* texture)
        {
            ComPtr<ID3D11Device> device;
            texture->GetDevice(&device);
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            D3D11_TEXTURE2D_DESC d;
            texture->GetDesc(&d);
            presented = d.Format;
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            d.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> staging;
            Check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "capture texture");
            context->CopyResource(staging.Get(), texture);
            D3D11_MAPPED_SUBRESOURCE map = {};
            Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "capture pixels");
            if (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            {
                const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(map.pData) +
                                                                    map.RowPitch * (d.Height / 2));
                white = DirectX::PackedVector::XMConvertHalfToFloat(row[(d.Width / 8) * 4 + 1]);
                black = DirectX::PackedVector::XMConvertHalfToFloat(row[(d.Width * 7 / 8) * 4 + 1]);
            }
            context->Unmap(staging.Get(), 0);
            // Windows HDR state of the display that shows this window.
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
                if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)) && desc.Monitor == monitor)
                    advancedColor = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            }
        });
    Check(SUCCEEDED(b.Present()), "present");
    Check(presented == DXGI_FORMAT_R16G16B16A16_FLOAT, "16-bit float swapchain");
    const float expected = advancedColor ? 203.f / 80.f : 1.f;
    printf("Windows HDR %s: SDR white %.4f (expected %.4f), black %.4f\n",
           advancedColor ? "on" : "off",
           white,
           expected,
           black);
    Check(std::abs(white - expected) < .02f * expected, "SDR white level on the scRGB swapchain");
    Check(std::abs(black) < .005f, "SDR black level");
    puts("PASS 16-bit float output keeps SDR white and black levels");
    DestroyWindow(window);
    return 0;
}
