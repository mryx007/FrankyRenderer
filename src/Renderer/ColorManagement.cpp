#include "Renderer/ColorManagement.h"
#include <cmath>
#include <cstring>
#include <mutex>

static HRESULT ReadColorFile(const std::wstring& path, size_t maximum, std::vector<char>& data)
{
    if (path.empty())
        return E_INVALIDARG;
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 1 || static_cast<uint64_t>(size.QuadPart) > maximum)
    {
        CloseHandle(file);
        return E_INVALIDARG;
    }
    data.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
    DWORD error = ok ? ERROR_HANDLE_EOF : GetLastError();
    CloseHandle(file);
    return ok && read == data.size() ? S_OK : HRESULT_FROM_WIN32(error);
}

// Live preview calls this for every slider step. Reuse the parsed profile and
// LUT while their files and the inputs that shape them are unchanged.
struct ColorAssetKey
{
    bool icc = false, lut = false;
    std::wstring iccPath, lutPath;
    WIN32_FILE_ATTRIBUTE_DATA iccFile = {}, lutFile = {};
    int iccIntent = 0;
    float sdrNits = 0;

    bool operator==(const ColorAssetKey& o) const
    {
        const auto same = [](const WIN32_FILE_ATTRIBUTE_DATA& a, const WIN32_FILE_ATTRIBUTE_DATA& b)
        {
            return a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
                   CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0;
        };
        return icc == o.icc && lut == o.lut &&
               (!icc || (iccPath == o.iccPath && same(iccFile, o.iccFile) && iccIntent == o.iccIntent &&
                         sdrNits == o.sdrNits)) &&
               (!lut || (lutPath == o.lutPath && same(lutFile, o.lutFile)));
    }
};

static bool ColorAssetKeyFor(const RenderSettings& settings, ColorAssetKey& key)
{
    key.icc = settings.iccEnabled;
    key.lut = settings.lutEnabled;
    if (key.icc)
    {
        key.iccPath = settings.iccPath;
        key.iccIntent = settings.iccIntent;
        key.sdrNits = settings.sdrNits;
        if (!GetFileAttributesExW(key.iccPath.c_str(), GetFileExInfoStandard, &key.iccFile))
            return false;
    }
    if (key.lut)
    {
        key.lutPath = settings.lutPath;
        if (!GetFileAttributesExW(key.lutPath.c_str(), GetFileExInfoStandard, &key.lutFile))
            return false;
    }
    return true;
}

static HRESULT LoadColorAssets(const RenderSettings& settings, std::shared_ptr<ColorAssets>& out);

HRESULT PrepareColorAssets(const RenderSettings& settings, std::shared_ptr<ColorAssets>& out)
{
    out.reset();
    if (!settings.iccEnabled && !settings.lutEnabled)
        return S_OK;
    static std::mutex lock;
    static ColorAssetKey cachedKey;
    static std::shared_ptr<ColorAssets> cached;
    ColorAssetKey key;
    const bool cacheable = ColorAssetKeyFor(settings, key);
    {
        std::lock_guard guard(lock);
        if (cacheable && cached && key == cachedKey)
        {
            out = cached;
            return S_OK;
        }
    }
    const HRESULT hr = LoadColorAssets(settings, out);
    if (SUCCEEDED(hr) && cacheable)
    {
        std::lock_guard guard(lock);
        cachedKey = std::move(key);
        cached = out;
    }
    return hr;
}

static HRESULT LoadColorAssets(const RenderSettings& settings, std::shared_ptr<ColorAssets>& out)
{
    auto prepared = std::make_shared<ColorAssets>();
    std::vector<char> data;
    if (settings.iccEnabled)
    {
        HRESULT hr = ReadColorFile(settings.iccPath, 32 * 1024 * 1024, data);
        if (FAILED(hr))
            return hr;
        // Only RGB display/color-space profiles, not printer/CMYK profiles.
        if (data.size() < 128 || memcmp(data.data() + 36, "acsp", 4) || memcmp(data.data() + 16, "RGB ", 4) ||
            (memcmp(data.data() + 12, "mntr", 4) && memcmp(data.data() + 12, "spac", 4)))
            return E_INVALIDARG;
        pl_icc_profile profile = {};
        profile.data = data.data();
        profile.len = data.size();
        pl_icc_profile_compute_signature(&profile);
        pl_icc_params params = pl_icc_default_params;
        params.intent = settings.iccIntent == 1 ? PL_INTENT_PERCEPTUAL : PL_INTENT_RELATIVE_COLORIMETRIC;
        params.max_luma = settings.sdrNits;
        prepared->icc = pl_icc_open(nullptr, &profile, &params);
        if (!prepared->icc)
            return E_INVALIDARG;
    }
    if (settings.lutEnabled)
    {
        HRESULT hr = ReadColorFile(settings.lutPath, 64 * 1024 * 1024, data);
        if (FAILED(hr))
            return hr;
        prepared->lut = pl_lut_parse_cube(nullptr, data.data(), data.size());
        if (!prepared->lut)
            return E_INVALIDARG;
        const auto& lut = *prepared->lut;
        size_t cells = 1;
        for (int size : lut.size)
        {
            if (size < 2 || size > 65)
                return E_INVALIDARG;
            cells *= size;
        }
        if (!lut.data)
            return E_INVALIDARG;
        for (size_t i = 0; i < cells * 3; i++)
            if (!std::isfinite(lut.data[i]) || std::abs(lut.data[i]) > 16)
                return E_INVALIDARG;
    }
    out = std::move(prepared);
    return S_OK;
}
