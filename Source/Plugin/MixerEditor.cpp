#include "MixerEditor.h"
#include "../UI/Pickers.h"
#include "../Hosting/CrashGuard.h"

namespace lemon
{
using namespace core;
namespace col = ui::col;

namespace { constexpr int kHeaderH = 62; }

MixerEditor::MixerEditor(MixerProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&laf);

    for (auto* b : { &scanButton, &addTrackButton, &removeTrackButton }) addAndMakeVisible(b);
    for (int g = 0; g < 4; ++g)
    {
        auto& b = groupB[(size_t) g];
        b.setButtonText("LINK " + juce::String(g + 1));
        b.setClickingTogglesState(false);
        b.setTooltip("Switch link group " + juce::String(g + 1) + " on or off. Any number of groups can be on at once, so a single rack serves several LemonSlice groups");
        b.onClick = [this, g]
        {
            auto& cc = proc.getController();
            cc.setLinkMask(cc.model().linkMask ^ (1 << g));
            syncGroupButtons();
        };
        addAndMakeVisible(b);
    }
    allGroupsB.setClickingTogglesState(false);
    allGroupsB.setTooltip("All four link groups on (click again for group 1 only)");
    allGroupsB.onClick = [this]
    {
        auto& cc = proc.getController();
        cc.setLinkMask(cc.model().linkMask == 15 ? 1 : 15);
        syncGroupButtons();
    };
    addAndMakeVisible(allGroupsB);
    compactB.setClickingTogglesState(false);
    compactB.setToggleState(proc.compactUi.load(), juce::dontSendNotification);
    compactB.setTooltip("Smaller strips: hides EQ and width and shortens the faders. Empty inserts are always collapsed");
    compactB.onClick = [this]
    {
        proc.compactUi = ! proc.compactUi.load();
        compactB.setToggleState(proc.compactUi.load(), juce::dontSendNotification);
        sized = false;
        requestRebuild();
    };
    addAndMakeVisible(compactB);
    syncGroupButtons();
    scanButton.setTooltip("Scan your VST3 folders for plugins");
    addTrackButton.setTooltip("Add a mixer track (up to 16 plus master)");
    removeTrackButton.setTooltip("Remove the last track");
    scanButton.onClick = [this] { startScan(); };
    addTrackButton.onClick = [this] { proc.getController().addTrack(); };
    removeTrackButton.onClick = [this] { proc.getController().removeLastTrack(); };

    status.setFont(ui::sans(12.f));
    if (! proc.getScanner().flData()) proc.getScanner().refreshFl({});     // FL Organizer data are read only when a GUI is open
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
    CrashGuard::instance().endOwned(this);
    proc.getController().removeChangeListener(this);
    proc.getController().onSlotWillClose = nullptr;
    dock.onLayoutChanged = nullptr; dock.onClosed = nullptr; dock.onFloat = nullptr;
    dock.closeAll();                 
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
    windows.closeFor(inst);                       
    dock.open(inst, inst->getName(), track, slot);
    requestRebuild();                            
}

void MixerEditor::showPluginPicker(juce::Component& anchor, int track, int slot)
{
    juce::Component::SafePointer<MixerEditor> safe(this);
    auto content = std::make_unique<ui::PluginPicker>(
        proc.getScanner(),
        [safe, track, slot](const CatalogEntry& e)
        {
            if (safe == nullptr) return;
            if (! e.quarantined) { safe->proc.getController().loadPlugin(track, slot, e.desc); return; }
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Flagged plugin")
                    .withMessage(CrashGuard::instance().reason(e.id) + ".\n\nLoad it anyway? If it loads cleanly the flag is cleared.")
                    .withButton("Load anyway").withButton("Cancel"),
                [safe, track, slot, e](int r)
                { if (r == 1 && safe != nullptr) safe->proc.getController().loadPlugin(track, slot, e.desc, true); });
        },
        [safe] { if (safe != nullptr) safe->startScan(); },
        [safe] { if (safe != nullptr) safe->startImport(); },
        [safe] { if (safe != nullptr) safe->locateFlDatabase(); },
        [safe] { if (safe != nullptr) safe->linkOrganizerDb(); });
    juce::CallOutBox::launchAsynchronously(std::move(content), getLocalArea(&anchor, anchor.getLocalBounds()), this);
}

void MixerEditor::askRetryFlagged(int track, int slot)
{
    const auto& sm = proc.getController().model().tracks[(size_t) track].slots[(size_t) slot];
    juce::Component::SafePointer<MixerEditor> safe(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Flagged plugin")
            .withMessage(CrashGuard::instance().reason(sm.desc.createIdentifierString())
                         + ".\n\nIt was left unloaded (its saved settings are kept). Retry loading it?")
            .withButton("Retry").withButton("Leave off"),
        [safe, track, slot](int r) { if (r == 1 && safe != nullptr) safe->proc.getController().retryPlugin(track, slot); });
}

void MixerEditor::syncGroupButtons()
{
    const int mask = proc.getController().model().linkMask;
    for (int k = 0; k < 4; ++k) groupB[(size_t) k].setToggleState(((mask >> k) & 1) != 0, juce::dontSendNotification);
    allGroupsB.setToggleState(mask == 15, juce::dontSendNotification);
}

void MixerEditor::showLanePicker(juce::Component& anchor, int current, std::function<void(int)> onPick)
{
    juce::StringArray names;
    std::vector<int> choices;
    for (int i = 0; i < link::kLanes; ++i) { names.add("DAW " + juce::String(i + 1)); choices.push_back(i); }
    auto content = std::make_unique<ui::TrackPicker>(names, choices, current, "OFF", std::move(onPick));
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
    auto& sc = proc.getScanner();
    if (sc.isScanning()) { status.setText("Busy - try again in a moment", juce::dontSendNotification); return; }
    status.setText("Scanning VST3 folders...", juce::dontSendNotification);
    juce::Component::SafePointer<MixerEditor> safe(this);
    sc.scan({},
        [safe](const juce::String& n)
        { if (safe != nullptr) safe->status.setText("Scanning: " + juce::File(n).getFileName(), juce::dontSendNotification); },
        [safe]
        {
            if (safe != nullptr)
                safe->status.setText("Scan finished: " + juce::String(safe->proc.getScanner().list().getNumTypes())
                                     + " plugins. " + safe->proc.getScanner().lastMessage(), juce::dontSendNotification);
        });
}

void MixerEditor::startImport()
{
    auto& sc = proc.getScanner();
    if (sc.isScanning()) { status.setText("Busy - try again in a moment", juce::dontSendNotification); return; }
    status.setText("Importing plugins from the FL Studio database...", juce::dontSendNotification);
    juce::Component::SafePointer<MixerEditor> safe(this);
    sc.importFromFl(
        [safe](const juce::String& n)
        { if (safe != nullptr) safe->status.setText("Importing: " + juce::File(n).getFileName(), juce::dontSendNotification); },
        [safe]
        {
            if (safe != nullptr)
                safe->status.setText("Import finished: " + juce::String(safe->proc.getScanner().list().getNumTypes()) + " plugins known. " + safe->proc.getScanner().lastMessage(),
                                     juce::dontSendNotification);
        });
}

void MixerEditor::locateFlDatabase()
{
    chooser = std::make_unique<juce::FileChooser>("Select FL Studio's 'Plugin database' folder", proc.getScanner().flRoot());
    juce::Component::SafePointer<MixerEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            safe->proc.getScanner().setFlRoot(fc.getResult());
            safe->proc.getScanner().refreshFl([safe]
            { if (safe != nullptr) safe->status.setText("FL database reloaded", juce::dontSendNotification); });
        });
}

void MixerEditor::linkOrganizerDb()
{
    if (! data::sqliteAvailable())
    {
        status.setText("SQLite is not in this build: add Source/ThirdParty/sqlite/sqlite3.h and sqlite3.c, then rebuild",
                       juce::dontSendNotification);
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Select your FLP Organizer database", proc.getScanner().orgPath(), "*.db;*.sqlite;*.sqlite3");
    juce::Component::SafePointer<MixerEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            safe->proc.getScanner().setOrgPath(fc.getResult());
            safe->status.setText("Reading the Organizer database...", juce::dontSendNotification);
            safe->proc.getScanner().refreshFl([safe]
            {
                if (safe == nullptr) return;
                auto org = safe->proc.getScanner().orgData();
                if (org && org->ok)
                    safe->status.setText("Organizer linked: table '" + juce::String(org->table) + "', " + juce::String((int) org->plugins.size()) + " plugins", juce::dontSendNotification);
                else if (org)
                {
                    juce::SystemClipboard::copyTextToClipboard(juce::String(org->schema));
                    safe->status.setText("Organizer DB: " + juce::String(org->error) + " (its table layout is on your clipboard - paste it to me)", juce::dontSendNotification);
                }
            });
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
    if (! sized)
    {
        sized = true;
        const int want = juce::jlimit(460, 1000, maxH + kHeaderH + 20 + dockHeight);
        if (want != getHeight()) setSize(getWidth(), want);
    }
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
        // Grow shrink the window so the mixer area keeps its size.
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
    h.removeFromRight(8);
    compactB.setBounds(h.removeFromRight(66).withTrimmedBottom(14));
    h.removeFromRight(6);
    allGroupsB.setBounds(h.removeFromRight(36).withTrimmedBottom(14));
    h.removeFromRight(2);
    for (int g = 3; g >= 0; --g) { groupB[(size_t) g].setBounds(h.removeFromRight(52).withTrimmedBottom(14)); h.removeFromRight(2); }
    h.removeFromRight(10);
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
