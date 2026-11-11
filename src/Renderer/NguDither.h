#pragma once
#include <cstdint>

// E-044: target PS_1352 consumes consecutive MSVC CRT rand() values for
// U2.y/U2.z. The process-wide reset point for long playback remains unknown,
// so callers explicitly opt into advancing this per-scaler sequence.
class NguDither
{
  public:
    struct Phase
    {
        float y;
        float z;
    };

    void Reset()
    {
        randomState_ = 1;
    }

    Phase NextPhase()
    {
        return {NextTargetValue(), NextTargetValue()};
    }

  private:
    float NextTargetValue()
    {
        randomState_ = randomState_ * 0x343fdu + 0x269ec3u;
        return static_cast<float>((randomState_ >> 16) & 0x7fffu) / 32767.0f;
    }

    uint32_t randomState_ = 1;
};
