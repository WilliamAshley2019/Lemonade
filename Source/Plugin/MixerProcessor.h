#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Mixer.h"
#include "../Hosting/PluginScanner.h"
#include "../Model/MixerController.h"

namespace lemon
{
class MixerProcessor : public juce::AudioProcessor, private juce::AudioProcessorParameter::Listener
{
public:
    MixerProcessor();
    ~MixerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Lemon Mixer Rack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    core::MixerEngine& getEngine() noexcept { return engine; }
    MixerController& getController() noexcept { return *controller; }
    PluginScanner& getScanner() noexcept { return scanner; }

    // Host-automatable parameters (track 0 = master).
    juce::AudioParameterFloat* volumeParam(int t) const { return volume[(size_t) t]; }
    juce::AudioParameterFloat* panParam(int t) const { return pan[(size_t) t]; }
    juce::AudioParameterBool* muteParam(int t) const { return mute[(size_t) t]; }
    juce::AudioParameterBool* soloParam(int t) const { return solo[(size_t) t]; }

private:
    static BusesProperties makeBuses();
    void parameterValueChanged(int index, float) override;
    void parameterGestureChanged(int, bool) override {}
    void pushParam(int index);

    core::MixerEngine engine;
    ForwardPlayHead playHead;
    PluginScanner scanner;
    std::unique_ptr<MixerController> controller;

    std::array<juce::AudioParameterFloat*, core::kMaxTracks> volume {}, pan {};
    std::array<juce::AudioParameterBool*, core::kMaxTracks> mute {}, solo {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerProcessor)
};
}
