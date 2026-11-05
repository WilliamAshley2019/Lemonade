#pragma once
// LemonSlice In / LemonSlice Out - partner plugins for Lemon Mixer Rack.
// One source, two plugins, chosen at compile time with LEMON_SLICE_MODE (0 = In, 1 = Out).
//
//   LemonSlice In  : put on a DAW mixer track. Sends that track's audio to lane N, which a
//                    Lemon Mixer Rack track set to "FROM DAW N" picks up.
//   LemonSlice Out : put on a DAW mixer track. Plays back whatever the Lemon Mixer Rack track set
//                    to "TO DAW N" is producing, replacing (or adding to) the track's audio.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Link/LinkBus.h"

#ifndef LEMON_SLICE_MODE
 #define LEMON_SLICE_MODE 0
#endif

namespace lemon
{
class SliceProcessor : public juce::AudioProcessor
{
public:
    static constexpr bool isIn = (LEMON_SLICE_MODE == 0);

    SliceProcessor();
    ~SliceProcessor() override;

    void prepareToPlay(double sr, int block) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override
    { return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return isIn ? "LemonSlice In" : "LemonSlice Out"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI status (written by the audio thread, read by the editor timer)
    std::atomic<int> status { 0 };          // 0 = idle/no partner, 1 = linked, 2 = lane taken by another In
    std::atomic<int> underruns { 0 };
    std::atomic<int> marginFrames { 0 };
    std::atomic<float> peak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    link::Segment* segment = nullptr;
    link::Writer writer;
    link::Reader reader;
    uint32_t id = 0;
    int curGroup = -1, curLane = -1;
    double sampleRate = 48000.0;
    int blockCap = 0;
    std::vector<float> bufL, bufR;
    std::atomic<float>* pGroup = nullptr; std::atomic<float>* pLane = nullptr;
    std::atomic<float>* pGain = nullptr;  std::atomic<float>* pThru = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceProcessor)
};
}
