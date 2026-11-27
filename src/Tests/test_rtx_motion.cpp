// RTX Video Super Resolution with Smooth Motion on prepared HDR pictures
// (planar 10-bit HDR10, which goes to the driver as P010 from a texture pool):
// every blend may show only the two pictures it mixes, never an older one.
// Picture i carries a bright, strongly coloured marker at its own place. The
// output of every refresh is copied without waiting for the GPU and read only
// at the end, so reading back does not serialise the GPU work between
// pictures (that would hide a picture being overwritten while in use).
// Run through Tests/run_tests.py --rtx-motion (it uses test settings).
#include "Renderer/Backend.h"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace
{
constexpr int W = 640, H = 360, Markers = 12, Size = 40, Pictures = 60;
constexpr float Positions[] = {.2f, .4f, .6f, .8f};

void Check(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

int MarkerX(int m)
{
    return 40 + (m % 4) * 150;
}

int MarkerY(int m)
{
    return 40 + (m / 4) * 110;
}

bool InMarker(int m, int x, int y)
{
    return x >= MarkerX(m) && x < MarkerX(m) + Size && y >= MarkerY(m) && y < MarkerY(m) + Size;
}

// YUV420P10 HDR10 (BT.2020 limited): dark grey background, the picture's
// marker bright and strongly coloured (its chroma differs from the background).
std::vector<uint8_t> Picture(int picture)
{
    const int m = picture % Markers;
    std::vector<uint16_t> data(size_t(W) * H * 3 / 2, uint16_t(512));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            data[size_t(y) * W + x] = InMarker(m, x, y) ? 64 + 600 : 64 + 120;
    uint16_t* u = data.data() + size_t(W) * H;
    uint16_t* v = u + size_t(W / 2) * (H / 2);
    for (int y = 0; y < H / 2; y++)
        for (int x = 0; x < W / 2; x++)
            if (InMarker(m, x * 2, y * 2))
            {
                u[size_t(y) * (W / 2) + x] = 512 + (m % 2 ? 300 : -250);
                v[size_t(y) * (W / 2) + x] = 512 + (m % 2 ? -250 : 300);
            }
    std::vector<uint8_t> bytes(data.size() * 2);
    memcpy(bytes.data(), data.data(), bytes.size());
    return bytes;
}

struct Shot
{
    ComPtr<ID3D11Texture2D> staging;
    int previous, next;
    float position;
};
} // namespace

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrankyRtxMotionTest";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(
        0, wc.lpszClassName, L"RTX motion", WS_POPUP, 0, 0, W * 2, H * 2, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "hidden test window");
    PlaceboBackend b;
    RenderSettings s;
    s.outputFormat = 2;
    s.ditherMethod = 3;
    s.peakDetect = false;
    s.smoothMotion = true;
    s.rtxSuperResolution = true;
    Check(SUCCEEDED(b.SetSettings(s)), "settings");
    Check(SUCCEEDED(b.SetWindow(window)), "window");
    std::vector<Shot> shots;
    ComPtr<ID3D11DeviceContext> context;
    Shot pending = {};
    b.SetPresentProbeForTest(
        [&](ID3D11Texture2D* texture)
        {
            ComPtr<ID3D11Device> device;
            texture->GetDevice(&device);
            device->GetImmediateContext(&context);
            D3D11_TEXTURE2D_DESC d;
            texture->GetDesc(&d);
            d.Usage = D3D11_USAGE_STAGING;
            d.BindFlags = 0;
            d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            d.MiscFlags = 0;
            Check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &pending.staging)), "capture texture");
            context->CopyResource(pending.staging.Get(), texture); // no Map here: no wait
            shots.push_back(pending);
        });
    VideoFrame f;
    f.width = W;
    f.height = H;
    f.stride = W * 2;
    f.format = PixelFormat::YUV420P10;
    f.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
    f.levels = PL_COLOR_LEVELS_LIMITED;
    f.color = pl_color_space_hdr10;
    f.color.hdr.max_luma = 1000;

    std::vector<uint64_t> ids(Pictures);
    auto first = Picture(0);
    Check(SUCCEEDED(b.CaptureUpload(f, first.data(), first.size(), ids[0])), "first picture");
    for (int i = 1; i < Pictures; i++)
    {
        auto picture = Picture(i);
        Check(SUCCEEDED(b.CaptureUpload(f, picture.data(), picture.size(), ids[i])), "picture");
        for (float position : Positions)
        {
            Check(SUCCEEDED(b.SelectMix(ids[i - 1],
                                        i >= 2 ? ids[i - 2] : 0,
                                        ids[i],
                                        PL_FIELD_NONE,
                                        PL_FIELD_NONE,
                                        PL_FIELD_NONE,
                                        position,
                                        1.f)),
                  "select mix");
            // A timed present during playback: Smooth Motion blends (a still
            // picture would show only the current one).
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            b.SetPresentationTiming(now.QuadPart, true);
            pending = {nullptr, i - 1, i, position};
            Check(SUCCEEDED(b.Present()), "present");
        }
    }
    const uint64_t rtx = b.RtxFrames();
    const std::wstring stats = b.StatisticsSnapshot();
    // Read every refresh now.
    int ghosts = 0, missing = 0;
    float background[3] = {}, strongest = 0;
    for (size_t k = 0; k < shots.size(); k++)
    {
        auto& shot = shots[k];
        D3D11_TEXTURE2D_DESC d;
        shot.staging->GetDesc(&d);
        D3D11_MAPPED_SUBRESOURCE map = {};
        Check(SUCCEEDED(context->Map(shot.staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "capture pixels");
        auto mean = [&](int sx, int sy, float out[3])
        {
            const int cx = sx * int(d.Width) / W, cy = sy * int(d.Height) / H;
            double sum[3] = {};
            for (int y = cy - 6; y < cy + 6; y++)
                for (int x = cx - 6; x < cx + 6; x++)
                {
                    const uint32_t p = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map.pData) +
                                                                         size_t(y) * map.RowPitch)[x];
                    for (int c = 0; c < 3; c++)
                        sum[c] += ((p >> (10 * c)) & 1023) / 1023.;
                }
            for (int c = 0; c < 3; c++)
                out[c] = float(sum[c] / 144);
        };
        // background: a place no marker ever covers
        mean(W / 2, H - 12, background);
        for (int m = 0; m < Markers; m++)
        {
            float v[3];
            mean(MarkerX(m) + Size / 2, MarkerY(m) + Size / 2, v);
            float deviation = 0;
            for (int c = 0; c < 3; c++)
                deviation = std::max(deviation, std::abs(v[c] - background[c]));
            const bool mixed = m == shot.previous % Markers || m == shot.next % Markers;
            if (mixed)
                strongest = std::max(strongest, deviation);
            if (!mixed && deviation > .03f)
            {
                if (ghosts < 5)
                    printf("  ghost: blend %d->%d at %.1f shows marker %d (deviation %.3f)\n",
                           shot.previous,
                           shot.next,
                           shot.position,
                           m,
                           deviation);
                ++ghosts;
            }
            if (mixed && deviation < .1f)
                ++missing;
        }
        context->Unmap(shot.staging.Get(), 0);
    }
    printf("%zu refreshes read after the run, RTX pictures %llu, strongest marker %.3f, old markers %d, missing %d\n",
           shots.size(),
           (unsigned long long)rtx,
           strongest,
           ghosts,
           missing);
    b.SetWindow(nullptr);
    DestroyWindow(window);
    if (!rtx)
    {
        puts("SKIP RTX Video not active on this GPU");
        return 0;
    }
    // A refresh can occasionally be skipped (no picture handed over); nearly all must be there.
    Check(shots.size() + 3 >= size_t((Pictures - 1) * std::size(Positions)), "refreshes captured");
    Check(ghosts == 0, "no older picture shows in a Smooth Motion blend");
    Check(missing == 0, "every blend shows both mixed pictures");
    puts("PASS RTX with Smooth Motion on prepared HDR pictures: no older pictures in any blend");
    return 0;
}
