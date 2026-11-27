#include "Renderer/Backend.h"
#include "Renderer/MotionTiming.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

// Copy-back decoding delivers 4K P010 pictures in CPU memory (25 MB each).
// Copying one into the GPU under the render lock took up to 18 ms; presenting
// the previous picture waited for it and missed its refresh (repeated and late
// refreshes). The render lock must stay free while a picture is copied.

static void Require(bool ok, const char* text)
{
    if (!ok)
    {
        std::printf("FAIL %s\n", text);
        std::exit(1);
    }
}

int main()
{
    PlaceboBackend backend;
    RenderSettings settings;
    settings.peakDetect = settings.colorAdjustment = false;
    Require(SUCCEEDED(backend.SetSettings(settings)), "settings");
    VideoFrame frame = {};
    frame.width = 3840;
    frame.height = 2160;
    frame.stride = 3840 * 2;
    frame.format = PixelFormat::P010;
    frame.aspectX = 16;
    frame.aspectY = 9;
    frame.color = pl_color_space_hdr10;
    std::vector<uint8_t> picture(size_t(frame.stride) * frame.height * 3 / 2, 0x40);
    uint64_t id = 0;
    Require(SUCCEEDED(backend.CaptureUpload(frame, picture.data(), picture.size(), id)) && id, "first 4K upload");
    std::atomic<bool> stop = false;
    std::atomic<int> uploads = 0;
    std::thread uploader(
        [&]
        {
            while (!stop)
            {
                uint64_t next = 0;
                Require(SUCCEEDED(backend.CaptureUpload(frame, picture.data(), picture.size(), next)),
                        "4K upload while presenting");
                ++uploads;
            }
        });
    // Stand-in for the presenting thread: a call that takes the render lock.
    double longest = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end)
    {
        const auto start = std::chrono::steady_clock::now();
        backend.SetGeometry(RECT{}, RECT{});
        const double waited =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        longest = std::max(longest, waited);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stop = true;
    uploader.join();
    std::printf("MEASURE %d 4K P010 uploads from CPU memory; longest wait for the render lock %.2f ms\n",
                uploads.load(),
                longest);
    Require(uploads > 10, "uploads kept running");
    Require(longest < 4.0, "a 4K picture from CPU memory is copied without holding the render lock");
    std::puts("PASS 4K pictures from CPU memory leave the render lock free while they are copied");

    // The GPU stalls 25-40 ms while the decoder copies pictures back; Smooth
    // Motion needs four refreshes in advance then. Automatic must reach them.
    Require(backend.PresentAheadCeiling() < PresentLead::Maximum, "without Smooth Motion the device keeps its queue");
    PlaceboBackend motion;
    settings.smoothMotion = true;
    Require(SUCCEEDED(motion.SetSettings(settings)), "Smooth Motion settings");
    Require(SUCCEEDED(motion.CaptureUpload(frame, picture.data(), picture.size(), id)), "upload with Smooth Motion");
    Require(motion.PresentAheadCeiling() == PresentLead::Maximum,
            "Present in Advance: Automatic reaches four refreshes");
    settings.presentAhead = 2;
    PlaceboBackend fixed;
    Require(SUCCEEDED(fixed.SetSettings(settings)) &&
                SUCCEEDED(fixed.CaptureUpload(frame, picture.data(), picture.size(), id)),
            "fixed two refreshes");
    Require(fixed.PresentAheadCeiling() < PresentLead::Maximum, "a fixed lead of two keeps the device's queue");
    std::puts("PASS Present in Advance: Automatic may reach four refreshes with Smooth Motion");
}
