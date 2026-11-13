#pragma once
#include "Renderer/Backend.h"
#include <dshow.h>
#include <dvdmedia.h>

struct MediaFormat
{
    VideoFrame frame;
    REFERENCE_TIME averageDuration = 0;
    bool interlaced = false, topFieldFirst = true;
    RECT source = {};
    VIDEOINFOHEADER basic = {};
};

HRESULT ParseFormat(const AM_MEDIA_TYPE& type, MediaFormat& out);
// Turns bottom-up RGB (positive height) into the same type top-down. Returns
// false for every other type, which stays unchanged.
bool MakeTopDown(AM_MEDIA_TYPE& type);

struct HdrSideDataStatus
{
    HRESULT interfaceResult = E_NOINTERFACE, masteringResult = E_NOINTERFACE, lightResult = E_NOINTERFACE,
            dynamicResult = E_NOINTERFACE;
    size_t masteringBytes = 0, lightBytes = 0, dynamicBytes = 0;
    bool mastering = false, light = false, dynamic = false, dynamicLegacy = false;
    HRESULT doviResult = E_NOINTERFACE;
    size_t doviBytes = 0;
    bool dovi = false;
};

HdrSideDataStatus ReadHdrSideData(IMediaSample* sample, VideoFrame& frame);
// True when the decoder attached Dolby Vision metadata to this sample.
bool HasDoviSideData(IMediaSample*);
// Applies decoder-layout (V2) Dolby Vision side data supplied by another source.
bool ApplyDoviSideData(const std::vector<BYTE>&, VideoFrame&);
// True when the decoder attached HDR10+ dynamic metadata to this sample.
bool HasHdr10PlusSideData(IMediaSample*);
// Applies decoder-layout HDR10+ side data supplied by another source.
bool ApplyHdr10PlusSideData(const std::vector<BYTE>&, VideoFrame&);
