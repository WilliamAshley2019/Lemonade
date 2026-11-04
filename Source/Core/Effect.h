#pragma once
// The only thing the mixer knows about an insert effect. Hosted VST3s implement this today;
// a CLAP / built-in / DAW-native effect can implement it tomorrow without touching the mixer.

#include <string>

namespace lemon::core
{
class IEffect
{
public:
    virtual ~IEffect() = default;

    // Message thread. Must be cheap when called again with unchanged arguments.
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    // Audio thread. Process in place. keyL/keyR are non-null only if wantsKey() and a key source is routed.
    virtual void process(float* left, float* right, const float* keyL, const float* keyR, int n) noexcept = 0;

    virtual void reset() = 0;

    // Latency in samples. The engine reads it when it builds a snapshot (message thread).
    virtual int latency() const = 0;

    // True if the effect has a sidechain input.
    virtual bool wantsKey() const = 0;

    virtual std::string name() const = 0;
};
}
