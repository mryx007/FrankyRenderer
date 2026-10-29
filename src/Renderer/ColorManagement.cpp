#include "Renderer/ColorManagement.h"
#include <cmath>
#include <cstring>

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

HRESULT PrepareColorAssets(const RenderSettings& settings, std::shared_ptr<ColorAssets>& out)
{
    out.reset();
    if (!settings.iccEnabled && !settings.lutEnabled)
        return S_OK;
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
