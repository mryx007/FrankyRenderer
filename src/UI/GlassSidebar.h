#pragma once
#include "UI/MenuTheme.h"

inline constexpr float SidebarCornerRadius = 14.f;

// Frosted sidebar using only the existing Direct2D render target. Two resampling
// passes provide a soft backdrop without an extra graphics device or runtime.
// The foreground controls are drawn afterwards and remain sharp.
inline bool DrawFrostedSidebar(ID2D1HwndRenderTarget* target, float width, float height)
{
    const auto palette = CurrentMenuPalette();
    if (height <= 0 || width <= 0)
        return false;
    float dpiX = 96, dpiY = 96;
    target->GetDpi(&dpiX, &dpiY);
    const auto pixels = target->GetPixelSize();
    const auto captureWidth = std::min(pixels.width, UINT32(std::ceil(width * dpiX / 96.f)));
    if (!captureWidth || !pixels.height)
        return false;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> scene;
    const auto properties = D2D1::BitmapProperties(target->GetPixelFormat(), dpiX, dpiY);
    if (FAILED(target->CreateBitmap(
            D2D1::SizeU(captureWidth, pixels.height), nullptr, 0, properties, scene.GetAddressOf())))
        return false;
    const auto sourceRect = D2D1::RectU(0, 0, captureWidth, pixels.height);
    const auto destination = D2D1::Point2U(0, 0);
    if (FAILED(scene->CopyFromRenderTarget(&destination, target, &sourceRect)))
        return false;
    const auto bounds = D2D1::RectF(0, 0, width, height);
    Microsoft::WRL::ComPtr<ID2D1Bitmap> softened = scene;
    for (const UINT32 divisor : {12u, 4u})
    {
        Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> pass;
        const auto desired = D2D1::SizeF(width, height);
        const auto resolution =
            D2D1::SizeU(std::max(1u, captureWidth / divisor), std::max(1u, pixels.height / divisor));
        if (FAILED(target->CreateCompatibleRenderTarget(
                &desired, &resolution, nullptr, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, pass.GetAddressOf())))
            return false;
        pass->BeginDraw();
        pass->Clear(D2D1::ColorF(palette.sidebar));
        pass->DrawBitmap(softened.Get(), bounds, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (FAILED(pass->EndDraw()) || FAILED(pass->GetBitmap(softened.ReleaseAndGetAddressOf())))
            return false;
    }
    const D2D1_GRADIENT_STOP stops[] = {{0.f, D2D1::ColorF(palette.glassTop, .80f)},
                                        {.65f, D2D1::ColorF(palette.glassMid, .76f)},
                                        {1.f, D2D1::ColorF(palette.glassBottom, .86f)}};
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> tint;
    if (FAILED(target->CreateGradientStopCollection(stops, 3, collection.GetAddressOf())) ||
        FAILED(target->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(width * .4f, height)),
            collection.Get(),
            tint.GetAddressOf())))
        return false;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> mask;
    target->GetFactory(factory.GetAddressOf());
    if (FAILED(factory->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(bounds, SidebarCornerRadius, SidebarCornerRadius), mask.GetAddressOf())))
        return false;
    // Capture the backdrop before opening a layer; CopyFromRenderTarget cannot
    // read from a render target while a clipping layer is active.
    target->PushLayer(D2D1::LayerParameters(bounds, mask.Get()), nullptr);
    target->DrawBitmap(softened.Get(), bounds, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    target->FillRectangle(bounds, tint.Get());
    target->PopLayer();
    return true;
}
