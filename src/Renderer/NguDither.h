#pragma once
#include <array>
#include <cstdint>
#include <vector>

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

    // FUN_4a429160: init_by_array with
    // {GetTickCount(), FILETIME.dwLowDateTime, FILETIME.dwHighDateTime}.
    void SeedColor(uint32_t tick, uint32_t fileTimeLow, uint32_t fileTimeHigh)
    {
        const uint32_t key[3] = {tick, fileTimeLow, fileTimeHigh};
        colorState_[0] = 0x12bd6aau;
        for (uint32_t index = 1; index < colorState_.size(); ++index)
        {
            const uint32_t previous = colorState_[index - 1];
            colorState_[index] =
                (previous ^ (previous >> 30)) * 0x6c078965u + index;
        }

        uint32_t stateIndex = 1;
        uint32_t keyIndex = 0;
        for (uint32_t count = 0; count < colorState_.size(); ++count)
        {
            const uint32_t previous = colorState_[stateIndex - 1];
            colorState_[stateIndex] =
                (colorState_[stateIndex] ^
                 ((previous ^ (previous >> 30)) * 0x19660du)) +
                key[keyIndex] + keyIndex;
            if (++stateIndex >= colorState_.size())
            {
                colorState_[0] = colorState_.back();
                stateIndex = 1;
            }
            if (++keyIndex >= 3)
                keyIndex = 0;
        }
        for (uint32_t count = 0; count + 1 < colorState_.size(); ++count)
        {
            const uint32_t previous = colorState_[stateIndex - 1];
            colorState_[stateIndex] =
                (colorState_[stateIndex] ^
                 ((previous ^ (previous >> 30)) * 0x5d588b65u)) -
                stateIndex;
            if (++stateIndex >= colorState_.size())
            {
                colorState_[0] = colorState_.back();
                stateIndex = 1;
            }
        }
        colorState_[0] = 0x80000000u;
        colorIndex_ = static_cast<uint32_t>(colorState_.size());
    }

    void SeedColorFromClock();

    void FillColor(std::vector<uint32_t>& words)
    {
        for (uint32_t& word : words)
            word = NextColorWord();
    }

  private:
    float NextTargetValue()
    {
        randomState_ = randomState_ * 0x343fdu + 0x269ec3u;
        return static_cast<float>((randomState_ >> 16) & 0x7fffu) / 32767.0f;
    }

    uint32_t NextColorWord()
    {
        static constexpr uint32_t magic[2] = {0u, 0x9908b0dfu};
        if (colorIndex_ >= colorState_.size())
        {
            for (uint32_t index = 0; index + 1 < colorState_.size(); ++index)
            {
                const uint32_t mixed =
                    ((colorState_[index] ^ colorState_[index + 1]) &
                     0x7fffffffu) ^
                    colorState_[index];
                colorState_[index] =
                    colorState_[(index + 397) % colorState_.size()] ^
                    (mixed >> 1) ^ magic[mixed & 1u];
            }
            const uint32_t mixed =
                ((colorState_.back() ^ colorState_[0]) & 0x7fffffffu) ^
                colorState_.back();
            // E-148/FUN_4a4292c0: the final recurrence ANDs state[396] with
            // the shifted mixture before applying the magic word. This differs
            // from the textbook MT19937 recurrence.
            colorState_.back() =
                (colorState_[396] & (mixed >> 1)) ^ magic[mixed & 1u];
            colorIndex_ = 0;
        }
        uint32_t value = colorState_[colorIndex_++];
        value ^= value >> 11;
        value ^= (value & 0xff3a58adu) << 7;
        value ^= (value & 0xffffdf8cu) << 15;
        return value ^ (value >> 18);
    }

    uint32_t randomState_ = 1;
    std::array<uint32_t, 624> colorState_{};
    uint32_t colorIndex_ = 624;
};
