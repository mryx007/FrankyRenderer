// Field order from the picture (GitHub issue 53): comb measurement and votes.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Renderer/FieldOrderAnalysis.h"
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr int W = 160, H = 120;

// A picture whose content moves `speed` pixels per field to the right, with
// vertical detail, as 8-bit RGBA. Rows of field `parity` are taken at `time`.
void Fill(std::vector<uint8_t>& frame, int parity, double time, double speed)
{
    for (int y = parity; y < H; y += 2)
        for (int x = 0; x < W; x++)
        {
            const double u = x - time * speed;
            const double value = 128 + 60 * std::sin(u * .35) + 40 * std::sin(y * .5 + u * .05);
            for (int c = 0; c < 4; c++)
                frame[(y * W + x) * 4 + c] = uint8_t(std::lround(std::clamp(value, 0., 255.)));
        }
}

// Frames of interlaced video: frame n shows its first field at time 2n and the
// other at 2n+1.
std::vector<std::vector<uint8_t>> Video(bool topFirst, double speed, int frames)
{
    std::vector<std::vector<uint8_t>> video(frames, std::vector<uint8_t>(W * H * 4));
    for (int n = 0; n < frames; n++)
    {
        Fill(video[n], topFirst ? 0 : 1, 2 * n, speed);
        Fill(video[n], topFirst ? 1 : 0, 2 * n + 1, speed);
    }
    return video;
}

int Decide(const std::vector<std::vector<uint8_t>>& video)
{
    FieldOrderVotes votes;
    for (size_t n = 0; n + 1 < video.size(); n++)
        votes.Observe(FieldComb(video[n].data(), video[n + 1].data(), W, H, W * 4, 4, 1),
                      FieldComb(video[n + 1].data(), video[n].data(), W, H, W * 4, 4, 1));
    return votes.decided;
}

bool Check(bool ok, const char* text)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", text);
    return ok;
}
}

int main()
{
    bool ok = true;
    ok &= Check(Decide(Video(true, 2, 20)) == 1, "moving top field first video is detected as top field first");
    ok &= Check(Decide(Video(false, 2, 20)) == 2, "moving bottom field first video is detected as bottom field first");
    ok &= Check(Decide(Video(true, 0, 30)) == 0, "a still picture decides nothing");
    ok &= Check(Decide(Video(true, 2, 3)) == 0, "two frame pairs decide nothing");
    ok &= Check(Decide(Video(false, 2, 4)) == 2, "three unanimous frame pairs decide at once");
    {
        // A dissenting pair among the first ones: the majority rule applies.
        FieldOrderVotes votes;
        votes.Observe(1, 2);
        votes.Observe(2, 1);
        for (int i = 0; i < 10; i++)
            votes.Observe(1, 2);
        ok &= Check(votes.decided == 0, "a dissenting early pair prevents the quick decision");
        votes.Observe(1, 2);
        votes.Observe(1, 2);
        ok &= Check(votes.decided == 1, "then twelve of sixteen pairs decide");
    }
    {
        // Once decided, a few contrary pairs do not switch.
        FieldOrderVotes votes;
        for (int i = 0; i < 16; i++)
            votes.Observe(1, 2);
        for (int i = 0; i < 6; i++)
            votes.Observe(2, 1);
        ok &= Check(votes.decided == 1, "a few contrary pairs do not switch a decided order");
    }
    {
        // A change of order (another channel) is followed once enough pairs agree.
        FieldOrderVotes votes;
        const auto top = Video(true, 2, 20), bottom = Video(false, 2, 20);
        for (const auto* video : {&top, &bottom})
            for (size_t n = 0; n + 1 < video->size(); n++)
                votes.Observe(FieldComb((*video)[n].data(), (*video)[n + 1].data(), W, H, W * 4, 4, 1),
                              FieldComb((*video)[n + 1].data(), (*video)[n].data(), W, H, W * 4, 4, 1));
        ok &= Check(votes.decided == 2, "a later change of field order is followed");
    }
    {
        // Noise around a still picture: no clear difference, no vote.
        FieldOrderVotes votes;
        for (int i = 0; i < 40; i++)
            votes.Observe(5.0 + (i % 3) * .05, 5.0 + (i % 2) * .05);
        ok &= Check(votes.decided == 0, "small differences do not vote");
        votes.Observe(NAN, 3);
        ok &= Check(votes.decided == 0, "invalid measurements are ignored");
    }
    return ok ? 0 : 1;
}
