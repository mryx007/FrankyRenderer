#pragma once
#include <windows.h>
#include <d3d11.h>
#include <unknwn.h>
#include <libplacebo/colorspace.h>

// Internal prepared-frame payload, not a public decoder API. All consumers
// must use the same libplacebo API and validate payload size before copying.
inline constexpr UINT DvTransportVersion = 2; // MEL carries only layer zero; FEL carries both layers.

struct DvFrameInfo
{
    UINT version, placeboApi, bytes;
    UINT width[2], height[2];
    LONGLONG pts;
    pl_dovi_metadata dovi;
    pl_color_space color;
};

MIDL_INTERFACE("2B0D4666-795D-43DA-9905-4F3D501386EB") IDvLayerSample : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetInfo(UINT version, UINT bytes, DvFrameInfo * info) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlane(UINT layer, UINT plane, ID3D11Texture2D * *texture) = 0;
};
