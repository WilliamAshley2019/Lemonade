#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace lemon::ui
{
// Hosted plugin GUIs docked inside the mixer window. Each card is tied to a track/slot, can be
// minimized to its title bar or maximized to fill the dock, popped out into its own window, or closed.
class PluginDock : public juce::Component
{
public:
    PluginDock();
    ~PluginDock() override;

    void open(juce::AudioProcessor*, const juce::String& title, int track, int slot);
    bool isOpen(juce::AudioProcessor*) const;
    bool hasSlot(int track, int slot) const;
    void closeFor(juce::AudioProcessor*);      // safe to call from anywhere on the message thread, not from the card's own buttons
    void closeAll();
    bool empty() const noexcept;

    int desiredHeight() const;                 // 0 when empty
    std::function<void()> onLayoutChanged;
    std::function<void(juce::AudioProcessor*)> onFloat;      // user asked to pop a card out
    std::function<void()> onClosed;                          // a card went away (strips refresh their "docked" highlight)

    void paint(juce::Graphics&) override;
    void resized() override;

    struct Card;
private:
    void layoutChanged();
    juce::OwnedArray<Card> cards;
    juce::LookAndFeel_V4 plainLaf;     // hosted GUIs that do not set their own look get a neutral one
    friend struct Card;
};
}
