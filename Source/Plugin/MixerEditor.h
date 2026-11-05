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
    PluginWindowManager windows;                 

    juce::StringArray trackNames() const;
    bool isDocked(int track, int slot) const { return dock.hasSlot(track, slot); }
    void openPluginUi(int track, int slot);      
    void showPluginPicker(juce::Component& anchor, int track, int slot);
    void askRetryFlagged(int track, int slot);
    void showLanePicker(juce::Component& anchor, int current, std::function<void(int)> onPick);   // lane 1-16 grid, NONE = -1
    void showTrackPicker(juce::Component& anchor, const std::vector<int>& choices, int current,
                         const juce::String& noneLabel, std::function<void(int)> onPick);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void handleAsyncUpdate() override { rebuild(); }
    void requestRebuild() { triggerAsyncUpdate(); }   
    void rebuild();
    void startScan();
    void startImport();
    void locateFlDatabase();
    void linkOrganizerDb();
    void dockChanged();
    void layoutAll();

    lemon::ui::AlumLookAndFeel laf;                      
    juce::TooltipWindow tooltip { this, 500 };

    std::array<juce::TextButton, 4> groupB;
    juce::TextButton allGroupsB { "ALL" }, compactB { "COMPACT" };
    void syncGroupButtons();
    bool sized = false;
    juce::TextButton scanButton { "SCAN" }, addTrackButton { "+ TRACK" }, removeTrackButton { "- TRACK" };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
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
