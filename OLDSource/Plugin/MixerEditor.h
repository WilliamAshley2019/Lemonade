#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MixerProcessor.h"
#include "PluginWindowManager.h"
#include "../UI/Theme.h"
#include "../UI/PluginDock.h"
#include "../UI/TrackStrip.h"

namespace lemon
{
class MixerEditor : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer, private juce::AsyncUpdater
{
public:
    explicit MixerEditor(MixerProcessor&);
    ~MixerEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override;

    // ---- used by the strips ----
    MixerProcessor& proc;
    PluginWindowManager windows;                 // floating plugin windows (the dock handles docked ones)

    juce::StringArray trackNames() const;
    bool isDocked(int track, int slot) const { return dock.hasSlot(track, slot); }
    void openPluginUi(int track, int slot);      // dock (or re-show) the plugin's GUI
    void showPluginPicker(juce::Component& anchor, int track, int slot);
    void showTrackPicker(juce::Component& anchor, const std::vector<int>& choices, int current,
                         const juce::String& noneLabel, std::function<void(int)> onPick);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void handleAsyncUpdate() override { rebuild(); }
    void requestRebuild() { triggerAsyncUpdate(); }   // never rebuild inside a strip's own callback
    void rebuild();
    void startScan();
    void dockChanged();
    void layoutAll();

    lemon::ui::AlumLookAndFeel laf;                      // declared first: outlives every child
    juce::TooltipWindow tooltip { this, 500 };

    juce::TextButton scanButton { "SCAN" }, addTrackButton { "+ TRACK" }, removeTrackButton { "- TRACK" };
    juce::Label status;
    juce::Viewport viewport;
    juce::Component strips;
    std::vector<std::unique_ptr<lemon::ui::TrackStrip>> stripList;
    lemon::ui::PluginDock dock;

    juce::Rectangle<int> latencyLcd, pluginsLcd, headerRect;
    int dockHeight = 0;
    int lastLatencyShown = -1, lastPluginsShown = -1;
    bool inLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerEditor)
};
}
