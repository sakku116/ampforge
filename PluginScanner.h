#pragma once

#include <JuceHeader.h>

class PluginScanner
{
public:
    explicit PluginScanner(juce::AudioPluginFormatManager& formatManager);

    void scanDefaultWindowsVST3Folders();

    const juce::KnownPluginList& getKnownPluginList() const { return knownPluginList; }
    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }

private:
    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList knownPluginList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanner)
};
