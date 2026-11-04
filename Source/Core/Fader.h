#pragma once
// Fader / balance maths. Fader position is 0..1 with 0.8 = unity (0 dB) and 1.0 = +6 dB.

#include <algorithm>
#include <cmath>

#include "Common.h"

namespace lemon::core::fader
{
inline float positionToDb(float p) noexcept
{
    if (p <= 0.0005f)
        return -120.0f;
    if (p >= 0.8f)
        return (std::min(p, 1.0f) - 0.8f) / 0.2f * 6.0f;
    const float x = (0.8f - p) / 0.8f;           // 0 at unity .. 1 at the bottom
    return -70.0f * std::pow(x, 1.6f);
}

inline float positionToGain(float p) noexcept
{
    if (p <= 0.0005f)
        return 0.0f;
    return std::pow(10.0f, positionToDb(p) * 0.05f);
}

// Balance law for stereo material: centre is unity, the opposite side is attenuated (equal-power).
inline void balance(float pan, float& gainL, float& gainR) noexcept
{
    pan = std::min(1.0f, std::max(-1.0f, pan));
    if (pan >= 0.0f) { gainL = std::cos(pan * kHalfPi);  gainR = 1.0f; }
    else             { gainL = 1.0f;                     gainR = std::cos(-pan * kHalfPi); }
}
}
