#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "../Hosting/PluginScanner.h"

namespace lemon::ui
{
// Scrollable, searchable plugin list shown in a call-out. Click a row to load it. Rows are enriched with
// FL Studio's database (favourite folders, categories, vendor) and flagged when a plugin crashed the host.
class PluginPicker : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    PluginPicker(PluginScanner&, std::function<void(const CatalogEntry&)> onPick, std::function<void()> onScan,
                 std::function<void()> onImportFl, std::function<void()> onLocateFl, std::function<void()> onOrganizer);
    void resized() override;
    void paint(juce::Graphics&) override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void returnKeyPressed(int row) override { choose(row); }
    void timerCallback() override;
    void refresh();
    void choose(int row);

    PluginScanner& scanner;
    std::function<void(const CatalogEntry&)> pick;
    std::function<void()> scanCb, importCb, locateCb, orgCb;
    std::vector<CatalogEntry> shown;
    const FlData* lastFl = nullptr;
    int lastKnown = -1, lastQuarantine = -1;

    juce::TextEditor filter;
    juce::ListBox listBox { {}, this };
    juce::TextButton favOnly { "FL FAVOURITES" }, hideFlagged { "HIDE FLAGGED" };
    juce::TextButton scanButton { "SCAN FOLDERS" }, importButton { "IMPORT FROM FL" }, locateButton { "FL DB..." }, orgButton { "ORG DB..." };
    juce::Label status;
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
};
}
