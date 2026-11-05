#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Mixer.h"
#include "../Hosting/HostedSlot.h"

namespace lemon
{
struct SlotModel
{
    bool hasDesc = false;
    juce::PluginDescription desc;
    juce::MemoryBlock pendingState;           // state to restore when (re)loading
    std::shared_ptr<HostedSlot> slot;         // null = empty, or plugin missing / loading
    int keySource = -1;
    bool missing = false;
    bool loading = false;
};

struct TrackModel
{
    juce::String name;
    bool toMaster = true;
    int hostOut = -1;
    std::array<SlotModel, core::kMaxSlots> slots;
    std::vector<core::SendCfg> sends;
};

struct Model
{
    std::vector<TrackModel> tracks;
    std::array<int, core::kMaxHostIn> hostInputTrack { 1, 2, 3, 4, 5 };
};

// Message-thread owner of the mixer topology. Every structural edit goes through apply():
// mutate a copy, build a MixerCfg, publish it; if the engine rejects it (routing loop...) the
// model is left unchanged and lastError says why.
class MixerController : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    MixerController(core::MixerEngine&, juce::AudioPluginFormatManager&, ForwardPlayHead&);
    ~MixerController() override;

    const Model& model() const noexcept { return model_; }
    int numTracks() const noexcept { return (int) model_.tracks.size(); }
    juce::String lastError;

    // Hooks (message thread)
    std::function<void()> onLatencyChanged;
    std::function<void(juce::AudioProcessor*)> onSlotWillClose;   // close the plugin's window here

    void prepareAll(double sampleRate, int maxBlock);   // called from prepareToPlay

    bool addTrack();
    bool removeLastTrack();
    void renameTrack(int t, const juce::String& name);
    bool setToMaster(int t, bool on);
    bool setHostOut(int t, int hostOut);

    void loadPlugin(int t, int s, const juce::PluginDescription& d);
    bool removePlugin(int t, int s);
    bool setKeySource(int t, int s, int sourceTrack);   // -1 clears

    bool addSend(int t, int dest, bool preFader);
    bool removeSend(int t, int k);
    bool setSendPre(int t, int k, bool pre);
    bool setSendDest(int t, int k, int dest);

    // State
    void save(juce::ValueTree& into) const;
    void restore(const juce::ValueTree& from);

    juce::AudioPluginInstance* instanceAt(int t, int s) const;

private:
    void timerCallback() override;
    core::MixerCfg buildCfg(const Model&) const;
    bool apply(const std::function<void(Model&)>& mutate);
    void instantiate(int t, int s, const juce::PluginDescription&, const juce::MemoryBlock& state);
    void notifyLatency();

    core::MixerEngine& engine;
    juce::AudioPluginFormatManager& formats;
    ForwardPlayHead& playHead;
    Model model_;
    int lastLatency = -1;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
}
