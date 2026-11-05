#include "MixerProcessor.h"
#include "../Hosting/HostLog.h"
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
    static std::atomic<uint32_t> instanceCounter { 0 };
    linkId = (((instanceCounter.fetch_add(1) + 1) & 0xffu) << 24) | ((juce::Time::getMillisecondCounter() & 0x1ffffu) << 7);
    if (linkId == 0) linkId = 128;
    segment = link::openSegment();
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
    controller->onLatencyChanged = [this]
    {
        hostLog("latency -> " + juce::String(engine.latency()) + " (telling the host)");
        setLatencySamples(engine.latency());
    };
    for (int i = 0; i < getParameters().size(); ++i) pushParam(i);
}

MixerProcessor::~MixerProcessor()
{
    hostLog("rack destroy begin");
    for (auto* p : getParameters())
        p->removeListener(this);
    controller.reset();     
    engine.shutdown();      
    for (auto& lt : linkTracks) { for (auto& w : lt.writer) w.release(); for (auto& r : lt.reader) r.release(); }
    if (segment) link::closeSegment();
    hostLog("rack destroy end");
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

void MixerProcessor::prepareLink(double sr, int block)
{
    linkRate = sr;
    linkCap = std::max(block, 64);
    scratchL.assign((size_t) linkCap, 0.0f); scratchR.assign((size_t) linkCap, 0.0f);
    for (auto& lt : linkTracks)
    {
        lt.inL.assign((size_t) linkCap, 0.0f); lt.inR.assign((size_t) linkCap, 0.0f);
        lt.tapL.assign((size_t) linkCap, 0.0f); lt.tapR.assign((size_t) linkCap, 0.0f);
        for (auto& w : lt.writer) w.release();
        for (auto& r : lt.reader) r.release();
        lt.from = lt.to = lt.mask = -2;            
    }
}

int MixerProcessor::linkUnderruns() const noexcept
{
    int n = 0;
    for (auto& lt : linkTracks) for (auto& r : lt.reader) n += r.underruns();
    return n;
}

 void MixerProcessor::serviceLinkIn(core::IO& io, int n)
{
    if (!segment || n > linkCap) return;
    auto& L = controller->linkLive();
    const int mask = L.mask.load(std::memory_order_relaxed);
    for (int t = 0; t < kMaxTracks; ++t)
    {
        auto& lt = linkTracks[(size_t) t];
        const int from = L.from[(size_t) t].load(std::memory_order_relaxed);
        const int to   = L.to[(size_t) t].load(std::memory_order_relaxed);
        if (lt.mask != mask || lt.from != from || lt.to != to)
        {
            for (int g = 0; g < link::kGroups; ++g)
            {
                const bool on = (mask >> g) & 1;
                lt.reader[(size_t) g].release();
                lt.writer[(size_t) g].release();
                if (on && from >= 0 && from < link::kLanes) lt.reader[(size_t) g].attach(segment, g, from, link::toRack, linkRate, n);
                if (on && to >= 0 && to < link::kLanes)
                    lt.writer[(size_t) g].attach(segment, g, to, link::toDaw, linkId + (uint32_t) (t * 4 + g), linkRate);
            }
            lt.mask = mask; lt.from = from; lt.to = to;
        }
        if (from >= 0)
        {
            std::fill(lt.inL.begin(), lt.inL.begin() + n, 0.0f);
            std::fill(lt.inR.begin(), lt.inR.begin() + n, 0.0f);
            for (int g = 0; g < link::kGroups; ++g)
                if ((mask >> g) & 1)
                    if (lt.reader[(size_t) g].read(scratchL.data(), scratchR.data(), n))
                        for (int i = 0; i < n; ++i) { lt.inL[(size_t) i] += scratchL[(size_t) i]; lt.inR[(size_t) i] += scratchR[(size_t) i]; }
            io.trackL[t] = lt.inL.data();
            io.trackR[t] = lt.inR.data();
        }
        if (to >= 0)
        {
            bool any = false;
            for (auto& w : lt.writer) any = any || w.attached();
            if (any) { io.tapL[t] = lt.tapL.data(); io.tapR[t] = lt.tapR.data(); }
        }
    }
}

void MixerProcessor::serviceLinkOut(int n)
{
    if (!segment || n > linkCap) return;
    for (auto& lt : linkTracks)
        for (auto& w : lt.writer)
            if (w.attached()) w.write(lt.tapL.data(), lt.tapR.data(), n);
}

void MixerProcessor::prepareToPlay(double sr, int block)
{
    controller->prepareAll(sr, block);
    prepareLink(sr, block);
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

    const int ns = buffer.getNumSamples();
    serviceLinkIn(io, ns);
    engine.process(io, ns);
    serviceLinkOut(ns);
    playHead.set(nullptr);
}

juce::AudioProcessorEditor* MixerProcessor::createEditor() { return new MixerEditor(*this); }

void MixerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    const auto t0 = juce::Time::getMillisecondCounter();
    juce::ValueTree root("LemonMixerRack");
    root.setProperty("version", 1, nullptr);
    root.setProperty("compact", compactUi.load(), nullptr);
    juce::ValueTree params("Params");
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
            params.setProperty(r->getParameterID(), r->getValue(), nullptr);
    root.addChild(params, -1, nullptr);
    controller->save(root);
    juce::MemoryOutputStream os(dest, false);
    root.writeToStream(os);
    hostLog("getState " + juce::String((juce::int64) dest.getSize()) + " bytes, " + juce::String((int) (juce::Time::getMillisecondCounter() - t0)) + " ms");
}

void MixerProcessor::setStateInformation(const void* data, int size)
{
    auto root = juce::ValueTree::readFromData(data, (size_t) size);
    if (! root.isValid() || ! root.hasType("LemonMixerRack"))
        return;

    
    const auto t0 = juce::Time::getMillisecondCounter();
    auto params = root.getChildWithName("Params");
    if (params.isValid())
    {
        for (int i = 0; i < getParameters().size(); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(getParameters()[i]))
                if (params.hasProperty(r->getParameterID()))
                {
                    r->setValue((float) (double) params.getProperty(r->getParameterID()));
                    pushParam(i);
                }
        root.removeChild(params, nullptr);
    }
    compactUi = (bool) root.getProperty("compact", false);
    controller->restore(root);
    hostLog("setState " + juce::String(size) + " bytes, " + juce::String((int) (juce::Time::getMillisecondCounter() - t0)) + " ms");
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new lemon::MixerProcessor(); }
