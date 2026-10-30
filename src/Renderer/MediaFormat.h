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
