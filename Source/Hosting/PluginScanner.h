#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace lemon
{
// Owns the format manager + known-plugin list, persists the list, scans on a background thread
// with a dead-man's-pedal file so a crashing plugin is blacklisted on the next scan.
class PluginScanner : private juce::Thread
{
public:
    PluginScanner();
    ~PluginScanner() override;

    juce::AudioPluginFormatManager& formats() noexcept { return formatManager; }
    juce::KnownPluginList& list() noexcept { return known; }

    bool isScanning() const noexcept { return isThreadRunning(); }
    // folders empty -> default VST3 locations. Callbacks run on the message thread.
    void scan(const juce::StringArray& folders, std::function<void(const juce::String&)> progress,
              std::function<void()> done);

    static juce::File dataFolder();

private:
    void run() override;
    void save();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList known;
    juce::StringArray foldersToScan;
    std::function<void(const juce::String&)> onProgress;
    std::function<void()> onDone;
};
}
