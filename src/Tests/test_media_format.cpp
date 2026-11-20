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
    // The row pitch is biWidth, whatever spare room biSizeImage declares.
    {
        VIDEOINFOHEADER2 info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 854;
        info.bmiHeader.biHeight = 480;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 12;
        info.bmiHeader.biCompression = MAKEFOURCC('N', 'V', '1', '2');
        info.bmiHeader.biSizeImage = 616320; // MPC Video Decoder: 856 x 480 x 1.5
        info.dwPictAspectRatioX = 16;
        info.dwPictAspectRatioY = 9;
        AM_MEDIA_TYPE type = {};
        type.majortype = MEDIATYPE_Video;
        type.subtype = MEDIASUBTYPE_NV12;
        type.formattype = FORMAT_VideoInfo2;
        type.cbFormat = sizeof(info);
        type.pbFormat = reinterpret_cast<BYTE*>(&info);
        MediaFormat format;
        Require(SUCCEEDED(ParseFormat(type, format)) && format.frame.stride == 854, "NV12 854x480 rows are 854 bytes");
        info.bmiHeader.biWidth = 864; // wider rows, picture in rcSource
        info.rcSource = {0, 0, 854, 480};
        Require(SUCCEEDED(ParseFormat(type, format)) && format.frame.stride == 864 && format.source.right == 854,
                "a wider biWidth is the pitch, rcSource the picture");
        type.subtype = MEDIASUBTYPE_RGB24;
        info.bmiHeader.biCompression = BI_RGB;
        info.bmiHeader.biBitCount = 24;
        info.bmiHeader.biWidth = 854;
        info.rcSource = {};
        info.bmiHeader.biSizeImage = 0;
        Require(SUCCEEDED(ParseFormat(type, format)) && format.frame.stride == 2564, "RGB24 rows round to DWORDs");
    }
    puts("PASS row pitch is biWidth; spare room in biSizeImage does not change it");
    {
        VideoFrame pq, hlg, ipt;
        pq.color.transfer = PL_COLOR_TRC_PQ;
        hlg.color.transfer = PL_COLOR_TRC_HLG;
        ipt.color.transfer = PL_COLOR_TRC_UNKNOWN;
        Require(!KeepsDolbyVision(pq, false, 8) && !KeepsDolbyVision(hlg, false, 8) && !KeepsDolbyVision(pq, false, 7),
                "off: Profile 7 and 8 play their HDR10 or HLG base layer");
        Require(!KeepsDolbyVision(pq, false, 0), "off: an HDR10 base layer of unknown profile is used as HDR10");
        Require(KeepsDolbyVision(pq, false, 5) && KeepsDolbyVision(ipt, false, 0),
                "Profile 5 always keeps Dolby Vision");
        Require(KeepsDolbyVision(pq, true, 8) && KeepsDolbyVision(hlg, true, 8), "on: Dolby Vision is used");
    }
    puts("PASS Use Dolby Vision: off leaves Profile 7 and 8 as HDR10 or HLG, Profile 5 keeps Dolby Vision");
}
