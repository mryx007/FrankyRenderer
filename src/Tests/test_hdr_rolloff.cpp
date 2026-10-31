// HDR output modes on a 4000-nit HDR10 ramp, rendered to an HDR10 target:
// passthrough (mode 1) keeps the source luminance, "Tone Map HDR to Display
// Peak" (mode 3) rolls highlights off to HDR Target Peak and leaves dark
// content unchanged.
#include "Renderer/Backend.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void Check(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

// SMPTE ST 2084 code (0..1) to nits.
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
    // A gray ramp across 64 columns from 0 to 4000 nits, limited-range 10-bit.
    VideoFrame f;
    f.width = f.height = 64;
    f.stride = 128;
    f.format = PixelFormat::P010;
    f.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
    f.color = pl_color_space_hdr10;
    f.color.hdr.max_luma = 4000;
    f.color.hdr.max_cll = 4000;
    std::vector<uint16_t> data(64 * 64 * 3 / 2, 512 << 6);
    std::vector<double> nits(64);
    for (int x = 0; x < 64; x++)
    {
        nits[x] = 4000. * x / 63;
        const int code = int(std::lround(64 + NitsToPq(nits[x]) * 876));
        for (int y = 0; y < 64; y++)
            data[y * 64 + x] = uint16_t(code << 6);
    }
    const auto render = [&](int mode, float peak, const char* curve = "auto")
    {
        PlaceboBackend b;
        RenderSettings s;
        s.hdrMode = mode;
        s.hdrPassthrough = mode != 2;
        s.hdrNits = peak;
        s.toneMapping = curve;
        s.peakDetect = false;
        s.colorAdjustment = false;
        s.ditherMethod = 3;
        Check(SUCCEEDED(b.SetSettings(s)), "settings");
        Check(SUCCEEDED(b.Upload(f, reinterpret_cast<uint8_t*>(data.data()), data.size() * 2)), "HDR source");
        std::vector<uint8_t> out;
        Check(SUCCEEDED(b.RenderOffscreen(64, 64, out, true, 16)), "HDR10 output");
        std::vector<double> result(64);
        const auto* pixels = reinterpret_cast<const uint16_t*>(out.data());
        for (int x = 0; x < 64; x++)
            result[x] = PqToNits(pixels[(32 * 64 + x) * 4 + 1] / 65535.);
        return result;
    };

    const auto passthrough = render(1, 0);
    printf("passthrough: 1000 nits -> %.0f, 4000 nits -> %.0f\n", passthrough[16], passthrough[63]);
    Check(std::abs(passthrough[63] - 4000) < 200, "passthrough keeps the 4000-nit peak");
    Check(std::abs(passthrough[16] - nits[16]) < .05 * nits[16], "passthrough keeps mid highlights");

    for (const char* curve : {"auto", "spline", "bt2390"})
        for (const float peak : {500.f, 1000.f})
        {
            const auto rolled = render(3, peak, curve);
            printf("%-6s display peak %4.0f: 63 nits -> %5.1f, %.0f nits -> %4.0f, 4000 nits -> %4.0f\n",
                   curve,
                   peak,
                   rolled[1],
                   nits[16],
                   rolled[16],
                   rolled[63]);
            Check(rolled[63] <= peak * 1.02 && rolled[63] > peak * .8, "highlights end at the display peak");
            for (int x = 1; x < 64; x++)
                Check(rolled[x] >= rolled[x - 1] - .5, "roll-off is monotonic");
            // BT.2390 leaves everything below its knee unchanged.
            if (!strcmp(curve, "bt2390"))
                Check(std::abs(rolled[1] - nits[1]) < .05 * nits[1], "BT.2390 keeps content below the knee");
        }
    puts("PASS HDR passthrough keeps the source peak; Tone Map HDR to Display Peak rolls highlights off to it");
    return 0;
}
