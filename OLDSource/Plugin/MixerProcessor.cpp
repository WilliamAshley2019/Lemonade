#include "MixerProcessor.h"
#include "MixerEditor.h"
#include "../Core/Fader.h"

namespace lemon
{
using namespace core;

juce::AudioProcessor::BusesProperties MixerProcessor::makeBuses()
{
    using juce::AudioChannelSet;
    BusesProperties p;
    p = p.withInput("Input", AudioChannelSet::stereo(), true);
    for (int i = 1; i < kMaxHostIn; ++i)
        p = p.withInput("Input " + juce::String(i + 1), AudioChannelSet::stereo(), false);
    p = p.withOutput("Master", AudioChannelSet::stereo(), true);
    for (int i = 0; i < kMaxHostOut; ++i)
        p = p.withOutput("Out " + juce::String(i + 1), AudioChannelSet::stereo(), false);
    return p;
}

MixerProcessor::MixerProcessor() : AudioProcessor(makeBuses())
{
    for (int t = 0; t < kMaxTracks; ++t)
    {
        const juce::String nm = t == 0 ? "Master" : "Track " + juce::String(t);
        const auto id = [t](const char* s) { return juce::ParameterID { "t" + juce::String(t) + "_" + s, 1 }; };

        auto volAttr = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(fader::positionToDb(v), 1) + " dB"; });
        addParameter(volume[(size_t) t] = new juce::AudioParameterFloat(id("vol"), nm + " Volume",
                         juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f, volAttr));
        addParameter(pan[(size_t) t]    = new juce::AudioParameterFloat(id("pan"), nm + " Pan",
                         juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
        addParameter(mute[(size_t) t]   = new juce::AudioParameterBool(id("mute"), nm + " Mute", false));
        addParameter(solo[(size_t) t]   = new juce::AudioParameterBool(id("solo"), nm + " Solo", false));
    }
    for (auto* p : getParameters())
    {
        p->addListener(this);
    }

    controller = std::make_unique<MixerController>(engine, scanner.formats(), playHead);
    controller->onLatencyChanged = [this] { setLatencySamples(engine.latency()); };
    for (int i = 0; i < getParameters().size(); ++i) pushParam(i);
}

MixerProcessor::~MixerProcessor()
{
    for (auto* p : getParameters())
        p->removeListener(this);
    controller.reset();     // releases hosted plugins while the engine is still alive
}

void MixerProcessor::pushParam(int index)
{
    const int t = index / 4, kind = index % 4;
    if (t >= kMaxTracks) return;
    auto& lv = engine.live(t);
    switch (kind)
    {
        case 0: lv.volume = volume[(size_t) t]->get(); break;
        case 1: lv.pan = pan[(size_t) t]->get(); break;
        case 2: lv.mute = mute[(size_t) t]->get(); break;
        default: lv.solo = solo[(size_t) t]->get(); break;
    }
}

void MixerProcessor::parameterValueChanged(int index, float) { pushParam(index); }

void MixerProcessor::prepareToPlay(double sr, int block)
{
    controller->prepareAll(sr, block);
    setLatencySamples(engine.latency());
}

bool MixerProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto st = juce::AudioChannelSet::stereo();
    if (l.getMainOutputChannelSet() != st) return false;
    for (auto& b : l.inputBuses)  if (! b.isDisabled() && b != st) return false;
    for (auto& b : l.outputBuses) if (! b.isDisabled() && b != st) return false;
    return true;
}

void MixerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    playHead.set(getPlayHead());

    IO io;
    for (int b = 0; b < kMaxHostIn; ++b)
        if (auto* bus = getBus(true, b); bus != nullptr && bus->isEnabled())
        {
            auto ib = getBusBuffer(buffer, true, b);
            if (ib.getNumChannels() >= 1)
            {
                io.inL[b] = ib.getReadPointer(0);
                io.inR[b] = ib.getNumChannels() > 1 ? ib.getReadPointer(1) : io.inL[b];
            }
        }
    {
        auto ob = getBusBuffer(buffer, false, 0);
        if (ob.getNumChannels() >= 2) { io.outL = ob.getWritePointer(0); io.outR = ob.getWritePointer(1); }
    }
    for (int k = 0; k < kMaxHostOut; ++k)
        if (auto* bus = getBus(false, 1 + k); bus != nullptr && bus->isEnabled())
        {
            auto ob = getBusBuffer(buffer, false, 1 + k);
            if (ob.getNumChannels() >= 2) { io.auxOutL[k] = ob.getWritePointer(0); io.auxOutR[k] = ob.getWritePointer(1); }
        }

    engine.process(io, buffer.getNumSamples());
    playHead.set(nullptr);
}

juce::AudioProcessorEditor* MixerProcessor::createEditor() { return new MixerEditor(*this); }

void MixerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree root("LemonMixerRack");
    root.setProperty("version", 1, nullptr);
    juce::ValueTree params("Params");
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
            params.setProperty(r->getParameterID(), r->getValue(), nullptr);
    root.addChild(params, -1, nullptr);
    controller->save(root);
    juce::MemoryOutputStream os(dest, false);
    root.writeToStream(os);
}

void MixerProcessor::setStateInformation(const void* data, int size)
{
    auto root = juce::ValueTree::readFromData(data, (size_t) size);
    if (! root.isValid() || ! root.hasType("LemonMixerRack"))
        return;

    // Strip the params node so the track list is exactly the Track children.
    auto params = root.getChildWithName("Params");
    if (params.isValid())
    {
        for (auto* p : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
                if (params.hasProperty(r->getParameterID()))
                    r->setValueNotifyingHost((float) (double) params.getProperty(r->getParameterID()));
        root.removeChild(params, nullptr);
    }
    controller->restore(root);
    setLatencySamples(engine.latency());
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new lemon::MixerProcessor(); }
