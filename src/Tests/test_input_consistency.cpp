// The same picture through every input path must look the same. The
// reference is RGB input (GBRP16): it needs no YUV matrix and no range
// expansion. Every other path renders the same grey steps and colours and is
// compared with it on the 10-bit output:
//   - every pixel format the renderer accepts (CPU upload), 8 to 16 bit,
//     4:2:0 / 4:2:2 / 4:4:4, semi-planar, planar and packed
//   - limited and full range, BT.601 / BT.709 / BT.2020 matrices
//   - the hardware decoder path (D3D11 NV12 and P010 textures)
//   - Smooth Motion (a blend of two identical frames)
//   - deinterlacing of a picture whose two fields are the same
//   - RTX Video Super Resolution against the renderer's own upscaling
//   - with Windows HDR on: native HDR10 passthrough against libplacebo's HDR
//     output with a peak high enough not to roll off
// Run through Tests/run_tests.py --input-consistency (it uses test settings).
#include "Renderer/Backend.h"
#include "Renderer/PixelLayout.h"
#include <DirectXPackedVector.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;

namespace
{
constexpr int PatchWidth = 16, Height = 32;

struct Patch
{
    const char* name;
    double r, g, b; // R'G'B', encoded in the source transfer
    bool grey;
};

const Patch Patches[] = {{"black", 0, 0, 0, true},
                         {"grey 10%", .1, .1, .1, true},
                         {"grey 25%", .25, .25, .25, true},
                         {"grey 50%", .5, .5, .5, true},
                         {"grey 75%", .75, .75, .75, true},
                         {"white", 1, 1, 1, true},
                         {"red", .75, .05, .05, false},
                         {"green", .05, .75, .05, false},
                         {"blue", .05, .05, .75, false},
                         {"yellow", .8, .8, .1, false},
                         {"skin", .8, .6, .5, false}};
constexpr int PatchCount = int(std::size(Patches)), Width = PatchCount * PatchWidth;

struct Source
{
    PixelFormat format = PixelFormat::GBRP16;
    pl_color_system matrix = PL_COLOR_SYSTEM_BT_709;
    pl_color_levels levels = PL_COLOR_LEVELS_LIMITED;
    bool hdr = false;
};

struct Coefficients
{
    double kr, kb;
};

Coefficients Matrix(pl_color_system m)
{
    switch (m)
    {
    case PL_COLOR_SYSTEM_BT_601:
        return {.299, .114};
    case PL_COLOR_SYSTEM_BT_2020_NC:
        return {.2627, .0593};
    default:
        return {.2126, .0722};
    }
}

// Y'CbCr codes of a patch at a bit depth.
void Encode(const Patch& p, const Source& s, int depth, uint32_t out[3])
{
    const auto k = Matrix(s.matrix);
    const double kg = 1 - k.kr - k.kb;
    const double y = k.kr * p.r + kg * p.g + k.kb * p.b;
    const double cb = (p.b - y) / (2 * (1 - k.kb)), cr = (p.r - y) / (2 * (1 - k.kr));
    const double scale = std::ldexp(1.0, depth - 8), max = std::ldexp(1.0, depth) - 1;
    if (s.levels == PL_COLOR_LEVELS_FULL)
    {
        out[0] = uint32_t(std::lround(y * max));
        out[1] = uint32_t(std::clamp(std::lround(std::ldexp(1.0, depth - 1) + cb * max), 0L, long(max)));
        out[2] = uint32_t(std::clamp(std::lround(std::ldexp(1.0, depth - 1) + cr * max), 0L, long(max)));
    }
    else
    {
        out[0] = uint32_t(std::lround((16 + 219 * y) * scale));
        out[1] = uint32_t(std::lround((128 + 224 * cb) * scale));
        out[2] = uint32_t(std::lround((128 + 224 * cr) * scale));
    }
}

uint32_t RgbCode(double v, int depth)
{
    return uint32_t(std::lround(std::clamp(v, 0.0, 1.0) * (std::ldexp(1.0, depth) - 1)));
}

void Put(std::vector<uint8_t>& data, size_t at, int bytes, uint32_t value)
{
    if (bytes == 1)
        data[at] = uint8_t(value);
    else
    {
        data[at] = uint8_t(value);
        data[at + 1] = uint8_t(value >> 8);
    }
}

// The picture in a pixel format, laid out as the renderer reads it.
std::vector<uint8_t> Picture(const Source& s, int& stride)
{
    const auto l = Layout(s.format);
    stride = MinimumStride(s.format, Width);
    std::vector<uint8_t> data(ImageBytes(s.format, stride, Height));
    const int depth = l.depth, bytes = l.bytes, shift = l.shift;
    const int cw = Width >> l.chromaX, ch = Height >> l.chromaY;
    auto patch = [&](int x) -> const Patch& { return Patches[std::min(x / PatchWidth, PatchCount - 1)]; };
    switch (l.packing)
    {
    case PixelPacking::SemiPlanar:
        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; ++x)
            {
                uint32_t c[3];
                Encode(patch(x), s, depth, c);
                Put(data, size_t(y) * stride + size_t(x) * bytes, bytes, c[0] << shift);
            }
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
            {
                uint32_t c[3];
                Encode(patch(x << l.chromaX), s, depth, c);
                const size_t at = size_t(stride) * Height + size_t(y) * stride + size_t(x) * 2 * bytes;
                Put(data, at, bytes, c[1] << shift);
                Put(data, at + bytes, bytes, c[2] << shift);
            }
        break;
    case PixelPacking::Planar:
    {
        const int cstride = stride >> l.chromaX;
        const size_t p1 = size_t(stride) * Height, p2 = p1 + size_t(cstride) * ch;
        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; ++x)
            {
                const auto& p = patch(x);
                if (l.rgb) // G, B, R planes
                {
                    Put(data, size_t(y) * stride + size_t(x) * bytes, bytes, RgbCode(p.g, depth));
                    Put(data, p1 + size_t(y) * cstride + size_t(x) * bytes, bytes, RgbCode(p.b, depth));
                    Put(data, p2 + size_t(y) * cstride + size_t(x) * bytes, bytes, RgbCode(p.r, depth));
                    continue;
                }
                uint32_t c[3];
                Encode(p, s, depth, c);
                Put(data, size_t(y) * stride + size_t(x) * bytes, bytes, c[0]);
            }
        if (!l.rgb)
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x)
                {
                    uint32_t c[3];
                    Encode(patch(x << l.chromaX), s, depth, c);
                    Put(data, (l.vu ? p2 : p1) + size_t(y) * cstride + size_t(x) * bytes, bytes, c[1]);
                    Put(data, (l.vu ? p1 : p2) + size_t(y) * cstride + size_t(x) * bytes, bytes, c[2]);
                }
        break;
    }
    case PixelPacking::Packed422:
        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; x += 2)
            {
                uint32_t c[3];
                Encode(patch(x), s, depth, c);
                const size_t at = size_t(y) * stride + size_t(x) * 2 * bytes;
                const uint32_t order[4] = {
                    l.vu ? c[1] : c[0], l.vu ? c[0] : c[1], l.vu ? c[2] : c[0], l.vu ? c[0] : c[2]};
                for (int i = 0; i < 4; ++i)
                    Put(data, at + size_t(i) * bytes, bytes, order[i] << shift);
            }
        break;
    case PixelPacking::PackedRGB:
        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; ++x)
            {
                const auto& p = patch(x);
                const uint32_t r = RgbCode(p.r, depth), g = RgbCode(p.g, depth), b = RgbCode(p.b, depth);
                const uint32_t order[4] = {l.vu ? b : r, g, l.vu ? r : b, uint32_t(std::ldexp(1.0, depth) - 1)};
                const size_t at = size_t(y) * stride + size_t(x) * l.channels * bytes;
                for (int i = 0; i < l.channels; ++i)
                    Put(data, at + size_t(i) * bytes, bytes, order[i]);
            }
        break;
    case PixelPacking::Packed444:
        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; ++x)
            {
                uint32_t c[3];
                Encode(patch(x), s, depth, c);
                const size_t at = size_t(y) * stride + size_t(x) * (s.format == PixelFormat::Y410 ? 4 : 8);
                if (s.format == PixelFormat::Y410)
                {
                    const uint32_t word = c[1] | (c[0] << 10) | (c[2] << 20) | (3u << 30);
                    std::memcpy(&data[at], &word, 4);
                }
                else
                {
                    const uint32_t order[4] = {c[1], c[0], c[2], 65535};
                    for (int i = 0; i < 4; ++i)
                        Put(data, at + size_t(i) * 2, 2, order[i]);
                }
            }
        break;
    case PixelPacking::Gray:
        for (int y = 0; y < Height; ++y)
            for (int x = 0; x < Width; ++x)
            {
                uint32_t c[3];
                Encode(patch(x), s, depth, c);
                Put(data, size_t(y) * stride + size_t(x) * bytes, bytes, c[0]);
            }
        break;
    }
    return data;
}

VideoFrame Frame(const Source& s, int stride)
{
    VideoFrame f;
    f.width = Width;
    f.height = Height;
    f.stride = stride;
    f.format = s.format;
    f.matrix = s.matrix;
    f.levels = s.levels;
    if (s.hdr)
    {
        f.color = pl_color_space_hdr10;
        f.color.hdr.max_luma = 1000;
        f.color.hdr.min_luma = .005f;
    }
    else
        f.color = pl_color_space_bt709;
    return f;
}

using Result = std::array<std::array<double, 3>, PatchCount>;

HWND g_window = nullptr;
bool g_hdrOn = false;

RenderSettings BaseSettings()
{
    RenderSettings s;
    s.outputFormat = 2;
    s.autoHdr = false;
    s.peakDetect = false;
    s.colorAdjustment = false;
    s.ditherMethod = 3;
    s.hdrPassthrough = true;
    s.hdrMode = 2;
    return s;
}

// Reads the 10-bit output: the middle of every patch, scaled to the window.
bool Read(PlaceboBackend& b, Result& out, bool& pq, std::function<HRESULT()> present)
{
    pl_color_space target = {};
    b.SetPresentColorProbeForTest([&](const pl_color_space& color) { target = color; });
    bool ok = false;
    b.SetPresentProbeForTest(
        [&](ID3D11Texture2D* texture)
        {
            ComPtr<ID3D11Device> device;
            texture->GetDevice(&device);
            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            D3D11_TEXTURE2D_DESC d;
            texture->GetDesc(&d);
            if (d.Format != DXGI_FORMAT_R10G10B10A2_UNORM)
                return;
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
            const double patch = double(d.Width) / PatchCount;
            for (int p = 0; p < PatchCount; ++p)
            {
                double sum[3] = {};
                int n = 0;
                for (UINT row = d.Height / 4; row < d.Height * 3 / 4; ++row)
                {
                    const auto* line =
                        reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(map.pData) + map.RowPitch * row);
                    for (int x = int(p * patch + patch / 4); x < int((p + 1) * patch - patch / 4); ++x)
                    {
                        for (int i = 0; i < 3; ++i)
                            sum[i] += ((line[x] >> (10 * i)) & 1023) / 1023.0;
                        ++n;
                    }
                }
                for (int i = 0; i < 3; ++i)
                    out[p][i] = n ? sum[i] / n : 0;
            }
            context->Unmap(staging.Get(), 0);
            ok = true;
        });
    if (FAILED(present()) || !ok)
        return false;
    pq = target.transfer == PL_COLOR_TRC_PQ;
    return true;
}

// Plain upload of a source with the given settings.
bool Render(const Source& s, const RenderSettings& settings, Result& out)
{
    PlaceboBackend b;
    bool pq = false;
    int stride = 0;
    const auto data = Picture(s, stride);
    if (FAILED(b.SetSettings(settings)) || FAILED(b.SetWindow(g_window)) ||
        FAILED(b.Upload(Frame(s, stride), data.data(), data.size())))
        return false;
    return Read(b, out, pq, [&] { return b.Present(); });
}

int g_failures = 0, g_skipped = 0;

// Signal difference on the 10-bit output; greys only for a grey-only source.
void Compare(const std::string& name,
             const Result& reference,
             const Result& tested,
             bool greysOnly = false,
             double tolerance = .008)
{
    double worst = 0;
    int at = 0, channel = 0;
    for (int p = 0; p < PatchCount; ++p)
    {
        if (greysOnly && !Patches[p].grey)
            continue;
        for (int i = 0; i < 3; ++i)
            if (const double d = std::abs(tested[p][i] - reference[p][i]); d > worst)
                worst = d, at = p, channel = i;
    }
    const bool failed = worst > tolerance;
    printf("%s %-46s max diff %.4f", failed ? "FAIL" : "PASS", name.c_str(), worst);
    if (failed)
        printf(" (%s %c: %.4f instead of %.4f)",
               Patches[at].name,
               "RGB"[channel],
               tested[at][channel],
               reference[at][channel]);
    puts("");
    g_failures += failed;
}

void Skip(const std::string& name, const char* why)
{
    printf("SKIP %-46s %s\n", name.c_str(), why);
    ++g_skipped;
}

// A D3D11 decoder-style texture (NV12 or P010) with the same picture.
bool RenderImported(const Source& s, const RenderSettings& settings, Result& out)
{
    PlaceboBackend b;
    if (FAILED(b.SetSettings(settings)) || FAILED(b.SetWindow(g_window)))
        return false;
    ID3D11Device* device = b.EnsureDevice();
    if (!device)
        return false;
    int stride = 0;
    const auto data = Picture(s, stride);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = Width;
    desc.Height = Height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = s.format == PixelFormat::P010 ? DXGI_FORMAT_P010 : DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {data.data(), UINT(stride), 0};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&desc, &init, &texture)))
        return false;
    bool pq = false;
    if (FAILED(b.Import(Frame(s, stride), texture.Get(), 0)))
        return false;
    return Read(b, out, pq, [&] { return b.Present(); });
}

// Smooth Motion: a blend halfway between two identical frames.
bool RenderMixed(const Source& s, const RenderSettings& settings, Result& out)
{
    PlaceboBackend b;
    auto motion = settings;
    motion.smoothMotion = true;
    if (FAILED(b.SetSettings(motion)) || FAILED(b.SetWindow(g_window)))
        return false;
    int stride = 0;
    const auto data = Picture(s, stride);
    uint64_t first = 0, second = 0;
    if (FAILED(b.CaptureUpload(Frame(s, stride), data.data(), data.size(), first)) ||
        FAILED(b.CaptureUpload(Frame(s, stride), data.data(), data.size(), second)) ||
        FAILED(b.SelectMix(first, 0, second, PL_FIELD_NONE, PL_FIELD_NONE, PL_FIELD_NONE, .5f, 1.f)))
        return false;
    bool pq = false;
    return Read(b, out, pq, [&] { return b.Present(); });
}

// Deinterlacing of a frame whose fields are identical.
bool RenderFields(const Source& s, const RenderSettings& settings, int algorithm, Result& out)
{
    PlaceboBackend b;
    auto fields = settings;
    fields.deinterlacing = algorithm;
    if (FAILED(b.SetSettings(fields)) || FAILED(b.SetWindow(g_window)))
        return false;
    b.SetSourceInterlaced(true);
    int stride = 0;
    const auto data = Picture(s, stride);
    uint64_t previous = 0, current = 0, next = 0;
    const auto frame = Frame(s, stride);
    if (FAILED(b.CaptureUpload(frame, data.data(), data.size(), previous)) ||
        FAILED(b.CaptureUpload(frame, data.data(), data.size(), current)) ||
        FAILED(b.CaptureUpload(frame, data.data(), data.size(), next)) ||
        FAILED(b.SelectField(current, previous, next, PL_FIELD_TOP, PL_FIELD_TOP)))
        return false;
    bool pq = false;
    return Read(b, out, pq, [&] { return b.Present(); });
}
} // namespace

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"FrankyInputConsistencyTest";
    RegisterClassW(&wc);
    g_window = CreateWindowExW(0,
                               wc.lpszClassName,
                               L"Input consistency",
                               WS_POPUP,
                               0,
                               0,
                               Width,
                               Height,
                               nullptr,
                               nullptr,
                               wc.hInstance,
                               nullptr);
    if (!g_window)
        return 1;
    {
        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIOutput6> output6;
        DXGI_OUTPUT_DESC1 desc = {};
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0, &adapter)))
            for (UINT i = 0; SUCCEEDED(adapter->EnumOutputs(i, &output)); ++i, output.Reset())
                if (SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc)) &&
                    desc.Monitor == MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST))
                    g_hdrOn = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    }
    printf("Windows HDR %s\n", g_hdrOn ? "on" : "off");

    const auto settings = BaseSettings();
    Result reference;
    Source rgb;
    if (!Render(rgb, settings, reference))
    {
        puts("FAIL reference (GBRP16) could not render");
        return 1;
    }

    // The reference itself against the expected values: SDR BT.709 video is
    // shown with the display's own gamma, so without SDR Gamma Processing the
    // signal passes through unchanged, colours included.
    {
        Result expected;
        for (int p = 0; p < PatchCount; ++p)
            expected[p] = {Patches[p].r, Patches[p].g, Patches[p].b};
        Compare("RGB reference vs expected values", expected, reference);
    }

    // Every pixel format, BT.709 limited range (RGB formats are full range).
    const PixelFormat formats[] = {
        PixelFormat::NV12,      PixelFormat::P010,      PixelFormat::P012,      PixelFormat::P016,
        PixelFormat::NV16,      PixelFormat::P210,      PixelFormat::P216,      PixelFormat::YV12,
        PixelFormat::I420,      PixelFormat::YV16,      PixelFormat::Y42B,      PixelFormat::YV24,
        PixelFormat::YUV444P8,  PixelFormat::YUV420P10, PixelFormat::YUV420P16, PixelFormat::YUV422P10,
        PixelFormat::YUV422P16, PixelFormat::YUV444P10, PixelFormat::YUV444P16, PixelFormat::GBRP8,
        PixelFormat::GBRP10,    PixelFormat::YUY2,      PixelFormat::UYVY,      PixelFormat::Y210,
        PixelFormat::Y216,      PixelFormat::BGR24,     PixelFormat::BGRX32,    PixelFormat::BGRA32,
        PixelFormat::RGB48,     PixelFormat::BGR48,     PixelFormat::BGRA64,    PixelFormat::Y410,
        PixelFormat::Y416,      PixelFormat::GRAY8,     PixelFormat::GRAY10,    PixelFormat::GRAY16};
    for (auto format : formats)
    {
        Source s;
        s.format = format;
        Result r;
        const std::string name = std::string("format ") + Layout(format).name;
        if (!Render(s, settings, r))
        {
            printf("FAIL %-46s could not render\n", name.c_str());
            ++g_failures;
            continue;
        }
        // 8-bit sources round to 1/219 of the range.
        Compare(
            name, reference, r, Layout(format).packing == PixelPacking::Gray, Layout(format).depth == 8 ? .012 : .008);
    }

    // Range and matrix, on a 10-bit 4:4:4 source.
    for (auto levels : {PL_COLOR_LEVELS_LIMITED, PL_COLOR_LEVELS_FULL})
        for (auto matrix : {PL_COLOR_SYSTEM_BT_601, PL_COLOR_SYSTEM_BT_709, PL_COLOR_SYSTEM_BT_2020_NC})
        {
            Source s;
            s.format = PixelFormat::YUV444P16;
            s.levels = levels;
            s.matrix = matrix;
            Result r;
            const std::string name = std::string(levels == PL_COLOR_LEVELS_FULL ? "full" : "limited") + " range, " +
                                     (matrix == PL_COLOR_SYSTEM_BT_601   ? "BT.601"
                                      : matrix == PL_COLOR_SYSTEM_BT_709 ? "BT.709"
                                                                         : "BT.2020") +
                                     " matrix";
            if (!Render(s, settings, r))
            {
                printf("FAIL %-46s could not render\n", name.c_str());
                ++g_failures;
                continue;
            }
            Compare(name, reference, r);
        }

    // The hardware decoder path against the same picture uploaded.
    for (auto format : {PixelFormat::NV12, PixelFormat::P010})
    {
        Source s;
        s.format = format;
        Result uploaded, imported;
        const std::string name = std::string("D3D11 texture ") + Layout(format).name;
        if (!Render(s, settings, uploaded) || !RenderImported(s, settings, imported))
        {
            Skip(name, "texture not available");
            continue;
        }
        Compare(name + " vs upload", uploaded, imported);
    }

    // Smooth Motion and deinterlacing must not change a still picture.
    {
        Source s;
        s.format = PixelFormat::P010;
        Result plain, mixed;
        Render(s, settings, plain);
        if (RenderMixed(s, settings, mixed))
            Compare("Smooth Motion blend of identical frames", plain, mixed);
        else
            Skip("Smooth Motion blend of identical frames", "could not render");
        for (int algorithm : {1, 2})
        {
            Result fields;
            const std::string name =
                algorithm == 1 ? "deinterlacing Bwdif, equal fields" : "deinterlacing Yadif, equal fields";
            if (RenderFields(s, settings, algorithm, fields))
                Compare(name, plain, fields);
            else
                Skip(name, "could not render");
        }
    }

    // RTX Video Super Resolution against the renderer's own upscaling (2x).
    {
        SetWindowPos(g_window, nullptr, 0, 0, Width * 2, Height * 2, SWP_NOZORDER | SWP_NOACTIVATE);
        Source s;
        s.format = PixelFormat::NV12;
        auto rtx = settings;
        rtx.rtxSuperResolution = true;
        PlaceboBackend probe;
        Result own, upscaled;
        bool used = false;
        if (Render(s, settings, own) && RenderImported(s, rtx, upscaled))
        {
            PlaceboBackend check;
            check.SetSettings(rtx);
            check.SetWindow(g_window);
            int stride = 0;
            const auto data = Picture(s, stride);
            check.Upload(Frame(s, stride), data.data(), data.size());
            check.Present();
            used = check.RtxFrames() > 0;
        }
        if (used)
            Compare("RTX Super Resolution vs own upscaling", own, upscaled, false, .02);
        else
            Skip("RTX Super Resolution vs own upscaling", "RTX Video not active on this GPU");
        SetWindowPos(g_window, nullptr, 0, 0, Width, Height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // HDR10: the native passthrough against libplacebo's HDR output without roll-off.
    if (g_hdrOn)
    {
        Source s;
        s.format = PixelFormat::P010;
        s.matrix = PL_COLOR_SYSTEM_BT_2020_NC;
        s.hdr = true;
        auto native = settings, placebo = settings;
        native.hdrMode = 1;
        placebo.hdrMode = 3;
        placebo.hdrNits = 10000;
        Result a, b;
        if (Render(s, native, a) && Render(s, placebo, b))
            Compare("HDR10 native passthrough vs libplacebo HDR", a, b);
        else
            Skip("HDR10 native passthrough vs libplacebo HDR", "could not render");
    }
    else
        Skip("HDR10 native passthrough vs libplacebo HDR", "needs Windows HDR on");

    DestroyWindow(g_window);
    printf("%s input consistency: %d difference(s), %d case(s) skipped\n",
           g_failures ? "FAIL" : "PASS",
           g_failures,
           g_skipped);
    return g_failures ? 1 : 0;
}
