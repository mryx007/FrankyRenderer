// The same picture through the 8-bit, 10-bit and 16-bit float outputs must
// look the same on the display. Each case renders grey steps and colours (SDR
// BT.709 and HDR10 sources) with one setting changed and compares what the
// display shows from each output:
//   Windows HDR off: integer outputs go to the display as they are; Windows
//   encodes a 16-bit float (linear) output with the sRGB curve.
//   Windows HDR on: integer SDR is shown as sRGB at Windows' SDR content
//   brightness, integer PQ as PQ, 16-bit float as scRGB (1.0 = 80 nits).
// 8-bit cannot carry PQ, so HDR passthrough compares 10-bit and 16-bit only.
// Run through Tests/run_tests.py --output-consistency (it uses test settings).
#include "Renderer/Backend.h"
#include "Renderer/SdrWhiteLevel.h"
#include <DirectXPackedVector.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace
{
constexpr int PatchWidth = 16, Height = 32;

struct Patch
{
    const char* name;
    double r, g, b; // encoded in the source's transfer
};

const Patch Patches[] = {{"black", 0, 0, 0},
                         {"grey 5%", .05, .05, .05},
                         {"grey 10%", .1, .1, .1},
                         {"grey 20%", .2, .2, .2},
                         {"grey 35%", .35, .35, .35},
                         {"grey 50%", .5, .5, .5},
                         {"grey 65%", .65, .65, .65},
                         {"grey 80%", .8, .8, .8},
                         {"white", 1, 1, 1},
                         {"red", .7, .1, .1},
                         {"green", .1, .7, .1},
                         {"blue", .1, .1, .7},
                         {"skin", .75, .55, .45}};
constexpr int PatchCount = int(std::size(Patches)), Width = PatchCount * PatchWidth;

double SrgbEncode(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
}

double SrgbDecode(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double PqDecode(double e)
{
    const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32,
                 c3 = 2392.0 / 4096 * 32;
    const double p = std::pow(std::clamp(e, 0.0, 1.0), 1 / m2);
    return 10000 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1 / m1);
}

struct Rgb
{
    double c[3] = {};
};

struct Output
{
    bool ok = false, hdrOn = false, pq = false;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    float windowsWhite = 0;
    // What the display shows: relative to SDR white (HDR on: nits); with
    // Windows HDR on as CIE XYZ, since PQ outputs use BT.2020 and scRGB BT.709.
    std::array<Rgb, PatchCount> light;
};

struct Case
{
    std::string name;
    bool hdrSource = false;
    int hdrMode = 2;
    RenderSettings settings;
};

HWND g_window = nullptr;

std::vector<uint8_t> Picture(bool hdr)
{
    // NV12, limited range, BT.709 (SDR) or BT.2020 NCL (HDR) matrix.
    const double kr = hdr ? .2627 : .2126, kb = hdr ? .0593 : .0722, kg = 1 - kr - kb;
    std::vector<uint8_t> data(Width * Height * 3 / 2);
    for (int p = 0; p < PatchCount; ++p)
    {
        const auto& c = Patches[p];
        const double y = kr * c.r + kg * c.g + kb * c.b;
        const double cb = (c.b - y) / (2 * (1 - kb)), cr = (c.r - y) / (2 * (1 - kr));
        const auto Y = uint8_t(std::lround(16 + 219 * y)), U = uint8_t(std::lround(128 + 224 * cb)),
                   V = uint8_t(std::lround(128 + 224 * cr));
        for (int row = 0; row < Height; ++row)
            for (int x = p * PatchWidth; x < (p + 1) * PatchWidth; ++x)
                data[row * Width + x] = Y;
        for (int row = 0; row < Height / 2; ++row)
            for (int x = p * PatchWidth / 2; x < (p + 1) * PatchWidth / 2; ++x)
            {
                data[Width * Height + row * Width + 2 * x] = U;
                data[Width * Height + row * Width + 2 * x + 1] = V;
            }
    }
    return data;
}

Output Render(const Case& test, int outputFormat)
{
    Output out;
    PlaceboBackend b;
    RenderSettings s = test.settings;
    s.outputFormat = outputFormat;
    s.hdrMode = test.hdrMode;
    s.hdrPassthrough = true;
    s.autoHdr = false;
    s.peakDetect = false;
    s.colorAdjustment = false;
    s.ditherMethod = 3;
    if (FAILED(b.SetSettings(s)) || FAILED(b.SetWindow(g_window)))
        return out;
    VideoFrame f;
    f.width = Width;
    f.height = Height;
    f.stride = Width;
    f.format = PixelFormat::NV12;
    f.levels = PL_COLOR_LEVELS_LIMITED;
    if (test.hdrSource)
    {
        f.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
        f.color = pl_color_space_hdr10;
        f.color.hdr.max_luma = 1000;
        f.color.hdr.min_luma = .005f;
    }
    else
    {
        f.matrix = PL_COLOR_SYSTEM_BT_709;
        f.color = pl_color_space_bt709;
    }
    const auto data = Picture(test.hdrSource);
    if (FAILED(b.Upload(f, data.data(), data.size())))
        return out;
    pl_color_space target = {};
    b.SetPresentColorProbeForTest([&](const pl_color_space& color) { target = color; });
    std::vector<std::array<double, 3>> raw(PatchCount);
    b.SetPresentProbeForTest(
        [&](ID3D11Texture2D* texture)
        {
            ComPtr<ID3D11Device> device;
            texture->GetDevice(&device);
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            D3D11_TEXTURE2D_DESC d;
            texture->GetDesc(&d);
            out.format = d.Format;
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            d.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &staging)))
                return;
            context->CopyResource(staging.Get(), texture);
            D3D11_MAPPED_SUBRESOURCE map = {};
            if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)))
                return;
            for (int p = 0; p < PatchCount; ++p)
            {
                double sum[3] = {};
                int n = 0;
                for (UINT row = d.Height / 4; row < d.Height * 3 / 4; ++row)
                {
                    const auto* line = static_cast<const uint8_t*>(map.pData) + map.RowPitch * row;
                    for (int x = p * PatchWidth + 4; x < (p + 1) * PatchWidth - 4; ++x)
                    {
                        double v[3] = {};
                        if (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
                            for (int i = 0; i < 3; ++i)
                                v[i] = DirectX::PackedVector::XMConvertHalfToFloat(
                                    reinterpret_cast<const uint16_t*>(line)[x * 4 + i]);
                        else if (d.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
                        {
                            const uint32_t word = reinterpret_cast<const uint32_t*>(line)[x];
                            for (int i = 0; i < 3; ++i)
                                v[i] = ((word >> (10 * i)) & 1023) / 1023.0;
                        }
                        else
                        {
                            const bool bgr = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
                            for (int i = 0; i < 3; ++i)
                                v[i] = line[x * 4 + (bgr ? 2 - i : i)] / 255.0;
                        }
                        for (int i = 0; i < 3; ++i)
                            sum[i] += v[i];
                        ++n;
                    }
                }
                for (int i = 0; i < 3; ++i)
                    raw[p][i] = n ? sum[i] / n : 0;
            }
            context->Unmap(staging.Get(), 0);
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            device.As(&dxgi);
            dxgi->GetAdapter(&adapter);
            const HMONITOR monitor = MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST);
            ComPtr<IDXGIOutput> output;
            for (UINT i = 0; SUCCEEDED(adapter->EnumOutputs(i, &output)); ++i, output.Reset())
            {
                ComPtr<IDXGIOutput6> output6;
                DXGI_OUTPUT_DESC1 desc = {};
                if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)) && desc.Monitor == monitor)
                {
                    out.hdrOn = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    out.windowsWhite = SdrWhiteLevel::Nits(desc.DeviceName);
                }
            }
        });
    if (FAILED(b.Present()) || out.format == DXGI_FORMAT_UNKNOWN)
        return out;
    const bool fp16 = out.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (GetEnvironmentVariableW(L"OUTPUT_CONSISTENCY_TRACE", nullptr, 0))
        printf("      [%s, format %d: target transfer %d primaries %d]\n",
               test.name.c_str(),
               int(out.format),
               int(target.transfer),
               int(target.primaries));
    out.pq = !fp16 && target.transfer == PL_COLOR_TRC_PQ;
    const bool limited = !fp16 && test.settings.outputLevels == 2;
    const double white = out.windowsWhite > 0 ? out.windowsWhite : 203;
    for (int p = 0; p < PatchCount; ++p)
        for (int i = 0; i < 3; ++i)
        {
            double v = raw[p][i];
            if (limited)
                v = (v * (out.format == DXGI_FORMAT_R10G10B10A2_UNORM ? 1023 : 255) -
                     (out.format == DXGI_FORMAT_R10G10B10A2_UNORM ? 64 : 16)) /
                    (out.format == DXGI_FORMAT_R10G10B10A2_UNORM ? 876 : 219);
            double light;
            if (!out.hdrOn)
                // The display decodes every signal alike; Windows encodes linear float as sRGB.
                light = fp16 ? std::clamp(v, 0.0, 1.0) : SrgbDecode(v);
            else if (fp16)
                light = v * 80;
            else if (out.pq)
                light = PqDecode(v);
            else
                light = SrgbDecode(v) * white;
            out.light[p].c[i] = light;
        }
    if (out.hdrOn)
    {
        // Windows reads PQ in the output's primaries, SDR and scRGB as BT.709.
        const auto primaries = out.pq ? target.primaries : PL_COLOR_PRIM_BT_709;
        const pl_matrix3x3 toXyz = pl_get_rgb2xyz_matrix(pl_raw_primaries_get(primaries));
        for (auto& rgb : out.light)
        {
            Rgb xyz;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    xyz.c[r] += toXyz.m[r][c] * rgb.c[c];
            rgb = xyz;
        }
    }
    out.ok = true;
    return out;
}

int Compare(const Case& test, const char* nameA, const Output& a, const char* nameB, const Output& b)
{
    // Light relative to white: 1 % of the value plus 0.2 % of white (8-bit steps, dither).
    const double white = !a.hdrOn ? 1 : (a.windowsWhite > 0 ? a.windowsWhite : 203);
    int failed = 0;
    std::string details;
    for (int p = 0; p < PatchCount; ++p)
        for (int i = 0; i < 3; ++i)
        {
            const double x = a.light[p].c[i], y = b.light[p].c[i];
            if (std::abs(x - y) > .01 * std::max(x, y) + .002 * white)
            {
                char line[200];
                sprintf_s(line,
                          "      %-9s %c: %s %.4f, %s %.4f (%+.1f %%)\n",
                          Patches[p].name,
                          a.hdrOn ? "XYZ"[i] : "RGB"[i],
                          nameA,
                          x,
                          nameB,
                          y,
                          x > 0 ? (y / x - 1) * 100 : 0.0);
                if (failed < 6)
                    details += line;
                ++failed;
            }
        }
    printf("%s %-44s %s vs %s%s\n", failed ? "FAIL" : "PASS", test.name.c_str(), nameA, nameB, failed ? ":" : "");
    if (failed)
        printf("%s", details.c_str());
    return failed ? 1 : 0;
}
} // namespace

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrankyOutputConsistencyTest";
    RegisterClassW(&wc);
    g_window = CreateWindowExW(
        0, wc.lpszClassName, L"Output consistency", WS_POPUP, 0, 0, Width, Height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_window)
        return 1;

    std::vector<Case> cases;
    auto add = [&](const std::string& name, bool hdr, int mode, auto change)
    {
        Case c;
        c.name = name;
        c.hdrSource = hdr;
        c.hdrMode = mode;
        change(c.settings);
        cases.push_back(c);
    };
    for (bool hdr : {false, true})
    {
        const std::string source = hdr ? "HDR10 tone mapped to SDR" : "SDR";
        add(source, hdr, 2, [](RenderSettings&) {});
        add(source + ", Display Gamma BT.1886", hdr, 2, [](RenderSettings& s) { s.displayGammaCurve = 1; });
        add(source + ", Display Gamma 2.40", hdr, 2, [](RenderSettings& s) { s.displayGammaCurve = 240; });
        add(source + ", Display Gamut P3", hdr, 2, [](RenderSettings& s) { s.displayPrimaries = 5; });
        add(source + ", RGB Levels TV", hdr, 2, [](RenderSettings& s) { s.outputLevels = 2; });
        add(source + ", RGB Levels PC", hdr, 2, [](RenderSettings& s) { s.outputLevels = 1; });
        if (!hdr)
            add(source + ", SDR Gamma Processing 2.40", hdr, 2, [](RenderSettings& s) { s.sdrGammaCurve = 240; });
    }
    add("HDR10 passthrough", true, 1, [](RenderSettings&) {});
    add("HDR10 tone mapped to display peak", true, 3, [](RenderSettings&) {});

    int failures = 0, skipped = 0;
    bool reported = false;
    for (const auto& test : cases)
    {
        const auto eight = Render(test, 1), ten = Render(test, 2), sixteen = Render(test, 3);
        if (!eight.ok || !ten.ok || !sixteen.ok)
        {
            printf("FAIL %-44s could not render\n", test.name.c_str());
            ++failures;
            continue;
        }
        if (!reported)
        {
            reported = true;
            printf("Windows HDR %s", ten.hdrOn ? "on" : "off");
            if (ten.hdrOn)
                printf(", SDR content brightness %.0f nits", ten.windowsWhite);
            puts("");
        }
        // HDR output only exists with Windows HDR on.
        if (test.hdrMode != 2 && !ten.hdrOn)
        {
            printf("SKIP %-44s needs Windows HDR on\n", test.name.c_str());
            ++skipped;
            continue;
        }
        if (!ten.pq)
            failures += Compare(test, "10-bit", ten, "8-bit", eight);
        failures += Compare(test, "10-bit", ten, "16-bit", sixteen);
    }
    DestroyWindow(g_window);
    printf("%s output consistency: %d difference(s), %d case(s) skipped\n", failures ? "FAIL" : "PASS", failures, skipped);
    return failures ? 1 : 0;
}
