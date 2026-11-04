#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Effect.h"

namespace lemon
{
// Forwards the outer host's transport to hosted plugins. The outer processor publishes its
// playhead for the duration of each processBlock.
class ForwardPlayHead : public juce::AudioPlayHead
{
public:
    void set(juce::AudioPlayHead* p) noexcept { current.store(p); }
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (auto* p = current.load()) return p->getPosition();
        return {};
    }
private:
    std::atomic<juce::AudioPlayHead*> current { nullptr };
};

// Adapts a hosted audio plugin (stereo in/out, optional stereo sidechain) to core::IEffect.
class HostedSlot : public core::IEffect
{
public:
    // Returns null and fills `error` if the plugin cannot be used as a stereo effect.
    static std::shared_ptr<HostedSlot> create(std::unique_ptr<juce::AudioPluginInstance> instance,
                                              const juce::MemoryBlock* state, juce::String& error);

    ~HostedSlot() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* l, float* r, const float* keyL, const float* keyR, int n) noexcept override;
    void reset() override { if (plugin) plugin->reset(); }
    int  latency() const override { return plugin ? plugin->getLatencySamples() : 0; }
    bool wantsKey() const override { return hasKey; }
    std::string name() const override { return plugin ? plugin->getName().toStdString() : std::string(); }

    juce::AudioPluginInstance& instance() noexcept { return *plugin; }
    void setPlayHead(juce::AudioPlayHead* p) { plugin->setPlayHead(p); }

private:
    HostedSlot() = default;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    bool hasKey = false;
    int numChannels = 2;
};
}
