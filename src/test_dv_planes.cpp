#define NOMINMAX
#include "DolbyVision/Runtime.h"
using Microsoft::WRL::ComPtr;

int main()
{
    try
    {
        DvGpu gpu(nullptr);
        ComPtr<ID3D11DeviceContext> context;
        gpu.d3d->device->GetImmediateContext(&context);
        for (UINT flags : {UINT(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DECODER), UINT(D3D11_BIND_DECODER)})
        {
            constexpr UINT width = 128, allocatedHeight = 80, height = 64, slice = 2;
            std::vector<uint16_t> bytes[3];
            D3D11_SUBRESOURCE_DATA initial[3] = {};
            for (UINT layer = 0; layer < 3; ++layer)
            {
                bytes[layer].resize(width * allocatedHeight * 3 / 2);
                for (UINT y = 0; y < allocatedHeight; ++y)
                    for (UINT x = 0; x < width; ++x)
                        bytes[layer][y * width + x] = uint16_t(((x + 2 * y + 17 * layer) % 1024) << 6);
                for (UINT y = 0; y < allocatedHeight / 2; ++y)
                    for (UINT x = 0; x < width; x += 2)
                    {
                        bytes[layer][width * allocatedHeight + y * width + x] = uint16_t((123 + layer) << 6);
                        bytes[layer][width * allocatedHeight + y * width + x + 1] = uint16_t((789 - layer) << 6);
                    }
                initial[layer] = {bytes[layer].data(), width * 2, 0};
            }
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = allocatedHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 3;
            desc.Format = DXGI_FORMAT_P010;
            desc.SampleDesc.Count = 1;
            desc.BindFlags = flags;
            ComPtr<ID3D11Texture2D> texture;
            auto created = gpu.d3d->device->CreateTexture2D(&desc, initial, &texture);
            if (FAILED(created))
                printf("texture HRESULT=%08lx bind=%u\n", created, flags);
            check(SUCCEEDED(created), "test decoder texture");
            AVFrame input = {};
            input.format = AV_PIX_FMT_D3D11;
            input.width = width;
            input.height = height;
            input.data[0] = reinterpret_cast<uint8_t*>(texture.Get());
            input.data[1] = reinterpret_cast<uint8_t*>(UINT_PTR(slice));
            context->CSSetShader(gpu.splitShader.Get(), nullptr, 0);
            ComPtr<ID3D11Texture2D> planes[3];
            UploadDvPlanes(&input, gpu.splitShader.Get(), planes);
            pl_gpu_finish(gpu.d3d->gpu);
            ComPtr<ID3D11ComputeShader> restored;
            context->CSGetShader(&restored, nullptr, nullptr);
            check(restored.Get() == gpu.splitShader.Get(), "shared context bindings preserved");
            for (UINT p = 0; p < 3; ++p)
            {
                D3D11_TEXTURE2D_DESC shape = {};
                planes[p]->GetDesc(&shape);
                shape.Usage = D3D11_USAGE_STAGING;
                shape.BindFlags = 0;
                shape.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> readback;
                check(SUCCEEDED(gpu.d3d->device->CreateTexture2D(&shape, nullptr, &readback)), "plane readback");
                context->CopyResource(readback.Get(), planes[p].Get());
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                check(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map plane");
                bool correct = true;
                for (UINT y = 0; y < shape.Height; ++y)
                {
                    auto row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) +
                                                                 y * mapped.RowPitch);
                    for (UINT x = 0; x < shape.Width; ++x)
                    {
                        unsigned expected = p == 0   ? (x + 2 * y + 17 * slice) % 1024
                                            : p == 1 ? 123 + slice
                                                     : 789 - slice;
                        correct &= row[x] == expected;
                    }
                }
                context->Unmap(readback.Get(), 0);
                check(correct, "exact 10-bit plane values, array slice and padded surface");
            }
            printf("PASS DV GPU planes: bind=%u, slice=2, padded height, exact 10-bit Y/U/V, preserved context\n",
                   flags);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "FAIL DV planes: %s\n", error.what());
        return 1;
    }
}
