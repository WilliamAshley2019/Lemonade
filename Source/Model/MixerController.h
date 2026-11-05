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
    bool quarantined = false;                 // crashed the host before; not loaded until the user says so
};

struct TrackModel
{
    juce::String name;
    bool toMaster = true;
    int hostOut = -1;
    int linkFrom = -1;                        // LemonSlice In lane feeding this track (0-based), -1 = none
    int linkTo   = -1;                        // LemonSlice Out lane this track feeds, -1 = none
    std::array<SlotModel, core::kMaxSlots> slots;
    std::vector<core::SendCfg> sends;
};

struct Model
{
    std::vector<TrackModel> tracks;
    std::array<int, core::kMaxHostIn> hostInputTrack { 1, 2, 3, 4, 5 };
    int linkMask = 1;                         // bit g = link group g+1 is active (several can be on at once)
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
    void setLinkFrom(int t, int lane);        // -1 = off
    bool setLinkTo(int t, int lane);          // -1 = off
    void setLinkMask(int mask);               // bits 0..3

    // Audio-thread view of the link assignments (plain atomics, no rebuild needed for 'from').
    struct LinkLive
    {
        std::atomic<int> mask { 1 };
        std::array<std::atomic<int>, core::kMaxTracks> from, to;
        LinkLive() { for (auto& a : from) a = -1; for (auto& a : to) a = -1; }
    };
    LinkLive& linkLive() noexcept { return linkLive_; }

    void loadPlugin(int t, int s, const juce::PluginDescription& d, bool force = false);
    void retryPlugin(int t, int s);           // user accepted the risk for a quarantined slot
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
    void instantiate(int t, int s, const juce::PluginDescription&, const juce::MemoryBlock& state, bool force);
    void notifyLatency();

    core::MixerEngine& engine;
    juce::AudioPluginFormatManager& formats;
    ForwardPlayHead& playHead;
    Model model_;
    LinkLive linkLive_;
    void syncLinkLive();
    int lastLatency = -1;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
}
