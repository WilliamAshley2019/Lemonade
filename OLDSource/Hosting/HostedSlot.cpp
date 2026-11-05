#include "HostedSlot.h"

namespace lemon
{
using juce::AudioChannelSet;

std::shared_ptr<HostedSlot> HostedSlot::create(std::unique_ptr<juce::AudioPluginInstance> inst,
                                               const juce::MemoryBlock* state, juce::String& error)
{
    if (! inst) { error = "Plugin failed to load"; return {}; }

    if (inst->getBusCount(true) < 1 || inst->getBusCount(false) < 1)
    {
        error = inst->getName() + " has no audio input or output (instruments cannot be used as inserts)";
        return {};
    }

    auto build = [&](bool withKey)
    {
        juce::AudioProcessor::BusesLayout l = inst->getBusesLayout();
        for (int i = 0; i < inst->getBusCount(true); ++i)
            l.inputBuses.set(i, (i == 0 || (i == 1 && withKey)) ? AudioChannelSet::stereo() : AudioChannelSet::disabled());
        for (int i = 0; i < inst->getBusCount(false); ++i)
            l.outputBuses.set(i, i == 0 ? AudioChannelSet::stereo() : AudioChannelSet::disabled());
        return l;
    };

    bool key = false;
    auto layout = build(inst->getBusCount(true) > 1);
    if (inst->getBusCount(true) > 1 && inst->checkBusesLayoutSupported(layout))
        key = true;
    else
        layout = build(false);

    if (! inst->checkBusesLayoutSupported(layout) || ! inst->setBusesLayout(layout))
    {
        error = inst->getName() + " does not support a stereo layout";
        return {};
    }
    key = key && inst->getBus(true, 1) != nullptr && inst->getBus(true, 1)->isEnabled();

    if (state != nullptr && state->getSize() > 0)
        inst->setStateInformation(state->getData(), static_cast<int>(state->getSize()));

    std::shared_ptr<HostedSlot> s(new HostedSlot());
    s->plugin = std::move(inst);
    s->hasKey = key;
    s->numChannels = key ? 4 : 2;
    return s;
}

HostedSlot::~HostedSlot()
{
    if (plugin)
    {
        plugin->setPlayHead(nullptr);
        plugin->releaseResources();
    }
}

void HostedSlot::prepare(double sr, int maxBlock)
{
    plugin->releaseResources();
    plugin->setRateAndBufferSizeDetails(sr, maxBlock);
    plugin->prepareToPlay(sr, maxBlock);
    buffer.setSize(numChannels, maxBlock, false, true, false);
    midi.ensureSize(256);
    midi.clear();
}

void HostedSlot::process(float* l, float* r, const float* kl, const float* kr, int n) noexcept
{
    float* const* ch = buffer.getArrayOfWritePointers();
    std::memcpy(ch[0], l, sizeof(float) * (size_t) n);
    std::memcpy(ch[1], r, sizeof(float) * (size_t) n);
    if (hasKey)
    {
        if (kl != nullptr && kr != nullptr)
        {
            std::memcpy(ch[2], kl, sizeof(float) * (size_t) n);
            std::memcpy(ch[3], kr, sizeof(float) * (size_t) n);
        }
        else
        {
            std::memset(ch[2], 0, sizeof(float) * (size_t) n);
            std::memset(ch[3], 0, sizeof(float) * (size_t) n);
        }
    }

    juce::AudioBuffer<float> view(ch, numChannels, n);
    midi.clear();
    plugin->processBlock(view, midi);

    std::memcpy(l, ch[0], sizeof(float) * (size_t) n);
    std::memcpy(r, ch[1], sizeof(float) * (size_t) n);
}
}
