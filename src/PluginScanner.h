#pragma once

#include <JuceHeader.h>

class PluginScanner
{
public:
    explicit PluginScanner(juce::AudioPluginFormatManager& formatManager);

    /** Clear list then scan all registered formats (VST3 + VST2 if SDK was included). */
    void scanAll();

    void scanDefaultWindowsVST3Folders();
    void scanDefaultWindowsVST2Folders();

    const juce::KnownPluginList& getKnownPluginList() const { return knownPluginList; }
    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }

private:
    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList knownPluginList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanner)
};
