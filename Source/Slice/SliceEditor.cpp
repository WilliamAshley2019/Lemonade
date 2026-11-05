#include "SliceEditor.h"

namespace lemon
{
using namespace ui;

SliceEditor::SliceEditor(SliceProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&laf);
    auto setup = [this](juce::Slider& s, const char* tip)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14);
        s.setTooltip(tip);
        addAndMakeVisible(s);
    };
    setup(group, "Link group 1-4: must match the group set in Lemon Mixer Rack");
    setup(lane, "DAW lane number 1-16: matches FROM DAW / TO DAW on a Lemon Mixer Rack track");
    setup(gain, "Level trim");
    gA = std::make_unique<juce::SliderParameterAttachment>(*proc.apvts.getParameter("group"), group);
    lA = std::make_unique<juce::SliderParameterAttachment>(*proc.apvts.getParameter("lane"), lane);
    vA = std::make_unique<juce::SliderParameterAttachment>(*proc.apvts.getParameter("gain"), gain);
    group.setNumDecimalPlacesToDisplay(0);
    lane.setNumDecimalPlacesToDisplay(0);

    thru.setButtonText(SliceProcessor::isIn ? "PASS THRU" : "ADD TO TRACK");
    thru.setClickingTogglesState(true);
    thru.setTooltip(SliceProcessor::isIn ? "Keep this track's own audio going to the DAW mixer as well (normally off, so it is not heard twice)"
                                         : "Mix the rack's audio with this track's own audio instead of replacing it");
    tA = std::make_unique<juce::ButtonParameterAttachment>(*proc.apvts.getParameter("thru"), thru);
    addAndMakeVisible(thru);

    setSize(330, 190);
    startTimerHz(15);
}

SliceEditor::~SliceEditor() { setLookAndFeel(nullptr); }

void SliceEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    auto top = r.removeFromTop(34);
    lcdRect = top.removeFromRight(120);
    ledRect = top.removeFromRight(30).withSizeKeepingCentre(14, 14);
    r.removeFromTop(8);
    auto bottom = r.removeFromBottom(22);
    thru.setBounds(bottom.withSizeKeepingCentre(120, 20));
    const int w = r.getWidth() / 3;
    for (auto* s : { &group, &lane, &gain }) { s->setBounds(r.removeFromLeft(w).reduced(6, 0).withTrimmedTop(12)); }
}

void SliceEditor::timerCallback()
{
    const int st = proc.status.load();
    const float pk = proc.peak.load();
    if (st != shownStatus || std::abs(pk - shownPeak) > 0.01f) { shownStatus = st; shownPeak = pk; repaint(); }
}

void SliceEditor::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);
    brushedMetal(g, getLocalBounds().toFloat().reduced(2.f), 6.f, true, *tex);
    auto r = getLocalBounds().reduced(12);
    auto top = r.removeFromTop(34);
    g.setColour(col::amber);
    g.setFont(caps(16.f));
    g.drawText(proc.getName().toUpperCase(), top.removeFromLeft(170), juce::Justification::centredLeft);

    const bool in = SliceProcessor::isIn;
    juce::Colour led = shownStatus == 1 ? col::perfect : shownStatus == 2 ? col::wrong : col::amberDim;
    glow(g, ledRect.toFloat(), led, 6.f, shownStatus > 0 ? 0.6f : 0.f);
    g.setColour(led); g.fillEllipse(ledRect.toFloat());

    glassWell(g, lcdRect.toFloat(), 4.f);
    juce::String t = shownStatus == 2 ? "LANE TAKEN"
                   : shownStatus == 1 ? (in ? "TO RACK" : "FROM RACK")
                   : (in ? "NO LINK" : "WAITING");
    tag(g, t, lcdRect.toFloat(), juce::Justification::centred, 9.f, shownStatus == 2 ? col::wrong : col::amber);

    auto c = getLocalBounds().reduced(12);
    c.removeFromTop(42);
    const int w = c.getWidth() / 3;
    const char* names[] = { "LINK GROUP", "DAW LANE", "GAIN" };
    for (int i = 0; i < 3; ++i)
        tag(g, names[i], juce::Rectangle<float>((float) (c.getX() + i * w), (float) c.getY(), (float) w, 12.f), juce::Justification::centred, 8.f, col::textDim);

    if (!in)
    {
        const int u = proc.underruns.load();
        if (u > 0) tag(g, "DROPOUTS " + juce::String(u), juce::Rectangle<float>(12.f, (float) getHeight() - 16.f, 120.f, 12.f), juce::Justification::centredLeft, 7.5f, col::textFaint);
    }
}
}
