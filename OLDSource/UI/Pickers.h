#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace lemon::ui
{
// Scrollable, searchable plugin list shown in a call-out. Click a row to load it.
class PluginPicker : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    PluginPicker(juce::KnownPluginList& list, std::function<void(const juce::PluginDescription&)> onPick,
                 std::function<void()> onScan);
    ~PluginPicker() override;
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override { choose(row); }
    void returnKeyPressed(int row) override { choose(row); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void choose(int row);

    juce::KnownPluginList& known;
    std::function<void(const juce::PluginDescription&)> pick;
    std::vector<juce::PluginDescription> shown;
    juce::TextEditor filter;
    juce::ListBox listBox { {}, this };
    juce::TextButton scanButton { "SCAN" };
    juce::Label count;
};

// A grid of visible track buttons (replaces drop-down menus for send targets / sidechain sources).
class TrackPicker : public juce::Component
{
public:
    TrackPicker(const juce::StringArray& trackNames, const std::vector<int>& choices, int current,
                const juce::String& noneLabel, std::function<void(int)> onPick);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    juce::String title;
};
}
