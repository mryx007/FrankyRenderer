#include "Backend.h"
#include "DirectShow/Include/FilterInterfaces.h"
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

using Microsoft::WRL::ComPtr;

static void Require(bool ok, const char* message)
{
    if (!ok) { printf("FAIL %s\n", message); exit(1); }
}

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    PlaceboBackend backend;
    RenderSettings settings;
    settings.autoHdr = settings.peakDetect = settings.colorAdjustment = false;
    settings.ditherMethod = 3;
    Require(SUCCEEDED(backend.SetSettings(settings)), "settings");
    VideoFrame frame;
    frame.width = 96; frame.height = 64; frame.stride = 96;
    frame.aspectX = 3; frame.aspectY = 2;
    frame.color = pl_color_space_bt709;
    // Four distinct luminance quadrants identify orientation and mirror order.
    std::vector<uint8_t> input(96 * 64 * 3 / 2, 128);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 96; ++x)
            input[y * 96 + x] = uint8_t(40 + (x >= 48 ? 45 : 0) + (y >= 32 ? 95 : 0));
    Require(SUCCEEDED(backend.Upload(frame, input.data(), input.size())), "upload");
    auto draw = [&](int degrees, bool mirror) {
        Require(SUCCEEDED(backend.SetRotation(degrees)), "set rotation");
        backend.SetFlip(mirror);
        int w = degrees % 180 ? 64 : 96, h = degrees % 180 ? 96 : 64;
        std::vector<uint8_t> pixels;
        Require(SUCCEEDED(backend.RenderOffscreen(w, h, pixels)), "GPU render");
        return pixels;
    };
    auto baseline = draw(0, false);
    auto validate = [&]() {
        for (int repeat = 0; repeat < 2; ++repeat)
            for (int rotation : {0, 90, 180, 270})
                for (bool mirror : {false, true}) {
                    auto pixels = draw(rotation, mirror);
                    int width = rotation % 180 ? 64 : 96, height = rotation % 180 ? 96 : 64;
                    for (int y : {height / 4, height * 3 / 4})
                        for (int x : {width / 4, width * 3 / 4}) {
                            int sx = x, sy = y;
                            if (rotation == 90) { sx = y; sy = 63 - x; }
                            if (rotation == 180) { sx = 95 - x; sy = 63 - y; }
                            if (rotation == 270) { sx = 95 - y; sy = x; }
                            if (mirror) sx = 95 - sx;
                            for (int channel = 0; channel < 3; ++channel)
                                Require(abs(int(pixels[(y * width + x) * 4 + channel]) -
                                            int(baseline[(sy * 96 + sx) * 4 + channel])) <= 2,
                                        "orientation, mirror order and aspect ratio pixels");
                        }
                }
    };
    validate();
    settings.smoothMotion = true;
    Require(SUCCEEDED(backend.SetSettings(settings)), "smooth motion settings");
    uint64_t first = 0, next = 0;
    Require(SUCCEEDED(backend.CaptureUpload(frame, input.data(), input.size(), first)) &&
            SUCCEEDED(backend.CaptureUpload(frame, input.data(), input.size(), next)), "capture temporal frames");
    Require(SUCCEEDED(backend.SelectMix(first, 0, next, PL_FIELD_NONE, PL_FIELD_NONE, PL_FIELD_NONE, .5f, 1.f)),
            "select temporal mix");
    validate();
    Require(backend.SetRotation(45) == E_INVALIDARG && backend.SetRotation(-90) == E_INVALIDARG &&
            backend.SetRotation(360) == E_INVALIDARG && backend.Rotation() == 270,
            "invalid angles preserve state");
    Require(draw(0, false) == baseline, "reset and repeated renders do not accumulate transforms");
    puts("PASS GPU pixels: four rotations, both mirror states, non-square aspect, repeat/reset, invalid angles, smooth motion");

    // Exercise the actual renderer COM interface without registration or playback.
    Require(argc == 2, "renderer path argument");
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    HMODULE module = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    Require(module != nullptr, "load renderer");
    using Getter = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto getter = reinterpret_cast<Getter>(GetProcAddress(module, "DllGetClassObject"));
    Require(getter != nullptr, "class factory export");
    const CLSID clsid = {0xb864e92a, 0x1bdf, 0x4da1, {0x90, 0x04, 0xcb, 0xf4, 0x67, 0xdf, 0x23, 0x9a}};
    ComPtr<IClassFactory> factory;
    Require(SUCCEEDED(getter(clsid, IID_PPV_ARGS(&factory))), "factory");
    const CLSID mpcvr = {0x71f080aa, 0x8661, 0x4093, {0xb1, 0x5e, 0x4f, 0x69, 0x03, 0xe7, 0x7d, 0x0a}};
    ComPtr<IClassFactory> compatibility;
    Require(getter(mpcvr, IID_PPV_ARGS(&compatibility)) == CLASS_E_CLASSNOTAVAILABLE && !compatibility,
            "native preview does not expose the MPCVR compatibility class");
    ComPtr<IExFilterConfig> config;
    Require(SUCCEEDED(factory->CreateInstance(nullptr, IID_PPV_ARGS(&config))), "configuration interface");
    for (int degrees : {0, 90, 180, 270}) {
        int readback = -1;
        Require(SUCCEEDED(config->Flt_SetInt("rotation", degrees)) &&
                SUCCEEDED(config->Flt_GetInt("rotation", &readback)) && readback == degrees, "COM rotation readback");
    }
    for (bool mirror : {true, false}) {
        bool readback = !mirror;
        Require(SUCCEEDED(config->Flt_SetBool("flip", mirror)) &&
                SUCCEEDED(config->Flt_GetBool("flip", &readback)) && readback == mirror, "COM mirror readback");
    }
    Require(config->Flt_SetInt("rotation", 45) == E_INVALIDARG &&
            config->Flt_SetInt(nullptr, 90) == E_POINTER, "COM invalid inputs");
    puts("PASS renderer COM: rotation and mirror setters/readback, invalid inputs");
}
