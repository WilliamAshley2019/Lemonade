#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace lemon
{
class MixerEditor;

namespace ui
{
// One mixer channel. Everything is a visible control (no hidden menus); every control has a caption.
class TrackStrip : public juce::Component
{
public:
    static constexpr int width = 164;

    TrackStrip(MixerEditor&, int track);
    ~TrackStrip() override;

    int idealHeight() const noexcept { return height; }
    void updateMeter();
    void paint(juce::Graphics&) override;

    struct SlotCell;
    struct SendWidgets;

private:
    void layout();
    void showError();

    MixerEditor& ed;
    const int track;
    int height = 100;

    juce::Label name;
    std::vector<std::unique_ptr<SlotCell>> cells;
    std::vector<std::unique_ptr<SendWidgets>> sends;
    juce::TextButton addSend { "+ SEND" };
    juce::TextButton mstButton { "MST" };
    std::array<juce::TextButton, 4> hostOut;
    juce::TextButton linkFromB { "FROM DAW" }, linkToB { "TO DAW" };
    std::array<juce::Slider, 3> eq;
    juce::Slider pan, widthKnob, fader;
    juce::TextButton muteB { "MUTE" }, soloB { "SOLO" }, polB { "PHASE" };
    std::unique_ptr<juce::SliderParameterAttachment> panAtt, faderAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> muteAtt, soloAtt;

    struct Caption { juce::String text; juce::Rectangle<int> rect; };
    std::vector<Caption> captions;
    juce::String keyText;
    juce::Rectangle<int> keyRect, lcdRect, meterRect;
    float meterL = 0.0f, meterR = 0.0f;
    juce::SharedResourcePointer<Textures> tex;
};
}
}
