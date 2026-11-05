#include "PluginScanner.h"

namespace lemon
{
juce::File PluginScanner::dataFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("AlphaAudio").getChildFile("LemonMixerRack");
}

PluginScanner::PluginScanner() : juce::Thread("Lemon plugin scan")
{
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());   // needs JUCE_PLUGINHOST_VST3=1
    dataFolder().createDirectory();
    if (auto xml = juce::XmlDocument::parse(dataFolder().getChildFile("plugins.xml")))
        known.recreateFromXml(*xml);
}

PluginScanner::~PluginScanner()
{
    stopThread(5000);
    save();
}

void PluginScanner::save()
{
    if (auto xml = known.createXml())
        xml->writeTo(dataFolder().getChildFile("plugins.xml"));
}

void PluginScanner::scan(const juce::StringArray& folders, std::function<void(const juce::String&)> progress,
                         std::function<void()> done)
{
    if (isThreadRunning())
        return;
    foldersToScan = folders;
    onProgress = std::move(progress);
    onDone = std::move(done);
    startThread();
}

void PluginScanner::run()
{
    auto pedal = dataFolder().getChildFile("scan.pedal");

    // Anything that crashed the previous scan is on the pedal file: blacklist it.
    if (pedal.existsAsFile())
        for (auto& line : juce::StringArray::fromLines(pedal.loadFileAsString()))
            if (line.isNotEmpty())
                known.addToBlacklist(line);

    for (int i = 0; i < formatManager.getNumFormats() && ! threadShouldExit(); ++i)
    {
        auto* fmt = formatManager.getFormat(i);
        juce::FileSearchPath paths;
        if (foldersToScan.isEmpty())
            paths = fmt->getDefaultLocationsToSearch();
        else
            for (auto& f : foldersToScan) paths.add(juce::File(f));

        juce::PluginDirectoryScanner scanner(known, *fmt, paths, true, pedal, true);
        juce::String name;
        while (! threadShouldExit() && scanner.scanNextFile(true, name))
        {
            if (onProgress)
                juce::MessageManager::callAsync([cb = onProgress, name] { cb(name); });
            save();
        }
    }
    pedal.deleteFile();
    save();
    if (onDone)
        juce::MessageManager::callAsync([cb = onDone] { cb(); });
}
}
