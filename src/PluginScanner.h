#pragma once

#include <JuceHeader.h>
#include <map>

class PluginScanner
{
public:
    explicit PluginScanner(juce::AudioPluginFormatManager& formatManager);

    /** Scan all registered formats (VST3 + VST2 if SDK was included).

        Incremental by default: loads the on-disk cache and only spawns a scan
        subprocess for files that are new or whose modification time changed.
        Pass @p forceRescan = true (the "Rescan" button) to clear everything and
        re-scan every file from scratch.  Either way the result is written back
        to the cache on completion. */
    void scanAll(bool forceRescan = false);

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

    // path -> last-modification time (ms) of every file we have already attempted
    // to scan (success, failure or crash). Lets us skip unchanged files next launch.
    std::map<juce::String, juce::int64> scannedFiles;
    bool cacheLoaded = false;

    void scanFormatOnPaths(juce::AudioPluginFormat& format,
                            const juce::FileSearchPath& searchPath,
                            const char* flagToUse,
                            const juce::String& formatLabel);

    // --- on-disk cache (%APPDATA%/AmpForge/pluginCache.xml) ---
    static juce::File getCacheFile();
    void loadCache();
    void saveCache() const;
    void pruneMissingFiles();
    void removeTypesForFile(const juce::String& path);
    bool isUpToDate(const juce::String& path) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanner)
};
