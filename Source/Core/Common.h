#pragma once
// Lemon Mixer core — shared constants. No JUCE dependency anywhere under Source/Core.

namespace lemon::core
{
constexpr int kMaxTracks  = 17;   // track 0 = Master, 1..16 = inserts
constexpr int kMaxSlots   = 8;    // effect slots per track
constexpr int kMaxSends   = 8;    // sends per track
constexpr int kMaxHostIn  = 5;    // host input buses (main + 4 aux)
constexpr int kMaxHostOut = 4;    // host aux output buses (besides the main output)
constexpr int kEqBands    = 3;    // low shelf / peak / high shelf

constexpr double kPi     = 3.14159265358979323846;
constexpr float  kHalfPi = 1.57079632679489661923f;
}
