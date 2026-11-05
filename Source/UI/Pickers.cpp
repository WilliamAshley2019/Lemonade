#include "Pickers.h"
#include "../Hosting/CrashGuard.h"

namespace lemon::ui
{
// ------------------------------------------------------------------ PluginPicker
PluginPicker::PluginPicker(PluginScanner& sc, std::function<void(const CatalogEntry&)> p, std::function<void()> s,
                           std::function<void()> imp, std::function<void()> loc, std::function<void()> org)
    : scanner(sc), pick(std::move(p)), scanCb(std::move(s)), importCb(std::move(imp)), locateCb(std::move(loc)), orgCb(std::move(org))
{
    setSize(480, 560);
    filter.setTextToShowWhenEmpty("search name, vendor, category or FL folder...", col::textFaint);
    filter.setFont(sans(13.f));
    filter.onTextChange = [this] { refresh(); };
    addAndMakeVisible(filter);

    favOnly.setClickingTogglesState(true);
    favOnly.setTooltip("Only plugins you filed in your FL Studio favourite folders");
    favOnly.onClick = [this] { refresh(); };
    hideFlagged.setClickingTogglesState(true);
    hideFlagged.setTooltip("Hide plugins that crashed the host (flagged !)");
    hideFlagged.onClick = [this] { refresh(); };
    addAndMakeVisible(favOnly);
    addAndMakeVisible(hideFlagged);

    listBox.setRowHeight(22);
    listBox.getVerticalScrollBar().setAutoHide(false);
    addAndMakeVisible(listBox);

    scanButton.setTooltip("Walk the default VST3 folders and add new plugins");
    scanButton.onClick = [this] { if (scanCb) scanCb(); };
    importButton.setTooltip("Add the VST3 plugins FL Studio already knows about (skips plugins FL could not load)");
    importButton.onClick = [this] { if (importCb) importCb(); };
    locateButton.setTooltip("Point at your FL Studio 'Plugin database' folder");
    locateButton.onClick = [this] { if (locateCb) locateCb(); };
    orgButton.setTooltip("Link your FLP Organizer database (read-only) to add its categories and plugin paths");
    orgButton.onClick = [this] { if (orgCb) orgCb(); };
    for (auto* b : { &scanButton, &importButton, &locateButton, &orgButton }) addAndMakeVisible(b);

    status.setFont(caps(8.5f));
    status.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(status);

    refresh();
    startTimer(600);
}

void PluginPicker::timerCallback()
{
    const auto fl = scanner.flData();
    const int known = scanner.list().getNumTypes();
    const int q = CrashGuard::instance().count();
    if (fl.get() != lastFl || known != lastKnown || q != lastQuarantine) refresh();
}

void PluginPicker::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);
    tag(g, "LOAD PLUGIN  -  CLICK A ROW", getLocalBounds().removeFromTop(22).toFloat(), juce::Justification::centred, 9.f, col::amber);
}

void PluginPicker::resized()
{
    auto r = getLocalBounds().reduced(6);
    r.removeFromTop(18);
    auto top = r.removeFromTop(24);
    hideFlagged.setBounds(top.removeFromRight(92));
    top.removeFromRight(4);
    favOnly.setBounds(top.removeFromRight(104));
    top.removeFromRight(4);
    filter.setBounds(top);
    r.removeFromTop(4);

    auto bottom = r.removeFromBottom(24);
    orgButton.setBounds(bottom.removeFromRight(80));
    bottom.removeFromRight(4);
    locateButton.setBounds(bottom.removeFromRight(66));
    bottom.removeFromRight(4);
    importButton.setBounds(bottom.removeFromRight(118));
    bottom.removeFromRight(4);
    scanButton.setBounds(bottom.removeFromRight(96));
    r.removeFromBottom(4);
    status.setBounds(r.removeFromBottom(16));
    listBox.setBounds(r);
}

void PluginPicker::refresh()
{
    lastFl = scanner.flData().get();
    lastKnown = scanner.list().getNumTypes();
    lastQuarantine = CrashGuard::instance().count();

    const auto q = filter.getText().trim();
    shown.clear();
    for (auto& e : scanner.catalog())
    {
        if (favOnly.getToggleState() && ! e.favourite) continue;
        if (hideFlagged.getToggleState() && e.quarantined) continue;
        if (q.isNotEmpty() && ! (e.desc.name.containsIgnoreCase(q) || e.vendor.containsIgnoreCase(q)
                                 || e.flCategory.containsIgnoreCase(q) || e.favouriteFolder.containsIgnoreCase(q)
                                 || e.orgCategory.containsIgnoreCase(q)))
            continue;
        shown.push_back(e);
    }
    std::sort(shown.begin(), shown.end(), [](const CatalogEntry& a, const CatalogEntry& b)
    {
        if (a.favourite != b.favourite) return a.favourite;
        return a.desc.name.compareIgnoreCase(b.desc.name) < 0;
    });
    listBox.updateContent();
    listBox.repaint();

    const auto fl = scanner.flData();
    juce::String st = juce::String((int) shown.size()) + " EFFECTS";
    if (! fl) st << "  -  READING FL DATABASE...";
    else if (! fl->found) st << "  -  FL DATABASE NOT FOUND (FL DB... TO LOCATE)";
    else st << "  -  FL KNOWS " << (int) fl->installed.size() << ", " << scanner.missingFromKnown() << " NOT IMPORTED YET";
    if (auto org = scanner.orgData()) st << (org->ok ? "  -  ORGANIZER: " + juce::String((int) org->plugins.size()) : juce::String("  -  ORGANIZER DB: SEE STATUS"));
    if (lastQuarantine > 0) st << "  -  " << lastQuarantine << " FLAGGED";
    if (scanner.list().getNumTypes() == 0) st << "  -  PRESS SCAN OR IMPORT";
    status.setText(st, juce::dontSendNotification);

    const int missing = fl ? scanner.missingFromKnown() : 0;
    importButton.setEnabled(missing > 0 && ! scanner.isScanning());
    importButton.setButtonText(missing > 0 ? "IMPORT " + juce::String(missing) + " FROM FL" : "IMPORT FROM FL");
    importButton.setAlpha(importButton.isEnabled() ? 1.0f : 0.4f);
}

void PluginPicker::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    auto& e = shown[(size_t) row];
    if (selected) { g.setColour(col::amber.withAlpha(0.2f)); g.fillRect(0, 0, w, h); }
    else if (row % 2) { g.setColour(juce::Colours::white.withAlpha(0.025f)); g.fillRect(0, 0, w, h); }

    int x = 6;
    if (e.quarantined) { g.setColour(col::wrong); g.setFont(sans(13.f, true)); g.drawText("!", x, 0, 12, h, juce::Justification::centred); }
    else if (e.favourite) { g.setColour(col::amber); g.setFont(sans(13.f)); g.drawText(juce::CharPointer_UTF8("\xE2\x98\x85"), x, 0, 12, h, juce::Justification::centred); }
    x += 16;

    const int nameW = (w - x) * 45 / 100, vendW = (w - x) * 26 / 100;
    g.setFont(sans(13.f));
    g.setColour(e.quarantined ? col::textFaint : selected ? col::amber : col::text);
    g.drawText(e.desc.name, x, 0, nameW - 4, h, juce::Justification::centredLeft, true);
    g.setFont(sans(11.f));
    g.setColour(col::textDim);
    g.drawText(e.vendor, x + nameW, 0, vendW - 4, h, juce::Justification::centredLeft, true);
    const auto cat = e.favouriteFolder.isNotEmpty() ? e.favouriteFolder : e.flCategory.isNotEmpty() ? e.flCategory : e.orgCategory;
    g.setColour(e.favourite ? col::amberDim.brighter(0.5f) : col::textFaint);
    g.drawText(cat, x + nameW + vendW, 0, w - x - nameW - vendW - 6, h, juce::Justification::centredRight, true);
}

void PluginPicker::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) shown.size()) return;
    if (! e.mods.isPopupMenu()) { choose(row); return; }

    const auto entry = shown[(size_t) row];
    juce::PopupMenu m;
    m.addItem(1, entry.quarantined ? "Release from quarantine" : "Flag as crashing (quarantine)");
    m.showMenuAsync(juce::PopupMenu::Options(), [this, entry](int r)
    {
        if (r != 1) return;
        auto& g = CrashGuard::instance();
        if (entry.quarantined) g.release(entry.id);
        else g.quarantine(entry.id, entry.desc.name, "flagged by you");
        refresh();
    });
}

void PluginPicker::choose(int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto e = shown[(size_t) row];
    if (pick) pick(e);
    if (auto* c = findParentComponentOfClass<juce::CallOutBox>()) c->dismiss();
}

// ------------------------------------------------------------------ TrackPicker
TrackPicker::TrackPicker(const juce::StringArray& names, const std::vector<int>& choices, int current,
                         const juce::String& noneLabel, std::function<void(int)> onPick)
{
    auto add = [&](const juce::String& text, int value, bool on)
    {
        auto* b = buttons.add(new juce::TextButton(text));
        b->setToggleState(on, juce::dontSendNotification);
        b->onClick = [this, onPick, value]
        {
            if (onPick) onPick(value);
            if (auto* c = findParentComponentOfClass<juce::CallOutBox>()) c->dismiss();
        };
        addAndMakeVisible(b);
    };
    if (noneLabel.isNotEmpty()) add(noneLabel, -1, current < 0);
    for (int t : choices) add(names[t], t, t == current);

    const int cols = 3, rows = (buttons.size() + cols - 1) / cols;
    setSize(cols * 82 + 12, 24 + rows * 26 + 8);
}

void TrackPicker::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);
    tag(g, "CHOOSE TRACK", getLocalBounds().removeFromTop(22).toFloat(), juce::Justification::centred, 9.f, col::amber);
}

void TrackPicker::resized()
{
    auto r = getLocalBounds().reduced(6);
    r.removeFromTop(20);
    const int cols = 3, w = r.getWidth() / cols;
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds(r.getX() + (i % cols) * w, r.getY() + (i / cols) * 26, w - 2, 24);
}
}
