#include "Settings.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        printf("FAIL %s\n", what);
        exit(1);
    }
}

static long long Delta(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    long long d = 0;
    for (size_t i = 0; i < a.size(); i++)
        if (i % 4 != 3)
            d += abs(int(a[i]) - b[i]);
    return d;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (auto module : {L"ucrtbase.dll", L"msvcrt.dll"})
        if (auto h = GetModuleHandleW(module))
            if (auto mode = reinterpret_cast<int(__cdecl*)(int)>(GetProcAddress(h, "_set_error_mode")))
                mode(1);
    Check(GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", nullptr, 0) > 0, "private registry");
    PlaceboSettings saved;
    Check(!ActiveTonal(saved.tonal), "neutral defaults");
    for (int zone = 0; zone < 3; zone++)
    {
        saved.tonal.exposure[zone] = float(zone) - 1;
        saved.tonal.contrast[zone] = .1f;
        saved.tonal.saturation[zone] = -.2f;
    }
    Check(SUCCEEDED(SaveSettings(saved)), "save");
    const auto read = LoadSettings();
    Check(!memcmp(&saved.tonal, &read.tonal, sizeof(saved.tonal)), "all nine controls persist");
    PlaceboBackend backend;
    RenderSettings s;
    s.autoHdr = false;
    s.colorAdjustment = false;
    s.ditherMethod = 3;
    s.peakDetect = false;
    VideoFrame f;
    f.width = f.height = 64;
    f.stride = 128;
    f.format = PixelFormat::P010;
    f.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
    f.color = pl_color_space_hdr10;
    f.color.hdr.max_luma = 4000;
    std::vector<uint16_t> input(64 * 64 * 3 / 2, 512 << 6);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            input[y * 64 + x] = (64 + x * 876 / 63) << 6;
    auto upload = [&]()
    {
        Check(SUCCEEDED(backend.Upload(f, reinterpret_cast<const uint8_t*>(input.data()), input.size() * 2)), "upload");
    };
    Check(SUCCEEDED(backend.SetSettings(s)), "settings");
    upload();
    auto draw = [&](bool hdr)
    {
        Check(SUCCEEDED(backend.SetSettings(s)), "live settings");
        std::vector<uint8_t> out;
        Check(SUCCEEDED(backend.RenderOffscreen(64, 64, out, hdr)), "GPU render");
        return out;
    };
    for (bool hdr : {false, true})
    {
        s.tonal = {};
        auto base = draw(hdr);
        for (int zone = 0; zone < 3; zone++)
            for (int kind = 0; kind < 3; kind++)
            {
                s.tonal = {};
                if (kind == 0)
                    s.tonal.exposure[zone] = -.8f;
                else if (kind == 1)
                    s.tonal.contrast[zone] = .5f;
                else
                    s.tonal.saturation[zone] = -.5f;
                // Neutral chroma needs a colored input to verify saturation.
                if (kind == 2)
                {
                    for (size_t i = 4096; i < input.size(); i += 2)
                    {
                        input[i] = 440 << 6;
                        input[i + 1] = 570 << 6;
                    }
                    upload();
                    s.tonal = {};
                    base = draw(hdr);
                    s.tonal.saturation[zone] = -.5f;
                }
                auto graded = draw(hdr);
                auto delta = Delta(base, graded);
                printf("target %s zone %d control %d delta %lld\n", hdr ? "HDR" : "SDR", zone, kind, delta);
                Check(delta > 100, "each control reaches GPU");
                s.tonal = {};
                Check(Delta(base, draw(hdr)) == 0, "zero exactly restores output");
                for (size_t i = 4096; i < input.size(); i++)
                    input[i] = 512 << 6;
                upload();
                base = draw(hdr);
            }
        s.tonal.exposure[2] = -1;
        auto lowered = draw(hdr);
        int darkDelta = 0;
        for (int x = 0; x < 20; x++)
            darkDelta = std::max(darkDelta, abs(int(lowered[x * 4]) - int(base[x * 4])));
        printf("dark pixel rounding delta %d / 255\n", darkDelta);
        Check(darkDelta <= 1, "highlights spare dark pixels within float intermediate rounding");
        Check(lowered[55 * 4] < base[55 * 4], "highlights lower bright pixels");
        s.tonal = {};
    }
    puts("HLG source tests");
    f.color = pl_color_space_hdr10;
    f.color.transfer = PL_COLOR_TRC_HLG;
    upload();
    for (bool hdr : {false, true})
    {
        s.tonal = {};
        auto base = draw(hdr);
        s.tonal.exposure[2] = -1;
        Check(Delta(base, draw(hdr)) > 100, "HLG grading");
        s.tonal = {};
        Check(Delta(base, draw(hdr)) == 0, "HLG neutral identity");
    }
    // Coexist with a source-stage chroma shader and output-stage sharpening.
    puts("Shader combination test");
    f.color = pl_color_space_hdr10;
    upload();
    s.tonal = {};
    s.userShaders.shaders[0].enabled = 1;
    s.userShaders.shaders[4].enabled = 1;
    auto chain = draw(true);
    puts("Combined shaders baseline passed");
    s.tonal.exposure[2] = -1;
    Check(Delta(chain, draw(true)) > 100, "grading with shader chain");
    s.tonal = {};
    Check(Delta(chain, draw(true)) == 0, "shader chain neutral identity");
    s.userShaders = {};
    // SDR bypass and neutral default incur no extra grading or conversion.
    f.color = pl_color_space_bt709;
    f.matrix = PL_COLOR_SYSTEM_BT_709;
    upload();
    s.tonal = {};
    auto sdr = draw(false);
    s.tonal.exposure[0] = 2;
    s.tonal.exposure[2] = -2;
    Check(Delta(sdr, draw(false)) == 0, "SDR unchanged");
    auto bad = s;
    bad.tonal.exposure[1] = std::numeric_limits<float>::quiet_NaN();
    Check(FAILED(backend.SetSettings(bad)), "reject NaN");
    bad = s;
    bad.tonal.saturation[0] = 2;
    Check(FAILED(backend.SetSettings(bad)), "reject out of range");
    printf(
        "PASS HDR tonal grading: 9 controls, SDR/HDR targets, neutral identity, isolated highlights, SDR bypass, persistence, validation\n");
}
