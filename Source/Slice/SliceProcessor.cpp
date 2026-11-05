#include "SliceProcessor.h"
#include "SliceEditor.h"

namespace lemon
{
juce::AudioProcessorValueTreeState::ParameterLayout SliceProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "group", 1 }, "Link Group", 1, link::kGroups, 1));
    l.add(std::make_unique<AudioParameterInt>(ParameterID { "lane", 1 }, "DAW Lane", 1, link::kLanes, 1));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { "gain", 1 }, "Gain",
          NormalisableRange<float>(-60.0f, 12.0f, 0.1f, 2.5f), 0.0f,
          AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { "thru", 1 },
          isIn ? "Pass Audio Through" : "Add To Track Audio", false));
    return l;
}

SliceProcessor::SliceProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "LemonSlice", layout())
{
    static std::atomic<uint32_t> counter { 0 };
    id = (counter.fetch_add(1) + 1) * 4096u + (uint32_t) (juce::Time::getMillisecondCounter() & 0xfffu) + 1u;
    segment = link::openSegment();
    pGroup = apvts.getRawParameterValue("group");
    pLane  = apvts.getRawParameterValue("lane");
    pGain  = apvts.getRawParameterValue("gain");
    pThru  = apvts.getRawParameterValue("thru");
}

SliceProcessor::~SliceProcessor()
{
    writer.release(); reader.release();
    if (segment) link::closeSegment();
}

void SliceProcessor::prepareToPlay(double sr, int block)
{
    sampleRate = sr;
    blockCap = std::max(block, 64);
    bufL.assign((size_t) blockCap, 0.0f);
    bufR.assign((size_t) blockCap, 0.0f);
    writer.release(); reader.release();
    curGroup = curLane = -1;
}

void SliceProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    if (nc < 2) return;
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getWritePointer(1);
    const int group = (int) pGroup->load() - 1, lane = (int) pLane->load() - 1;
    const float gain = juce::Decibels::decibelsToGain(pGain->load(), -60.0f);
    const bool thru = pThru->load() > 0.5f;

    if (segment && (group != curGroup || lane != curLane))
    {
        writer.release(); reader.release();
        if (isIn) writer.attach(segment, group, lane, link::toRack, id, sampleRate);
        else      reader.attach(segment, group, lane, link::toDaw, sampleRate, n);
        curGroup = group; curLane = lane;
    }

    if (n > blockCap || !segment)      // host exceeded the announced block size: cannot service the link safely
    {
        if (isIn && !thru) buffer.clear();
        if (!isIn && !thru) buffer.clear();
        status = 0;
        return;
    }

    float pk = 0.0f;
    if (isIn)
    {
        for (int i = 0; i < n; ++i) { bufL[(size_t) i] = L[i] * gain; bufR[(size_t) i] = R[i] * gain; }
        if (writer.attached()) { writer.write(bufL.data(), bufR.data(), n); status = 1; }
        else status = 2;
        for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::abs(bufL[(size_t) i]), std::abs(bufR[(size_t) i])));
        if (!thru) { buffer.clear(); }
    }
    else
    {
        const bool ok = reader.read(bufL.data(), bufR.data(), n);
        status = ok ? 1 : 0;
        underruns = reader.underruns();
        marginFrames = reader.latencyFrames();
        for (int i = 0; i < n; ++i)
        {
            const float a = bufL[(size_t) i] * gain, b = bufR[(size_t) i] * gain;
            pk = std::max(pk, std::max(std::abs(a), std::abs(b)));
            L[i] = thru ? L[i] + a : a;
            R[i] = thru ? R[i] + b : b;
        }
    }
    for (int c = 2; c < nc; ++c) buffer.clear(c, 0, n);
    peak = std::max(peak.load() * 0.9f, pk);
}

juce::AudioProcessorEditor* SliceProcessor::createEditor() { return new SliceEditor(*this); }

void SliceProcessor::getStateInformation(juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, d);
}

void SliceProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new lemon::SliceProcessor(); }
