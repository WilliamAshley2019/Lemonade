#include "PluginDock.h"
#include "../Hosting/CrashGuard.h"

namespace lemon::ui
{
namespace { constexpr int kTitleH = 22, kMinW = 200, kMaxDockH = 560, kPad = 4; }

struct PluginDock::Card : public juce::Component, private juce::ComponentListener
{
    Card(PluginDock& d, juce::AudioProcessor* p, const juce::String& t, int tr, int sl)
        : dock(d), owner(p), title(t), track(tr), slot(sl)
    {
        lemon::CrashGuard::instance().begin(this, lemon::CrashGuard::idFor(p), "gui", t);
        juce::AudioProcessorEditor* e = p->hasEditor() ? p->createEditorIfNeeded() : nullptr;
        if (e == nullptr) e = new juce::GenericAudioProcessorEditor(*p);
        ed = e;
        view.setLookAndFeel(&d.plainLaf);
        view.setScrollBarsShown(true, true);
        view.setViewedComponent(e, true);          // the viewport owns (and deletes) the editor
        e->addComponentListener(this);
        addAndMakeVisible(view);

        auto setup = [this](juce::TextButton& b, const char* tip) { b.setTooltip(tip); addAndMakeVisible(b); };
        setup(minB, "Minimize to title bar");
        setup(maxB, "Maximize / restore");
        setup(floatB, "Pop out into its own window");
        setup(closeB, "Close this plugin view");
        minB.onClick = [this] { minimized = ! minimized; if (minimized) maximized = false; dock.layoutChanged(); };
        maxB.onClick = [this] { maximized = ! maximized; if (maximized) minimized = false; dock.layoutChanged(); };
        floatB.onClick = [this]
        {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginDock>(&dock), p = owner]
            { if (safe != nullptr && safe->onFloat) safe->onFloat(p); });
        };
        closeB.onClick = [this]
        {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<PluginDock>(&dock), p = owner]
            { if (safe != nullptr) safe->closeFor(p); });
        };
    }

    ~Card() override
    {
        lemon::CrashGuard::instance().endOwned(this);
        if (ed != nullptr) ed->removeComponentListener(this);
        view.setViewedComponent(nullptr);          // deletes the plugin editor now, before its plugin can go
        view.setLookAndFeel(nullptr);
    }

    int bodyW() const { return ed ? ed->getWidth() + 14 : kMinW; }
    int bodyH() const { return ed ? ed->getHeight() + 14 : 120; }
    int preferredW() const { return minimized ? kMinW : juce::jmax(kMinW, bodyW()); }
    int preferredH() const { return minimized ? kTitleH : kTitleH + bodyH(); }

    void paint(juce::Graphics& g) override
    {
        static juce::SharedResourcePointer<Textures> tex;
        auto r = getLocalBounds().toFloat();
        brushedMetal(g, r.removeFromTop((float) kTitleH), 3.f, true, *tex);
        g.setColour(col::glass);
        g.fillRect(r);
        g.setColour(col::edgeLight.withAlpha(0.4f));
        g.drawRect(getLocalBounds(), 1);
        g.setColour(col::amber);                                  // "snapped to slot" LED
        g.fillEllipse(7.f, 8.f, 6.f, 6.f);
        engravedText(g, "T" + juce::String(track) + " / S" + juce::String(slot + 1) + "  " + title,
                     { 18.f, 0.f, (float) getWidth() - 160.f, (float) kTitleH }, juce::Justification::centredLeft,
                     caps(10.f), true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto t = r.removeFromTop(kTitleH).reduced(2);
        closeB.setBounds(t.removeFromRight(22));
        floatB.setBounds(t.removeFromRight(44));
        maxB.setBounds(t.removeFromRight(36));
        minB.setBounds(t.removeFromRight(36));
        view.setBounds(r.reduced(1));
        view.setVisible(! minimized);
    }

    PluginDock& dock;
    juce::AudioProcessor* owner;
    juce::String title;
    int track, slot;
    bool minimized = false, maximized = false;
    juce::AudioProcessorEditor* ed = nullptr;
    juce::Viewport view;
    juce::TextButton minB { "MIN" }, maxB { "MAX" }, floatB { "FLOAT" }, closeB { "X" };

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override
    {
        if (wasResized) dock.layoutChanged();
    }
};

PluginDock::PluginDock() { setInterceptsMouseClicks(false, true); }
PluginDock::~PluginDock() { closeAll(); }

bool PluginDock::empty() const noexcept { return cards.isEmpty(); }

void PluginDock::open(juce::AudioProcessor* p, const juce::String& title, int track, int slot)
{
    for (auto* c : cards)
        if (c->owner == p)
        {
            c->minimized = false;                  // clicking the name again just brings it back
            layoutChanged();
            return;
        }
    auto* c = cards.add(new Card(*this, p, title, track, slot));
    addAndMakeVisible(c);
    layoutChanged();
}

bool PluginDock::isOpen(juce::AudioProcessor* p) const
{
    for (auto* c : cards) if (c->owner == p) return true;
    return false;
}

bool PluginDock::hasSlot(int track, int slot) const
{
    for (auto* c : cards) if (c->track == track && c->slot == slot) return true;
    return false;
}

void PluginDock::closeFor(juce::AudioProcessor* p)
{
    for (int i = cards.size(); --i >= 0;)
        if (cards[i]->owner == p) cards.remove(i);
    layoutChanged();
    if (onClosed) onClosed();
}

void PluginDock::closeAll()
{
    cards.clear();
}

int PluginDock::desiredHeight() const
{
    if (cards.isEmpty()) return 0;
    int h = kTitleH;
    for (auto* c : cards) h = juce::jmax(h, c->preferredH());
    return juce::jmin(h, kMaxDockH) + kPad;
}

void PluginDock::layoutChanged()
{
    resized();
    repaint();
    if (onLayoutChanged) onLayoutChanged();
}

void PluginDock::paint(juce::Graphics& g)
{
    if (! cards.isEmpty()) g.fillAll(col::bg);
}

void PluginDock::resized()
{
    auto r = getLocalBounds().withTrimmedTop(kPad);
    Card* maxed = nullptr;
    for (auto* c : cards) if (c->maximized) maxed = c;

    for (auto* c : cards) c->setVisible(maxed == nullptr || c == maxed);
    if (maxed != nullptr) { maxed->setBounds(r); return; }

    int total = 0;
    for (auto* c : cards) total += c->preferredW() + 4;
    const float scale = total > r.getWidth() ? (float) r.getWidth() / (float) total : 1.0f;

    int x = r.getX();
    for (auto* c : cards)
    {
        const int w = juce::jmax(kMinW, juce::roundToInt((float) c->preferredW() * scale));
        c->setBounds(x, r.getY(), w, c->minimized ? kTitleH : r.getHeight());
        x += w + 4;
    }
}
}
