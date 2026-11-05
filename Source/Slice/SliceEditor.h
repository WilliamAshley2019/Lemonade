#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SliceProcessor.h"
#include "../UI/Theme.h"

namespace lemon
{
class SliceEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SliceEditor(SliceProcessor&);
    ~SliceEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    SliceProcessor& proc;
    ui::AlumLookAndFeel laf;
    juce::SharedResourcePointer<ui::Textures> tex;
    juce::Slider group, lane, gain;
    juce::TextButton thru;
    std::unique_ptr<juce::SliderParameterAttachment> gA, lA, vA;
    std::unique_ptr<juce::ButtonParameterAttachment> tA;
    juce::Rectangle<int> ledRect, lcdRect;
    int shownStatus = -1;
    float shownPeak = 0.0f;
};
}
