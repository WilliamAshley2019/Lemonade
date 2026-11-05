#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Hosting/CrashGuard.h"

namespace lemon
{
// One window per hosted plugin. Windows are hidden (not deleted) when closed so the plugin editor
// keeps its state. closeFor() must be called before the plugin instance is destroyed.
class PluginWindowManager
{
public:
    ~PluginWindowManager() { closeAll(); }

    void show(juce::AudioProcessor* p, const juce::String& title)
    {
        for (auto& w : windows)
            if (w->owner == p) { w->setVisible(true); w->toFront(true); return; }

        auto w = std::make_unique<Window>(p, title);
        windows.push_back(std::move(w));
    }

    void closeFor(juce::AudioProcessor* p)
    {
        for (auto it = windows.begin(); it != windows.end(); ++it)
            if ((*it)->owner == p) { (*it)->clearContentComponent(); windows.erase(it); return; }
    }

    void closeAll()
    {
        for (auto& w : windows) w->clearContentComponent();
        windows.clear();
    }

private:
    struct Window : juce::DocumentWindow
    {
        Window(juce::AudioProcessor* p, const juce::String& title)
            : DocumentWindow(title, juce::Colours::darkgrey, DocumentWindow::closeButton), owner(p)
        {
            CrashGuard::instance().begin(this, CrashGuard::idFor(p), "gui", title);
            juce::AudioProcessorEditor* ed = p->hasEditor() ? p->createEditorIfNeeded() : nullptr;
            if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor(*p);
            setContentOwned(ed, true);
            setUsingNativeTitleBar(true);
            setResizable(ed->isResizable(), false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        ~Window() override { CrashGuard::instance().endOwned(this); }
        void closeButtonPressed() override { setVisible(false); }
        juce::AudioProcessor* owner;
    };
    std::vector<std::unique_ptr<Window>> windows;
};
}
