#include "TrackStrip.h"
#include "../Core/Fader.h"
#include "../Plugin/MixerEditor.h"

namespace lemon::ui
{
using namespace core;

namespace
{
void styleKnob(juce::Slider& s, double lo, double hi, double def, double step = 0.001)
{
    s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setRange(lo, hi, step);
    s.setDoubleClickReturnValue(true, def);
}
}

// ============================================================================ insert slot cell
struct TrackStrip::SlotCell : public juce::Component
{
    SlotCell(MixerEditor& e, int t, int s) : ed(e), track(t), slot(s)
    {
        auto& c = ed.proc.getController();
        auto& sm = c.model().tracks[(size_t) t].slots[(size_t) s];
        auto& lv = ed.proc.getEngine().live(t);
        const bool loaded = sm.slot != nullptr;
        empty = ! sm.hasDesc && ! loaded && ! sm.loading;
        if (empty) addB.setButtonText("+  ADD INSERT");

        addB.setTooltip(loaded ? "Replace this plugin" : "Add a plugin to this insert");
        addB.onClick = [this] { ed.showPluginPicker(addB, track, slot); };
        remB.setTooltip("Remove the plugin from this insert");
        remB.setEnabled(loaded || sm.missing);
        remB.setAlpha(remB.isEnabled() ? 1.0f : 0.35f);
        remB.onClick = [this] { ed.proc.getController().removePlugin(track, slot); };

        juce::String txt = "- empty -";
        if (sm.loading) txt = "loading...";
        else if (loaded) txt = sm.desc.name;
        else if (sm.quarantined) txt = "FLAGGED " + sm.desc.name;
        else if (sm.missing) txt = "MISSING " + sm.desc.name;
        nameB.setButtonText(txt);
        nameB.setToggleState(loaded, juce::dontSendNotification);
        nameB.setTooltip(loaded ? "Click to open the plugin window (docked below)"
                         : sm.quarantined ? "This plugin crashed the host before. Click to retry or leave it off"
                                          : "Click to pick a plugin");
        nameB.onClick = [this]
        {
            auto& cc = ed.proc.getController();
            auto& cur = cc.model().tracks[(size_t) track].slots[(size_t) slot];
            if (cur.slot) ed.openPluginUi(track, slot);
            else if (cur.quarantined) ed.askRetryFlagged(track, slot);
            else ed.showPluginPicker(nameB, track, slot);
        };

        bypB.setClickingTogglesState(true);
        bypB.setToggleState(! lv.slotEnabled[(size_t) s].load(), juce::dontSendNotification);
        bypB.setTooltip("Bypass this plugin (it stays time-aligned)");
        bypB.setEnabled(loaded); bypB.setAlpha(loaded ? 1.0f : 0.35f);
        bypB.onClick = [this] { ed.proc.getEngine().live(track).slotEnabled[(size_t) slot] = ! bypB.getToggleState(); };

        const bool canKey = loaded && sm.slot->wantsKey();
        scB.setButtonText(sm.keySource > 0 && canKey ? "SC " + ed.trackNames()[sm.keySource] : "SC");
        scB.setToggleState(sm.keySource > 0 && canKey, juce::dontSendNotification);
        scB.setEnabled(canKey); scB.setAlpha(canKey ? 1.0f : 0.35f);
        scB.setTooltip(! loaded ? "Sidechain (load a plugin first)"
                       : canKey ? "Pick the mixer track that drives this plugin's sidechain"
                                : "This plugin has no sidechain input");
        scB.onClick = [this]
        {
            auto& cc = ed.proc.getController();
            std::vector<int> choices;
            for (int i = 1; i < cc.numTracks(); ++i) if (i != track) choices.push_back(i);
            const int cur = cc.model().tracks[(size_t) track].slots[(size_t) slot].keySource;
            ed.showTrackPicker(scB, choices, cur, "NONE", [this](int src)
            {
                if (! ed.proc.getController().setKeySource(track, slot, src))
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Sidechain",
                                                           ed.proc.getController().lastError);
            });
        };

        styleKnob(wet, 0.0, 1.0, 1.0, 0.01);
        wet.setValue(lv.slotMix[(size_t) s].load(), juce::dontSendNotification);
        wet.setEnabled(loaded); wet.setAlpha(loaded ? 1.0f : 0.35f);
        wet.setTooltip("Dry / wet mix (double-click = 100% wet)");
        wet.onValueChange = [this]
        {
            ed.proc.getEngine().live(track).slotMix[(size_t) slot] = (float) wet.getValue();
            pct.setText(juce::String(juce::roundToInt(wet.getValue() * 100.0)) + "%", juce::dontSendNotification);
        };
        pct.setFont(caps(8.5f)); pct.setJustificationType(juce::Justification::centredLeft);
        pct.setColour(juce::Label::textColourId, col::textDim);
        pct.setText(juce::String(juce::roundToInt(wet.getValue() * 100.0)) + "%", juce::dontSendNotification);
        pct.setInterceptsMouseClicks(false, false);

        for (auto* b : { &addB, &remB, &nameB, &bypB, &scB }) addAndMakeVisible(b);
        addAndMakeVisible(wet); addAndMakeVisible(pct);
        docked = ed.isDocked(t, s);
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        glassWell(g, r, 4.f);
        if (empty) return;
        if (docked) { g.setColour(col::amber.withAlpha(0.8f)); g.drawRoundedRectangle(r.reduced(0.5f), 4.f, 1.5f); }
        tag(g, "WET", wetCaption, juce::Justification::centredLeft, 7.5f, col::textFaint);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(3);
        const bool slim = empty;
        for (auto* c : std::initializer_list<juce::Component*> { &remB, &nameB, &bypB, &scB, &wet, &pct }) c->setVisible(! slim);
        if (slim) { addB.setBounds(r); return; }
        auto row1 = r.removeFromTop(18);
        addB.setBounds(row1.removeFromLeft(18));
        row1.removeFromLeft(2);
        remB.setBounds(row1.removeFromLeft(18));
        row1.removeFromLeft(2);
        nameB.setBounds(row1);
        r.removeFromTop(3);
        auto row2 = r;
        bypB.setBounds(row2.removeFromLeft(36));
        row2.removeFromLeft(2);
        scB.setBounds(row2.removeFromLeft(58));
        row2.removeFromLeft(2);
        wet.setBounds(row2.removeFromLeft(row2.getHeight()));
        auto txt = row2.reduced(2, 0);
        wetCaption = txt.removeFromTop(txt.getHeight() / 2).toFloat();
        pct.setBounds(txt);
    }

    MixerEditor& ed;
    int track, slot;
    bool docked = false;
    bool empty = false;
    juce::TextButton addB { "+" }, remB { "-" }, nameB, bypB { "BYP" }, scB { "SC" };
    juce::Slider wet;
    juce::Label pct;
    juce::Rectangle<float> wetCaption;
};

// ============================================================================ send row
struct TrackStrip::SendWidgets
{
    juce::TextButton dest, pre { "PRE" }, remove { "-" };
    juce::Slider level { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
};

// ============================================================================ strip
TrackStrip::TrackStrip(MixerEditor& e, int t) : ed(e), track(t)
{
    auto& c = ed.proc.getController();
    auto& tm = c.model().tracks[(size_t) t];
    auto& lv = ed.proc.getEngine().live(t);
    const auto names = ed.trackNames();

    name.setText(t == 0 ? "MASTER" : tm.name, juce::dontSendNotification);
    name.setFont(caps(11.f));
    name.setEditable(false, t > 0, false);
    name.setJustificationType(juce::Justification::centred);
    name.setColour(juce::Label::textColourId, col::amber);
    name.setTooltip(t == 0 ? "Master bus" : "Double-click to rename");
    name.onTextChange = [this] { ed.proc.getController().renameTrack(track, name.getText()); };
    addAndMakeVisible(name);

    for (int s = 0; s < kMaxSlots; ++s)
    {
        cells.push_back(std::make_unique<SlotCell>(ed, t, s));
        addAndMakeVisible(*cells.back());
    }

    // who is listening to this track's signal as a sidechain?
    for (int ot = 1; ot < c.numTracks(); ++ot)
        for (int s = 0; s < kMaxSlots; ++s)
        {
            auto& sm = c.model().tracks[(size_t) ot].slots[(size_t) s];
            if (sm.slot && sm.keySource == t && sm.slot->wantsKey())
                keyText += (keyText.isEmpty() ? "" : "  ") + juce::String("T") + juce::String(ot) + "." + juce::String(s + 1);
        }

    if (t > 0)
    {
        mstButton.setClickingTogglesState(false);
        mstButton.setToggleState(tm.toMaster, juce::dontSendNotification);
        mstButton.setTooltip("Send this track to the master bus");
        mstButton.onClick = [this]
        {
            auto& cc = ed.proc.getController();
            if (! cc.setToMaster(track, ! cc.model().tracks[(size_t) track].toMaster)) showError();
        };
        addAndMakeVisible(mstButton);

        for (int k = 0; k < 4; ++k)
        {
            auto& b = hostOut[(size_t) k];
            b.setButtonText(juce::String(k + 1));
            b.setToggleState(tm.hostOut == k, juce::dontSendNotification);
            b.setTooltip("Also copy this track to host output " + juce::String(k + 1) + " (click again to clear)");
            b.onClick = [this, k]
            {
                auto& cc = ed.proc.getController();
                const bool already = cc.model().tracks[(size_t) track].hostOut == k;
                cc.setHostOut(track, already ? -1 : k);
            };
            addAndMakeVisible(b);
        }

        {
            auto laneText = [](const char* pre, int lane) { return lane < 0 ? juce::String(pre) + " -" : juce::String(pre) + " " + juce::String(lane + 1); };
            linkFromB.setButtonText(laneText("FROM DAW", tm.linkFrom));
            linkToB.setButtonText(laneText("TO DAW", tm.linkTo));
            linkFromB.setToggleState(tm.linkFrom >= 0, juce::dontSendNotification);
            linkToB.setToggleState(tm.linkTo >= 0, juce::dontSendNotification);
            linkFromB.setTooltip("Audio from a DAW mixer track carrying a LemonSlice In set to this lane number");
            linkToB.setTooltip("Send this track's post-fader audio to a DAW mixer track carrying a LemonSlice Out set to this lane number");
            linkFromB.onClick = [this]
            {
                auto& cc = ed.proc.getController();
                ed.showLanePicker(linkFromB, cc.model().tracks[(size_t) track].linkFrom, [this](int lane) { ed.proc.getController().setLinkFrom(track, lane); });
            };
            linkToB.onClick = [this]
            {
                auto& cc = ed.proc.getController();
                ed.showLanePicker(linkToB, cc.model().tracks[(size_t) track].linkTo, [this](int lane)
                { if (! ed.proc.getController().setLinkTo(track, lane)) showError(); });
            };
            addAndMakeVisible(linkFromB);
            addAndMakeVisible(linkToB);
        }

        for (size_t k = 0; k < tm.sends.size(); ++k)
        {
            auto w = std::make_unique<SendWidgets>();
            w->dest.setButtonText("> " + names[tm.sends[k].dest]);
            w->dest.setTooltip("Send target (click to change)");
            w->dest.onClick = [this, k]
            {
                auto& cc = ed.proc.getController();
                std::vector<int> choices;
                for (int i = 1; i < cc.numTracks(); ++i) if (i != track) choices.push_back(i);
                ed.showTrackPicker(sends[k]->dest, choices, cc.model().tracks[(size_t) track].sends[k].dest, {},
                                   [this, k](int d) { if (! ed.proc.getController().setSendDest(track, (int) k, d)) showError(); });
            };
            w->pre.setClickingTogglesState(false);
            w->pre.setToggleState(tm.sends[k].preFader, juce::dontSendNotification);
            w->pre.setTooltip("Pre-fader send (off = post-fader)");
            w->pre.onClick = [this, k]
            {
                auto& cc = ed.proc.getController();
                cc.setSendPre(track, (int) k, ! cc.model().tracks[(size_t) track].sends[k].preFader);
            };
            w->remove.setTooltip("Remove this send");
            w->remove.onClick = [this, k] { ed.proc.getController().removeSend(track, (int) k); };
            w->level.setRange(0.0, 1.0, 0.001);
            w->level.setValue(lv.sendLevel[k].load(), juce::dontSendNotification);
            w->level.setDoubleClickReturnValue(true, 0.8);
            w->level.setTooltip("Send level");
            w->level.onValueChange = [this, k, p = &w->level] { ed.proc.getEngine().live(track).sendLevel[k] = (float) p->getValue(); };
            for (auto* b : { &w->dest, &w->pre, &w->remove }) addAndMakeVisible(b);
            addAndMakeVisible(w->level);
            sends.push_back(std::move(w));
        }
        addSend.setTooltip("Add a send to another track (effects bus, parallel chain, sidechain feed...)");
        addSend.setEnabled(tm.sends.size() < (size_t) kMaxSends);
        addSend.onClick = [this]
        {
            auto& cc = ed.proc.getController();
            std::vector<int> choices;
            for (int i = 1; i < cc.numTracks(); ++i) if (i != track) choices.push_back(i);
            ed.showTrackPicker(addSend, choices, -1, {}, [this](int d)
            { if (! ed.proc.getController().addSend(track, d, false)) showError(); });
        };
        addAndMakeVisible(addSend);
    }

    const char* eqTip[] = { "Low shelf gain, 100 Hz (double-click = 0)", "Mid bell gain, 1 kHz (double-click = 0)", "High shelf gain, 8 kHz (double-click = 0)" };
    for (int b = 0; b < kEqBands; ++b)
    {
        auto& k = eq[(size_t) b];
        styleKnob(k, -18.0, 18.0, 0.0, 0.1);
        k.setValue(lv.eqGainDb[(size_t) b].load(), juce::dontSendNotification);
        k.setTooltip(eqTip[b]);
        k.onValueChange = [this, b] { ed.proc.getEngine().live(track).eqGainDb[(size_t) b] = (float) eq[(size_t) b].getValue(); };
        addAndMakeVisible(k);
    }

    pan.setSliderStyle(juce::Slider::LinearHorizontal);
    pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pan.setDoubleClickReturnValue(true, 0.0);
    pan.setTooltip("Balance (double-click = centre)");
    addAndMakeVisible(pan);
    panAtt = std::make_unique<juce::SliderParameterAttachment>(*ed.proc.panParam(t), pan, nullptr);

    styleKnob(widthKnob, 0.0, 2.0, 1.0, 0.01);
    widthKnob.setValue(lv.width.load(), juce::dontSendNotification);
    widthKnob.setTooltip("Stereo width: 0 = mono, 1 = normal, 2 = wide (double-click = 1)");
    widthKnob.onValueChange = [this] { ed.proc.getEngine().live(track).width = (float) widthKnob.getValue(); };
    addAndMakeVisible(widthKnob);

    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fader.setDoubleClickReturnValue(true, 0.8);
    fader.setTooltip("Level (double-click = 0 dB)");
    addAndMakeVisible(fader);
    faderAtt = std::make_unique<juce::SliderParameterAttachment>(*ed.proc.volumeParam(t), fader, nullptr);

    for (auto* b : { &muteB, &soloB, &polB }) { b->setClickingTogglesState(true); addAndMakeVisible(b); }
    muteB.setTooltip("Mute"); soloB.setTooltip("Solo"); polB.setTooltip("Invert polarity");
    polB.setToggleState(lv.polarity.load(), juce::dontSendNotification);
    polB.onClick = [this] { ed.proc.getEngine().live(track).polarity = polB.getToggleState(); };
    muteAtt = std::make_unique<juce::ButtonParameterAttachment>(*ed.proc.muteParam(t), muteB, nullptr);
    soloAtt = std::make_unique<juce::ButtonParameterAttachment>(*ed.proc.soloParam(t), soloB, nullptr);
    if (t == 0) { soloB.setVisible(false); }

    layout();
    setSize(width, height);
}

TrackStrip::~TrackStrip() = default;

void TrackStrip::showError()
{
    const auto e = ed.proc.getController().lastError;
    if (e.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Routing", e);
}

void TrackStrip::layout()
{
    captions.clear();
    const int x0 = 5, w = width - 10;
    int y = 4;
    auto caption = [&](const juce::String& s, int cy) { captions.push_back({ s, { x0, cy, w, 10 } }); };

    name.setBounds(x0, y, w, 20); y += 24;

    caption("INSERTS  -  ADD  REMOVE  BYPASS  SIDECHAIN  WET", y); y += 11;
    {
        bool shownEmpty = false;      // empty inserts stay out of the way: only the first one is offered, as a slim row
        for (auto& c : cells)
        {
            if (c->empty && shownEmpty) { c->setVisible(false); continue; }
            if (c->empty) shownEmpty = true;
            c->setVisible(true);
            const int hh = c->empty ? 22 : 44;
            c->setBounds(x0, y, w, hh);
            y += hh + 2;
        }
    }

    keyRect = {};
    if (keyText.isNotEmpty()) { keyRect = { x0, y, w, 12 }; y += 14; }

    if (track > 0)
    {
        y += 2;
        caption("OUTPUT  -  MASTER  AND  HOST OUT 1-4", y); y += 11;
        auto row = juce::Rectangle<int>(x0, y, w, 18);
        mstButton.setBounds(row.removeFromLeft(40));
        row.removeFromLeft(4);
        const int bw = row.getWidth() / 4;
        for (auto& b : hostOut) b.setBounds(row.removeFromLeft(bw).reduced(1, 0));
        y += 22;

        caption("DAW LINK  -  LEMONSLICE  IN  /  OUT", y); y += 11;
        {
            auto lr = juce::Rectangle<int>(x0, y, w, 18);
            const int hw = (lr.getWidth() - 4) / 2;
            linkFromB.setBounds(lr.removeFromLeft(hw));
            lr.removeFromLeft(4);
            linkToB.setBounds(lr);
            y += 22;
        }

        caption("SENDS  -  TARGET  PRE  LEVEL", y); y += 11;
        for (auto& s : sends)
        {
            auto r = juce::Rectangle<int>(x0, y, w, 18);
            s->dest.setBounds(r.removeFromLeft(44));
            r.removeFromLeft(2);
            s->pre.setBounds(r.removeFromLeft(32));
            r.removeFromLeft(2);
            s->remove.setBounds(r.removeFromRight(18));
            s->level.setBounds(r.reduced(2, 0));
            y += 20;
        }
        addSend.setBounds(x0, y, w, 18); y += 22;
    }

    const bool compact = ed.proc.compactUi.load();
    for (auto& k : eq) k.setVisible(! compact);
    widthKnob.setVisible(! compact);
    y += 2;
    if (! compact)
    {
    caption("EQ  -  GAIN", y); y += 11;
    {
        const int kw = w / 3;
        for (int b = 0; b < kEqBands; ++b)
        {
            eq[(size_t) b].setBounds(x0 + b * kw, y, kw, 36);
            static const char* nm[] = { "LO", "MID", "HI" };
            captions.push_back({ nm[b], { x0 + b * kw, y + 36, kw, 10 } });
        }
        y += 48;
    }
    }

    caption(compact ? "PAN" : "PAN                              WIDTH", y); y += 11;
    pan.setBounds(x0, y, compact ? w : w - 40, 22);
    widthKnob.setBounds(x0 + w - 36, y - 2, 32, 28);
    y += 28;

    caption("LEVEL  -  DB", y); y += 11;
    lcdRect = { x0, y, w - 34, 20 };
    y += 24;
    const int faderH = compact ? 110 : 176;
    fader.setBounds(x0, y, w - 34, faderH);
    meterRect = { x0 + w - 30, y - 24, 30, faderH + 24 };
    y += faderH + 4;

    auto row = juce::Rectangle<int>(x0, y, w, 22);
    if (track == 0)
    {
        muteB.setBounds(row.removeFromLeft(w / 2).reduced(1, 0));
        polB.setBounds(row.reduced(1, 0));
    }
    else
    {
        const int bw = w / 3;
        muteB.setBounds(row.removeFromLeft(bw).reduced(1, 0));
        soloB.setBounds(row.removeFromLeft(bw).reduced(1, 0));
        polB.setBounds(row.reduced(1, 0));
    }
    y += 28;
    height = y + 2;
}

void TrackStrip::updateMeter()
{
    auto& lv = ed.proc.getEngine().live(track);
    const float l = lv.peakL.exchange(0.0f), r = lv.peakR.exchange(0.0f);
    meterL = juce::jmax(l, meterL * 0.88f);
    meterR = juce::jmax(r, meterR * 0.88f);
    repaint(meterRect);
    repaint(lcdRect);
}

void TrackStrip::paint(juce::Graphics& g)
{
    brushedMetal(g, getLocalBounds().toFloat().reduced(1.f), 5.f, true, *tex);

    for (auto& c : captions)
        tag(g, c.text, c.rect.toFloat(), juce::Justification::centred, 7.5f, col::textFaint);

    if (keyText.isNotEmpty())
    {
        g.setColour(col::amber.withAlpha(0.15f));
        g.fillRoundedRectangle(keyRect.toFloat(), 3.f);
        tag(g, "SIDECHAIN KEY OUT > " + keyText, keyRect.toFloat(), juce::Justification::centred, 7.5f, col::amber);
    }

    // LCD with the fader value in dB
    glassWell(g, lcdRect.toFloat(), 3.f);
    const float db = fader::positionToDb(ed.proc.volumeParam(track)->get());
    sevenSegment(g, db <= -69.0f ? "-70.0" : juce::String(db, 1), lcdRect.toFloat().reduced(6.f, 3.f), col::amber, col::amberGhost);

    // meter
    glassWell(g, meterRect.toFloat(), 3.f);
    auto m = meterRect.reduced(4, 5);
    auto draw = [&](juce::Rectangle<int> a, float peak)
    {
        const float db2 = juce::Decibels::gainToDecibels(peak, -60.0f);
        const int segs = 28, sh = a.getHeight() / segs;
        const int lit = juce::roundToInt(juce::jlimit(0.0f, 1.0f, (db2 + 60.0f) / 66.0f) * (float) segs);
        for (int i = 0; i < segs; ++i)
        {
            const float segDb = -60.0f + 66.0f * (float) i / (float) segs;
            const auto c = segDb > 0.0f ? col::wrong : segDb > -12.0f ? col::amber : col::cyan;
            g.setColour(i < lit ? c : col::amberGhost);
            g.fillRect(a.getX(), a.getBottom() - (i + 1) * sh, a.getWidth(), juce::jmax(1, sh - 1));
        }
    };
    draw(m.removeFromLeft(m.getWidth() / 2 - 1), meterL);
    m.removeFromLeft(2);
    draw(m, meterR);
}
}
