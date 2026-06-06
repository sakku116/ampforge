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

    /** Additional directories added by the user; merged with defaults on each scan. */
    void setCustomPaths(const juce::StringArray& paths) { customPaths = paths; }
    const juce::StringArray& getCustomPaths() const { return customPaths; }

    const juce::KnownPluginList& getKnownPluginList() const { return knownPluginList; }
    juce::KnownPluginList& getKnownPluginList() { return knownPluginList; }

private:
    juce::AudioPluginFormatManager& formatManager;
    juce::KnownPluginList knownPluginList;
    juce::StringArray customPaths;

    void scanFormatOnPaths(juce::AudioPluginFormat& format,
                            const juce::FileSearchPath& searchPath,
                            const char* flagToUse,
                            const juce::String& formatLabel);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanner)
};
