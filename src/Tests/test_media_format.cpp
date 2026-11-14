#include "Renderer/MediaFormat.h"
#include <uuids.h>
#include <cstdio>
#include <cstdlib>

// RGB orientation from the media type: a positive height means a bottom-up
// DIB for BI_RGB and BI_BITFIELDS alike (issue #16); YUV is always top-down.
static void Require(bool ok, const char* text)
{
    if (!ok)
    {
        printf("FAIL %s\n", text);
        exit(1);
    }
}

static bool BottomUp(const GUID& subtype, DWORD compression, LONG height, WORD bits)
{
    VIDEOINFOHEADER2 info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 64;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = bits;
    info.bmiHeader.biCompression = compression;
    info.dwPictAspectRatioX = info.dwPictAspectRatioY = 1;
    AM_MEDIA_TYPE type = {};
    type.majortype = MEDIATYPE_Video;
    type.subtype = subtype;
    type.formattype = FORMAT_VideoInfo2;
    type.cbFormat = sizeof(info);
    type.pbFormat = reinterpret_cast<BYTE*>(&info);
    MediaFormat format;
    Require(SUCCEEDED(ParseFormat(type, format)), "media type accepted");
    Require(format.frame.height == 64, "height is the absolute value");
    return format.frame.bottomUp;
}

int main()
{
    Require(BottomUp(MEDIASUBTYPE_RGB32, BI_RGB, 64, 32), "RGB32 BI_RGB positive height is bottom-up");
    Require(!BottomUp(MEDIASUBTYPE_RGB32, BI_RGB, -64, 32), "RGB32 BI_RGB negative height is top-down");
    Require(BottomUp(MEDIASUBTYPE_RGB32, BI_BITFIELDS, 64, 32), "RGB32 BI_BITFIELDS positive height is bottom-up");
    Require(!BottomUp(MEDIASUBTYPE_RGB32, BI_BITFIELDS, -64, 32), "RGB32 BI_BITFIELDS negative height is top-down");
    Require(BottomUp(MEDIASUBTYPE_RGB24, BI_RGB, 64, 24), "RGB24 positive height is bottom-up");
    Require(!BottomUp(MEDIASUBTYPE_NV12, MAKEFOURCC('N', 'V', '1', '2'), 64, 12), "NV12 is always top-down");
    // Asking the upstream filter for top-down rows (DirectVobSub, issue #16).
    {
        VIDEOINFOHEADER2 info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 64;
        info.bmiHeader.biHeight = 64;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        info.dwPictAspectRatioX = info.dwPictAspectRatioY = 1;
        AM_MEDIA_TYPE type = {};
        type.majortype = MEDIATYPE_Video;
        type.subtype = MEDIASUBTYPE_RGB32;
        type.formattype = FORMAT_VideoInfo2;
        type.cbFormat = sizeof(info);
        type.pbFormat = reinterpret_cast<BYTE*>(&info);
        Require(MakeTopDown(type) && info.bmiHeader.biHeight == -64, "bottom-up RGB becomes top-down");
        MediaFormat format;
        Require(SUCCEEDED(ParseFormat(type, format)) && !format.frame.bottomUp, "the new type reads top-down");
        Require(!MakeTopDown(type) && info.bmiHeader.biHeight == -64, "top-down RGB stays as it is");
        type.subtype = MEDIASUBTYPE_NV12;
        info.bmiHeader.biCompression = MAKEFOURCC('N', 'V', '1', '2');
        info.bmiHeader.biBitCount = 12;
        info.bmiHeader.biHeight = 64;
        Require(!MakeTopDown(type) && info.bmiHeader.biHeight == 64, "YUV is never changed");
    }
    puts("PASS RGB orientation follows the sign of biHeight for BI_RGB and BI_BITFIELDS");
}
