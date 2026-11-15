#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// Keep at most one pending request and one completed image. All CPU text
// drawing runs outside the video thread; this worker never touches D3D11.
class StatisticsRasterizer
{
  public:
    static constexpr UINT ReadyMessage = WM_APP + 0x392;

    struct Image
    {
        std::vector<unsigned char> pixels;
        int width = 0, height = 0, viewWidth = 0, viewHeight = 0, fontHeight = 0;
    };

  private:
    struct Request
    {
        std::wstring text;
        int width, height;
        uint64_t generation;
        HWND window;
        int textScale; // percent of the automatic size
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::optional<Request> pending;
    std::optional<Image> ready;
    uint64_t generation = 0;
    bool stopping = false;

    // The box never shrinks and the font never grows while the view size
    // stays the same, so changing numbers and a row that comes and goes (Queue
    // while playing) do not make it jitter. Worker thread only.
    int keptWidth = 0, keptHeight = 0, keptViewWidth = 0, keptViewHeight = 0, keptFont = 0, keptTextScale = 0;
    uint64_t keptGeneration = 0;

    // Statistics text markup: a line "\x01Title" is a section heading, a line
    // "label\tvalue" is a row with aligned columns, and "\x02y" or "\x02r" at
    // the start of a value colours it yellow or red. Inside a value the same
    // codes, and "\x02w" for white, colour the text after them.
    static COLORREF CodeColor(wchar_t code, COLORREF fallback)
    {
        return code == L'y'   ? RGB(225, 205, 155)
               : code == L'r' ? RGB(235, 155, 145)
               : code == L'w' ? RGB(255, 255, 255)
                              : fallback;
    }

    static std::wstring Plain(const std::wstring& value)
    {
        std::wstring out;
        for (size_t i = 0; i < value.size(); ++i)
            if (value[i] == L'\x02')
                ++i;
            else
                out += value[i];
        return out;
    }

    struct Line
    {
        enum class Kind
        {
            Text,
            Heading,
            Row
        } kind = Kind::Text;
        std::wstring label, value;
        COLORREF color = RGB(255, 255, 255);
    };

    static std::vector<Line> Parse(const std::wstring& text)
    {
        std::vector<Line> lines;
        size_t at = 0;
        while (at < text.size())
        {
            auto end = text.find(L'\n', at);
            if (end == std::wstring::npos)
                end = text.size();
            std::wstring raw = text.substr(at, end - at);
            at = end + 1;
            Line line;
            if (!raw.empty() && raw[0] == L'\x01')
            {
                line.kind = Line::Kind::Heading;
                line.value = raw.substr(1);
            }
            else if (const auto tab = raw.find(L'\t'); tab != std::wstring::npos)
            {
                line.kind = Line::Kind::Row;
                line.label = raw.substr(0, tab);
                line.value = raw.substr(tab + 1);
            }
            else
                line.value = raw;
            if (line.value.size() >= 2 && line.value[0] == L'\x02')
            {
                line.color = CodeColor(line.value[1], line.color);
                line.value.erase(0, 2);
            }
            lines.push_back(std::move(line));
        }
        return lines;
    }

    static std::optional<Image> Draw(const Request& request, int minWidth, int minHeight, int maxFont = 0)
    {
        const int viewWidth = request.width, viewHeight = request.height;
        if (viewWidth <= 16 || viewHeight <= 0)
            return {};
        const auto lines = Parse(request.text);
        int gaps = 0;
        for (size_t i = 1; i < lines.size(); ++i)
            gaps += lines[i].kind == Line::Kind::Heading;
        // Sections are separated by half a line.
        const auto contentHeight = [&](int lineHeight)
        { return int(lines.size()) * lineHeight + gaps * lineHeight / 2 + 16; };
        // 1/45 of the view height, at most 24 pixels, scaled by the setting.
        const int automatic = std::clamp(viewHeight / 45, 10, 24);
        int fontHeight = std::max(10, (automatic * std::clamp(request.textScale, 50, 200) + 50) / 100);
        if (maxFont > 0)
            fontHeight = std::min(fontHeight, maxFont);
        while (fontHeight > 10 && contentHeight(fontHeight + std::max(4, fontHeight / 3)) > viewHeight)
            --fontHeight;
        const int lineHeight = fontHeight + std::max(4, fontHeight / 3),
                  h = std::min(viewHeight, std::max(minHeight, contentHeight(lineHeight)));
        // DirectWrite places glyphs at fractional positions without hinting,
        // so letter spacing and stroke weight stay even at small sizes (GDI
        // rounded every glyph to whole pixels).
        static const auto write = []
        {
            Microsoft::WRL::ComPtr<IDWriteFactory> factory;
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
            return factory;
        }();
        static const auto d2d = []
        {
            Microsoft::WRL::ComPtr<ID2D1Factory> factory;
            D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, factory.GetAddressOf());
            return factory;
        }();
        if (!write || !d2d)
            return {};
        const auto makeFormat = [&](DWRITE_FONT_WEIGHT weight)
        {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
            write->CreateTextFormat(L"Segoe UI",
                                    nullptr,
                                    weight,
                                    DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL,
                                    float(fontHeight),
                                    L"en-us",
                                    format.GetAddressOf());
            if (format)
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            return format;
        };
        const auto font = makeFormat(DWRITE_FONT_WEIGHT_NORMAL), bold = makeFormat(DWRITE_FONT_WEIGHT_SEMI_BOLD);
        if (!font || !bold)
            return {};
        const auto layout = [&](const std::wstring& value, IDWriteTextFormat* format)
        {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> result;
            write->CreateTextLayout(
                value.data(), UINT32(value.size()), format, 100000.f, 1000.f, result.GetAddressOf());
            return result;
        };
        const auto extent = [&](const std::wstring& value, IDWriteTextFormat* format)
        {
            DWRITE_TEXT_METRICS metrics = {};
            if (const auto text = layout(value, format); !text || FAILED(text->GetMetrics(&metrics)))
                return 0.f;
            return metrics.widthIncludingTrailingWhitespace;
        };
        float labelWidth = 0, textWidth = 0;
        for (const auto& line : lines)
            if (line.kind == Line::Kind::Row)
                labelWidth = std::max(labelWidth, extent(line.label, font.Get()));
        const float valueX = std::ceil(labelWidth) + fontHeight;
        for (const auto& line : lines)
            textWidth =
                std::max(textWidth,
                         (line.kind == Line::Kind::Row ? valueX : 0) +
                             extent(Plain(line.value), line.kind == Line::Kind::Heading ? bold.Get() : font.Get()));
        const int w = std::min(viewWidth - 16, std::max(minWidth, int(std::ceil(textWidth)) + 24));
        Image image;
        image.width = w;
        image.height = h;
        image.viewWidth = viewWidth;
        image.viewHeight = viewHeight;
        image.fontHeight = fontHeight;
        image.pixels.resize(static_cast<size_t>(w) * h * 4);
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc)
            return {};
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* data = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap)
        {
            DeleteDC(dc);
            return {};
        }
        auto oldBitmap = SelectObject(dc, bitmap);
        const auto release = [&]
        {
            SelectObject(dc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(dc);
        };
        memset(data, 0, image.pixels.size());
        // Text on black in a software target bound to the bitmap, greyscale
        // smoothing (no colour fringes on the translucent box).
        Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
        const auto properties =
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                                         96.f,
                                         96.f);
        const RECT bounds = {0, 0, w, h};
        if (FAILED(d2d->CreateDCRenderTarget(&properties, target.GetAddressOf())) ||
            FAILED(target->BindDC(dc, &bounds)))
        {
            release();
            return {};
        }
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        Microsoft::WRL::ComPtr<IDWriteRenderingParams> defaults, params;
        if (SUCCEEDED(write->CreateRenderingParams(defaults.GetAddressOf())) &&
            SUCCEEDED(write->CreateCustomRenderingParams(defaults->GetGamma(),
                                                         defaults->GetEnhancedContrast(),
                                                         0.f,
                                                         DWRITE_PIXEL_GEOMETRY_FLAT,
                                                         DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
                                                         params.GetAddressOf())))
            target->SetTextRenderingParams(params.Get());
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        target->BeginDraw();
        target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush.GetAddressOf());
        if (!brush)
        {
            target->EndDraw();
            release();
            return {};
        }
        const auto draw = [&](float at, int top, const std::wstring& value, IDWriteTextFormat* format, COLORREF color)
        {
            brush->SetColor(D2D1::ColorF(GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f));
            if (const auto text = layout(value, format))
                target->DrawTextLayout(D2D1::Point2F(at, float(top)), text.Get(), brush.Get());
        };
        const float x = 12;
        int y = 8;
        for (size_t i = 0; i < lines.size() && y < h; ++i)
        {
            const auto& line = lines[i];
            if (line.kind == Line::Kind::Heading)
            {
                if (i > 0)
                    y += lineHeight / 2;
                draw(x, y, line.value, bold.Get(), RGB(130, 185, 255));
            }
            else
            {
                float valueAt = x;
                if (line.kind == Line::Kind::Row)
                {
                    draw(x, y, line.label, font.Get(), RGB(165, 170, 180));
                    valueAt = x + valueX;
                }
                // Draw the value in segments between colour codes.
                COLORREF color = line.color;
                for (size_t from = 0;;)
                {
                    const auto code = line.value.find(L'\x02', from);
                    const auto part = line.value.substr(from, code == std::wstring::npos ? code : code - from);
                    draw(valueAt, y, part, font.Get(), color);
                    valueAt += extent(part, font.Get());
                    if (code == std::wstring::npos || code + 1 >= line.value.size())
                        break;
                    color = CodeColor(line.value[code + 1], color);
                    from = code + 2;
                }
            }
            y += lineHeight;
        }
        if (FAILED(target->EndDraw()))
        {
            release();
            return {};
        }
        brush.Reset();
        target.Reset(); // before the bitmap and its DC go
        GdiFlush();
        // Text was drawn on black, so the brightest channel is the glyph
        // coverage. Composite onto the translucent background, straight alpha;
        // GDI stores BGR, the overlay texture RGB.
        const auto bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < image.pixels.size(); i += 4)
        {
            const unsigned coverage = std::max({bytes[i], bytes[i + 1], bytes[i + 2]});
            const unsigned alpha = 150 + coverage * 105 / 255;
            for (int c = 0; c < 3; ++c)
                image.pixels[i + c] = static_cast<unsigned char>(bytes[i + 2 - c] * 255 / alpha);
            image.pixels[i + 3] = static_cast<unsigned char>(alpha);
        }
        release();
        return image;
    }

    void Run()
    {
        for (;;)
        {
            std::optional<Request> request;
            {
                std::unique_lock guard(mutex);
                wake.wait(guard, [&] { return stopping || pending.has_value(); });
                if (stopping)
                    return;
                request = std::move(pending);
                pending.reset();
            }
            const bool same = request->generation == keptGeneration && request->width == keptViewWidth &&
                              request->height == keptViewHeight && request->textScale == keptTextScale;
            std::optional<Image> image;
            try
            {
                image = Draw(*request, same ? keptWidth : 0, same ? keptHeight : 0, same ? keptFont : 0);
            }
            catch (...)
            { /* Retain the previous overlay on allocation failure. */
            }
            if (image)
            {
                keptWidth = image->width;
                keptHeight = image->height;
                keptFont = image->fontHeight;
                keptViewWidth = request->width;
                keptViewHeight = request->height;
                keptGeneration = request->generation;
                keptTextScale = request->textScale;
            }
            {
                std::lock_guard guard(mutex);
                if (!stopping && request->generation == generation && image)
                {
                    ready = std::move(image);
                    if (request->window)
                        PostMessageW(request->window, ReadyMessage, 0, 0);
                }
            }
        }
    }

  public:
    ~StatisticsRasterizer()
    {
        Stop();
    }

    void Stop()
    {
        {
            std::lock_guard guard(mutex);
            stopping = true;
            pending.reset();
        }
        wake.notify_one();
        if (worker.joinable())
            worker.join();
    }

    void Reset()
    {
        std::lock_guard guard(mutex);
        ++generation;
        pending.reset();
        ready.reset();
    }

    bool Submit(const std::wstring& text, int width, int height, HWND window = nullptr, int textScale = 100) noexcept
    {
        try
        {
            std::lock_guard guard(mutex);
            if (stopping)
                return false;
            if (!worker.joinable())
                worker = std::thread([this] { Run(); });
            pending = Request{text, width, height, generation, window, textScale};
            wake.notify_one();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<Image> Take()
    {
        std::lock_guard guard(mutex);
        auto result = std::move(ready);
        ready.reset();
        return result;
    }
};
