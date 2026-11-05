#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Mixer.h"
#include "../Hosting/PluginScanner.h"
#include "../Model/MixerController.h"
#include "../Link/LinkBus.h"

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

    std::atomic<bool> compactUi { false };            // editor preference, saved with the project
    core::MixerEngine& getEngine() noexcept { return engine; }
    MixerController& getController() noexcept { return *controller; }
    PluginScanner& getScanner() noexcept { return scanner; }

    // Hostautomatable parameters (track 0 = master).
    juce::AudioParameterFloat* volumeParam(int t) const { return volume[(size_t) t]; }
    juce::AudioParameterFloat* panParam(int t) const { return pan[(size_t) t]; }
    juce::AudioParameterBool* muteParam(int t) const { return mute[(size_t) t]; }
    juce::AudioParameterBool* soloParam(int t) const { return solo[(size_t) t]; }

private:
    static BusesProperties makeBuses();
    void parameterValueChanged(int index, float) override;
    void parameterGestureChanged(int, bool) override {}
    void pushParam(int index);

    // Lemon link (LemonSlice In Out) 
    struct LinkTrack
    {
        std::array<link::Reader, link::kGroups> reader;
        std::array<link::Writer, link::kGroups> writer;
        int from = -2, to = -2, mask = -2;           // what is currently attached (-2 = nothing yet)
        std::vector<float> inL, inR, tapL, tapR;
    };
    void prepareLink(double sr, int block);
    void serviceLinkIn(core::IO&, int n);
    void serviceLinkOut(int n);
    link::Segment* segment = nullptr;
    std::array<LinkTrack, core::kMaxTracks> linkTracks;
    int linkCap = 0;
    std::vector<float> scratchL, scratchR;
    double linkRate = 48000.0;
    uint32_t linkId = 0;

public:
    int linkUnderruns() const noexcept;           // UI dropouts on the FROM-DAW lanes since attach
    bool linkAvailable() const noexcept { return segment != nullptr; }
private:
    core::MixerEngine engine;
    ForwardPlayHead playHead;
    std::shared_ptr<PluginScanner> scannerHolder = PluginScanner::acquire();   // shared by all instances in this process
    PluginScanner& scanner = *scannerHolder;
    std::unique_ptr<MixerController> controller;

    std::array<juce::AudioParameterFloat*, core::kMaxTracks> volume {}, pan {};
    std::array<juce::AudioParameterBool*, core::kMaxTracks> mute {}, solo {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerProcessor)
};
}
