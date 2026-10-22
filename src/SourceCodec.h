#pragma once
#include <dshow.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <algorithm>

inline std::string CodecSubtype(const GUID& subtype)
{
    if (subtype == MEDIASUBTYPE_MPEG2_VIDEO)
        return "MPEG-2";
    if (subtype == MEDIASUBTYPE_MPEG1Video || subtype == MEDIASUBTYPE_MPEG1Payload)
        return "MPEG-1";
    const GUID fourcc = {subtype.Data1, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
    if (subtype != fourcc)
        return {};
    char code[5] = {};
    for (int i = 0; i < 4; i++)
    {
        const unsigned c = (subtype.Data1 >> (8 * i)) & 255;
        code[i] = char(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    const std::string value = code;
    if (value == "H264" || value == "AVC1" || value == "X264" || value == "DAVC")
        return "H.264 / AVC";
    if (value == "HEVC" || value == "HVC1" || value == "HEV1" || value == "H265")
        return "HEVC / H.265";
    if (value == "AV01")
        return "AV1";
    if (value == "VP90" || value == "VP09")
        return "VP9";
    if (value == "VP80" || value == "VP08")
        return "VP8";
    if (value == "WVC1" || value == "WMVA")
        return "VC-1";
    if (value == "WMV3")
        return "WMV9";
    if (value == "WMV2")
        return "WMV8";
    if (value == "WMV1")
        return "WMV7";
    if (value == "XVID" || value == "DIVX" || value == "DX50" || value == "MP4V" || value == "FMP4" || value == "M4S2")
        return "MPEG-4 Part 2";
    if (value == "MJPG" || value == "JPEG")
        return "Motion JPEG";
    if (value == "DVSD" || value == "DVHD" || value == "DVSL")
        return "DV";
    if (value == "HFYU")
        return "HuffYUV";
    if (value == "FFV1")
        return "FFV1";
    if (value == "LAGS")
        return "Lagarith";
    return {};
}

// Read only the connected upstream video path. Never guess from a filename,
// filter name, or the decoded NV12/P010 format. Called at connection/start only.
inline std::string SourceCodecFromOutput(IPin* output)
{
    using Microsoft::WRL::ComPtr;
    std::vector<ComPtr<IBaseFilter>> pending;
    std::vector<ComPtr<IUnknown>> visited;
    auto append = [&](IPin* pin)
    {
        PIN_INFO info = {};
        if (pin && SUCCEEDED(pin->QueryPinInfo(&info)) && info.pFilter)
        {
            ComPtr<IBaseFilter> filter;
            filter.Attach(info.pFilter);
            pending.push_back(std::move(filter));
        }
    };
    append(output);
    for (size_t index = 0; index < pending.size() && index < 32; index++)
    {
        ComPtr<IUnknown> identity;
        if (FAILED(pending[index].As(&identity)))
            continue;
        if (std::any_of(visited.begin(), visited.end(), [&](const auto& v) { return v.Get() == identity.Get(); }))
            continue;
        visited.push_back(identity);
        ComPtr<IEnumPins> pins;
        if (FAILED(pending[index]->EnumPins(&pins)))
            continue;
        int videoInputs = 0;
        std::string found;
        for (int count = 0; count < 128; count++)
        {
            ComPtr<IPin> pin;
            if (pins->Next(1, &pin, nullptr) != S_OK)
                break;
            PIN_DIRECTION direction;
            if (FAILED(pin->QueryDirection(&direction)) || direction != PINDIR_INPUT)
                continue;
            ComPtr<IPin> peer;
            if (FAILED(pin->ConnectedTo(&peer)))
                continue;
            AM_MEDIA_TYPE type = {};
            const HRESULT hr = pin->ConnectionMediaType(&type);
            const bool video = SUCCEEDED(hr) && type.majortype == MEDIATYPE_Video;
            const std::string codec = video ? CodecSubtype(type.subtype) : std::string{};
            if (type.cbFormat)
                CoTaskMemFree(type.pbFormat);
            if (type.pUnk)
                type.pUnk->Release();
            if (video)
            {
                if (++videoInputs > 1)
                    return {};
                found = codec;
                if (codec.empty() && pending.size() < 32)
                    append(peer.Get());
            }
        }
        if (!found.empty())
            return found;
    }
    return {};
}
