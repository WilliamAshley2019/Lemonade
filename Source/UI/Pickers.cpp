#include "Pickers.h"

namespace lemon::ui
{
// ------------------------------------------------------------------ PluginPicker
PluginPicker::PluginPicker(juce::KnownPluginList& l, std::function<void(const juce::PluginDescription&)> p,
                           std::function<void()> s)
    : known(l), pick(std::move(p))
{
    setSize(360, 440);
    filter.setTextToShowWhenEmpty("search plugins...", col::textFaint);
    filter.setFont(sans(13.f));
    filter.onTextChange = [this] { refresh(); };
    addAndMakeVisible(filter);

    listBox.setRowHeight(22);
    listBox.getVerticalScrollBar().setAutoHide(false);
    addAndMakeVisible(listBox);

    scanButton.onClick = [s] { if (s) s(); };
    scanButton.setTooltip("Scan the default VST3 folders for new plugins");
    addAndMakeVisible(scanButton);

    count.setFont(caps(9.f));
    count.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(count);

    known.addChangeListener(this);
    refresh();
}

PluginPicker::~PluginPicker() { known.removeChangeListener(this); }

void PluginPicker::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);
    tag(g, "LOAD PLUGIN  -  CLICK TO INSERT", getLocalBounds().removeFromTop(22).toFloat(), juce::Justification::centred, 9.f, col::amber);
}

void PluginPicker::resized()
{
    auto r = getLocalBounds().reduced(6);
    r.removeFromTop(18);
    auto top = r.removeFromTop(24);
    scanButton.setBounds(top.removeFromRight(56));
    top.removeFromRight(4);
    filter.setBounds(top);
    r.removeFromTop(4);
    count.setBounds(r.removeFromBottom(16));
    listBox.setBounds(r);
}

void PluginPicker::refresh()
{
    shown.clear();
    const auto q = filter.getText().trim();
    for (auto& d : known.getTypes())
    {
        if (d.isInstrument || d.numInputChannels <= 0) continue;          // effects only
        if (q.isNotEmpty() && ! (d.name.containsIgnoreCase(q) || d.manufacturerName.containsIgnoreCase(q))) continue;
        shown.push_back(d);
    }
    std::sort(shown.begin(), shown.end(), [](const auto& a, const auto& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    listBox.updateContent();
    listBox.repaint();
    count.setText(juce::String((int) shown.size()) + " effects" + (known.getTypes().isEmpty() ? "  -  press SCAN" : ""),
                  juce::dontSendNotification);
}

void PluginPicker::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) shown.size()) return;
    if (selected) { g.setColour(col::amber.withAlpha(0.2f)); g.fillRect(0, 0, w, h); }
    else if (row % 2) { g.setColour(juce::Colours::white.withAlpha(0.025f)); g.fillRect(0, 0, w, h); }
    g.setFont(sans(13.f));
    g.setColour(selected ? col::amber : col::text);
    g.drawText(shown[(size_t) row].name, 8, 0, w * 6 / 10 - 8, h, juce::Justification::centredLeft, true);
    g.setFont(sans(11.f));
    g.setColour(col::textDim);
    g.drawText(shown[(size_t) row].manufacturerName, w * 6 / 10, 0, w * 4 / 10 - 6, h, juce::Justification::centredRight, true);
}

void PluginPicker::choose(int row)
{
    if (row < 0 || row >= (int) shown.size()) return;
    const auto d = shown[(size_t) row];
    if (pick) pick(d);
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
