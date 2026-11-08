#pragma once
#include <windows.h>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Keep at most one pending request and one completed image. All CPU text
// drawing runs outside the video thread; this worker never touches D3D11.
class StatisticsRasterizer
{
  public:
    static constexpr UINT ReadyMessage = WM_APP + 0x392;

    struct Image
    {
        std::vector<unsigned char> pixels;
        int width = 0, height = 0, viewWidth = 0, viewHeight = 0;
    };

  private:
    struct Request
    {
        std::wstring text;
        int width, height;
        uint64_t generation;
        HWND window;
    };

    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::optional<Request> pending;
    std::optional<Image> ready;
    uint64_t generation = 0;
    bool stopping = false;

    // The box never shrinks while the view size stays the same, so changing
    // numbers do not make it jitter. Worker thread only.
    int keptWidth = 0, keptHeight = 0, keptViewWidth = 0, keptViewHeight = 0;
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

    static std::optional<Image> Draw(const Request& request, int minWidth, int minHeight)
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
        int fontHeight = std::clamp(viewHeight / 45, 10, 24);
        while (fontHeight > 10 && contentHeight(fontHeight + std::max(4, fontHeight / 3)) > viewHeight)
            --fontHeight;
        const int lineHeight = fontHeight + std::max(4, fontHeight / 3),
                  h = std::min(viewHeight, std::max(minHeight, contentHeight(lineHeight)));
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc)
            return {};
        const auto makeFont = [&](int weight)
        {
            return CreateFontW(-fontHeight,
                               0,
                               0,
                               0,
                               weight,
                               FALSE,
                               FALSE,
                               FALSE,
                               DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY,
                               DEFAULT_PITCH,
                               L"Segoe UI");
        };
        HFONT font = makeFont(FW_NORMAL), bold = makeFont(FW_SEMIBOLD);
        if (!font || !bold)
        {
            if (font)
                DeleteObject(font);
            if (bold)
                DeleteObject(bold);
            DeleteDC(dc);
            return {};
        }
        auto oldFont = SelectObject(dc, font);
        const auto release = [&]
        {
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteObject(bold);
            DeleteDC(dc);
        };
        const auto extent = [&](const std::wstring& value)
        {
            SIZE size = {};
            GetTextExtentPoint32W(dc, value.data(), int(value.size()), &size);
            return int(size.cx);
        };
        int labelWidth = 0, textWidth = 0;
        for (const auto& line : lines)
            if (line.kind == Line::Kind::Row)
                labelWidth = std::max(labelWidth, extent(line.label));
        const int valueX = labelWidth + fontHeight;
        for (const auto& line : lines)
        {
            if (line.kind == Line::Kind::Heading)
                SelectObject(dc, bold);
            textWidth = std::max(textWidth, (line.kind == Line::Kind::Row ? valueX : 0) + extent(Plain(line.value)));
            SelectObject(dc, font);
        }
        const int w = std::min(viewWidth - 16, std::max(minWidth, textWidth + 24));
        Image image;
        image.width = w;
        image.height = h;
        image.viewWidth = viewWidth;
        image.viewHeight = viewHeight;
        try
        {
            image.pixels.resize(static_cast<size_t>(w) * h * 4);
        }
        catch (...)
        {
            release();
            throw;
        }
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
            release();
            return {};
        }
        auto oldBitmap = SelectObject(dc, bitmap);
        memset(data, 0, image.pixels.size());
        SetBkMode(dc, TRANSPARENT);
        const int x = 12;
        int y = 8;
        for (size_t i = 0; i < lines.size() && y < h; ++i)
        {
            const auto& line = lines[i];
            if (line.kind == Line::Kind::Heading)
            {
                if (i > 0)
                    y += lineHeight / 2;
                SelectObject(dc, bold);
                SetTextColor(dc, RGB(130, 185, 255));
                TextOutW(dc, x, y, line.value.data(), int(line.value.size()));
                SelectObject(dc, font);
            }
            else
            {
                int valueAt = x;
                if (line.kind == Line::Kind::Row)
                {
                    SetTextColor(dc, RGB(165, 170, 180));
                    TextOutW(dc, x, y, line.label.data(), int(line.label.size()));
                    valueAt = x + valueX;
                }
                // Draw the value in segments between colour codes.
                COLORREF color = line.color;
                for (size_t from = 0;;)
                {
                    const auto code = line.value.find(L'\x02', from);
                    const auto part = line.value.substr(from, code == std::wstring::npos ? code : code - from);
                    SetTextColor(dc, color);
                    TextOutW(dc, valueAt, y, part.data(), int(part.size()));
                    valueAt += extent(part);
                    if (code == std::wstring::npos || code + 1 >= line.value.size())
                        break;
                    color = CodeColor(line.value[code + 1], color);
                    from = code + 2;
                }
            }
            y += lineHeight;
        }
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
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
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
                              request->height == keptViewHeight;
            std::optional<Image> image;
            try
            {
                image = Draw(*request, same ? keptWidth : 0, same ? keptHeight : 0);
            }
            catch (...)
            { /* Retain the previous overlay on allocation failure. */
            }
            if (image)
            {
                keptWidth = image->width;
                keptHeight = image->height;
                keptViewWidth = request->width;
                keptViewHeight = request->height;
                keptGeneration = request->generation;
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

    bool Submit(const std::wstring& text, int width, int height, HWND window = nullptr) noexcept
    {
        try
        {
            std::lock_guard guard(mutex);
            if (stopping)
                return false;
            if (!worker.joinable())
                worker = std::thread([this] { Run(); });
            pending = Request{text, width, height, generation, window};
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
