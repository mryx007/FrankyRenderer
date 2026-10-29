#define NOMINMAX
#include "Renderer/Backend.h"
#include "DolbyVision/Runtime.h"
#include <atomic>
#include <thread>
#include <map>

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    try
    {
        DvGpu conversion(nullptr);
        PlaceboBackend backend;
        check(SUCCEEDED(backend.SetDevice(conversion.d3d->device)), "shared decoder device");
        RenderSettings settings;
        settings.peakDetect = settings.colorAdjustment = settings.autoHdr = false;
        settings.ditherMethod = 3;
        check(SUCCEEDED(backend.SetSettings(settings)), "settings");
        VideoFrame frame;
        frame.width = frame.height = 64;
        frame.stride = 64;
        frame.format = PixelFormat::NV12;
        frame.color = pl_color_space_bt709;
        std::vector<uint8_t> video(64 * 64 * 3 / 2, 128);
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                video[y * 64 + x] = uint8_t(40 + (x >= 32 ? 45 : 0) + (y >= 32 ? 95 : 0));
        check(SUCCEEDED(backend.Upload(frame, video.data(), video.size())), "static test video");
        std::map<int, std::vector<uint8_t>> reference;
        for (int size : {64, 72, 80, 88})
            check(SUCCEEDED(backend.RenderOffscreen(size, size, reference[size])), "baseline resized image");
        std::vector<uint16_t> data(64 * 64 * 3 / 2, 512 << 6);
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = 64;
        desc.ArraySize = desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_P010;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init = {data.data(), 128, 0};
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        check(SUCCEEDED(conversion.d3d->device->CreateTexture2D(&desc, &init, &texture)), "conversion input");
        AVFrame input = {};
        input.format = AV_PIX_FMT_D3D11;
        input.width = input.height = 64;
        input.data[0] = reinterpret_cast<uint8_t*>(texture.Get());
        std::atomic<bool> stop = false, failed = false;
        std::atomic<unsigned> work = 0;
        std::thread worker(
            [&]
            {
                try
                {
                    while (!stop)
                    {
                        Microsoft::WRL::ComPtr<ID3D11Texture2D> planes[3];
                        UploadDvPlanes(&input, conversion.splitShader.Get(), planes);
                        pl_gpu_finish(conversion.d3d->gpu);
                        ++work;
                    }
                }
                catch (...)
                {
                    failed = true;
                }
            });
        unsigned corrupt = 0;
        for (unsigned i = 0; i < 600; ++i)
        {
            int size = 64 + 8 * (i % 4);
            std::vector<uint8_t> pixels;
            if (FAILED(backend.RenderOffscreen(size, size, pixels)) || pixels != reference[size])
                ++corrupt;
        }
        stop = true;
        worker.join();
        printf("Concurrent DV conversion/redraw: conversions=%u corrupt=%u worker_failure=%d\n",
               work.load(),
               corrupt,
               failed.load());
        check(work > 0 && !failed && corrupt == 0, "concurrent resize/redraw pixel integrity");
        puts("PASS concurrent DV conversion and resized redraw preserve actual pixels");
        return 0;
    }
    catch (const std::exception& e)
    {
        fprintf(stderr, "FAIL %s\n", e.what());
        return 1;
    }
}
