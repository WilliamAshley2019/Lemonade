#include "MixerEditor.h"
#include "../UI/Pickers.h"

namespace lemon
{
using namespace core;
namespace col = ui::col;

namespace { constexpr int kHeaderH = 62; }

MixerEditor::MixerEditor(MixerProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&laf);

    for (auto* b : { &scanButton, &addTrackButton, &removeTrackButton }) addAndMakeVisible(b);
    scanButton.setTooltip("Scan your VST3 folders for plugins");
    addTrackButton.setTooltip("Add a mixer track (up to 16 plus master)");
    removeTrackButton.setTooltip("Remove the last track");
    scanButton.onClick = [this] { startScan(); };
    addTrackButton.onClick = [this] { proc.getController().addTrack(); };
    removeTrackButton.onClick = [this] { proc.getController().removeLastTrack(); };

    status.setFont(ui::sans(12.f));
    status.setColour(juce::Label::textColourId, col::amber);
    addAndMakeVisible(status);

    viewport.setViewedComponent(&strips, false);
    viewport.setScrollBarsShown(true, true);
    viewport.getHorizontalScrollBar().setAutoHide(false);
    addAndMakeVisible(viewport);
    addAndMakeVisible(dock);

    dock.onLayoutChanged = [this] { dockChanged(); };
    dock.onClosed = [this] { requestRebuild(); };
    dock.onFloat = [this](juce::AudioProcessor* ap)
    {
        const auto title = ap->getName();
        dock.closeFor(ap);
        windows.show(ap, title);
    };

    proc.getController().onSlotWillClose = [this](juce::AudioProcessor* ap)
    {
        windows.closeFor(ap);
        dock.closeFor(ap);
    };
    proc.getController().addChangeListener(this);

    setResizable(true, true);
    setResizeLimits(640, 460, 3200, 1400);
    setSize(1280, 820);
    rebuild();
    startTimerHz(30);
}

MixerEditor::~MixerEditor()
{
    stopTimer();
    cancelPendingUpdate();
    proc.getController().removeChangeListener(this);
    proc.getController().onSlotWillClose = nullptr;
    dock.onLayoutChanged = nullptr; dock.onClosed = nullptr; dock.onFloat = nullptr;
    dock.closeAll();                // plugin GUIs must die before their plugins do
    windows.closeAll();
    setLookAndFeel(nullptr);
}

juce::StringArray MixerEditor::trackNames() const
{
    juce::StringArray n;
    for (auto& t : proc.getController().model().tracks) n.add(t.name);
    if (n.size() > 0) n.set(0, "Master");
    return n;
}

void MixerEditor::openPluginUi(int track, int slot)
{
    auto* inst = proc.getController().instanceAt(track, slot);
    if (inst == nullptr) return;
    windows.closeFor(inst);                      // a plugin can only have one GUI open at a time
    dock.open(inst, inst->getName(), track, slot);
    requestRebuild();                            // lights the slot's "docked" outline
}

void MixerEditor::showPluginPicker(juce::Component& anchor, int track, int slot)
{
    auto content = std::make_unique<ui::PluginPicker>(
        proc.getScanner().list(),
        [this, track, slot](const juce::PluginDescription& d) { proc.getController().loadPlugin(track, slot, d); },
        [this] { startScan(); });
    juce::CallOutBox::launchAsynchronously(std::move(content), getLocalArea(&anchor, anchor.getLocalBounds()), this);
}

void MixerEditor::showTrackPicker(juce::Component& anchor, const std::vector<int>& choices, int current,
                                  const juce::String& noneLabel, std::function<void(int)> onPick)
{
    auto content = std::make_unique<ui::TrackPicker>(trackNames(), choices, current, noneLabel, std::move(onPick));
    juce::CallOutBox::launchAsynchronously(std::move(content), getLocalArea(&anchor, anchor.getLocalBounds()), this);
}

void MixerEditor::startScan()
{
    if (proc.getScanner().isScanning()) return;
    status.setText("Scanning VST3 plugins...", juce::dontSendNotification);
    juce::Component::SafePointer<MixerEditor> safe(this);
    proc.getScanner().scan({},
        [safe](const juce::String& n)
        { if (safe != nullptr) safe->status.setText("Scanning: " + juce::File(n).getFileName(), juce::dontSendNotification); },
        [safe]
        {
            if (safe != nullptr)
                safe->status.setText("Scan finished: " + juce::String(safe->proc.getScanner().list().getNumTypes())
                                     + " plugins (run it again if a plugin was skipped)", juce::dontSendNotification);
        });
}

void MixerEditor::changeListenerCallback(juce::ChangeBroadcaster*) { requestRebuild(); }

void MixerEditor::rebuild()
{
    stripList.clear();
    auto& c = proc.getController();
    int x = 0, maxH = 0;
    for (int t = 0; t < c.numTracks(); ++t)
    {
        auto s = std::make_unique<ui::TrackStrip>(*this, t);
        strips.addAndMakeVisible(*s);
        s->setTopLeftPosition(x, 0);
        x += ui::TrackStrip::width + 2;
        maxH = juce::jmax(maxH, s->idealHeight());
        stripList.push_back(std::move(s));
    }
    strips.setSize(x, maxH);
    if (c.lastError.isNotEmpty()) status.setText(c.lastError, juce::dontSendNotification);
    repaint();
}

void MixerEditor::timerCallback()
{
    for (auto& s : stripList) s->updateMeter();

    int loaded = 0;
    for (auto& t : proc.getController().model().tracks) for (auto& s : t.slots) loaded += s.slot ? 1 : 0;
    const int lat = proc.getEngine().latency();
    if (lat != lastLatencyShown || loaded != lastPluginsShown)
    {
        lastLatencyShown = lat; lastPluginsShown = loaded;
        repaint(headerRect);
    }
}

void MixerEditor::dockChanged()
{
    if (inLayout) return;
    const int want = dock.desiredHeight();
    if (want != dockHeight)
    {
        // Grow / shrink the window so the mixer area keeps its size.
        inLayout = true;
        const int delta = want - dockHeight;
        dockHeight = want;
        setSize(getWidth(), juce::jmax(460, getHeight() + delta));
        inLayout = false;
    }
    layoutAll();
}

void MixerEditor::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);
    static juce::SharedResourcePointer<ui::Textures> tex;
    ui::brushedMetal(g, headerRect.toFloat(), 6.f, true, *tex);

    ui::engravedText(g, "LEMON MIXER RACK", { 16.f, 8.f, 260.f, 26.f }, juce::Justification::centredLeft, ui::caps(18.f), true);
    ui::tag(g, "ALPHAAUDIO  -  VST3 HOST AND MIXER", { 16.f, 34.f, 260.f, 14.f }, juce::Justification::centredLeft, 8.5f);

    auto lcd = [&](juce::Rectangle<int> r, const juce::String& cap, const juce::String& value)
    {
        ui::glassWell(g, r.toFloat(), 4.f);
        ui::sevenSegment(g, value, r.toFloat().reduced(8.f, 6.f).withTrimmedBottom(10.f), col::amber, col::amberGhost);
        ui::tag(g, cap, r.toFloat().removeFromBottom(12.f), juce::Justification::centred, 7.5f, col::textDim);
    };
    lcd(latencyLcd, "LATENCY  SAMPLES", juce::String(juce::jlimit(0, 99999, proc.getEngine().latency())));
    int loaded = 0;
    for (auto& t : proc.getController().model().tracks) for (auto& s : t.slots) loaded += s.slot ? 1 : 0;
    lcd(pluginsLcd, "PLUGINS  LOADED", juce::String(loaded));
}

void MixerEditor::resized()
{
    if (inLayout) { layoutAll(); return; }
    layoutAll();
}

void MixerEditor::layoutAll()
{
    auto r = getLocalBounds();
    headerRect = r.removeFromTop(kHeaderH).reduced(4, 3);

    auto h = headerRect.reduced(8, 8);
    h.removeFromLeft(280);
    removeTrackButton.setBounds(h.removeFromRight(76).withTrimmedBottom(14));
    h.removeFromRight(4);
    addTrackButton.setBounds(h.removeFromRight(76).withTrimmedBottom(14));
    h.removeFromRight(4);
    scanButton.setBounds(h.removeFromRight(60).withTrimmedBottom(14));
    h.removeFromRight(12);
    pluginsLcd = h.removeFromRight(110).withTrimmedTop(-4).withTrimmedBottom(-4);
    h.removeFromRight(6);
    latencyLcd = h.removeFromRight(150).withTrimmedTop(-4).withTrimmedBottom(-4);
    status.setBounds(h.removeFromTop(h.getHeight() - 14));

    const int dh = juce::jmin(dockHeight, r.getHeight() - 160);
    dock.setBounds(r.removeFromBottom(juce::jmax(0, dh)));
    viewport.setBounds(r.reduced(4, 2));
    repaint();
}
}
