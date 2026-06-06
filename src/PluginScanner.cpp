#include "PluginScanner.h"
#include "HostDebug.h"
#include "ScanSubprocess.h"

PluginScanner::PluginScanner(juce::AudioPluginFormatManager& manager) : formatManager(manager)
{
}

void PluginScanner::scanAll()
{
    knownPluginList.clear();
    scanDefaultWindowsVST3Folders();
    scanDefaultWindowsVST2Folders();
}

void PluginScanner::scanFormatOnPaths(juce::AudioPluginFormat& format,
                                       const juce::FileSearchPath& searchPath,
                                       const char* flagToUse,
                                       const juce::String& formatLabel)
{
    // JUCE search finds both bundle folders and single-file DLLs.
    const auto pluginPaths = format.searchPathsForPlugins(searchPath, true, false);

    HostDebug::log(formatLabel + " paths on disk: " + juce::String(pluginPaths.size()));

    for (const auto& p : pluginPaths)
        HostDebug::log("  found: " + p);

    int failures = 0, crashes = 0;

    for (const auto& p : pluginPaths)
    {
        const auto before = knownPluginList.getNumTypes();

        if (! ScanSubprocess::scanFileInChildProcess(p, knownPluginList, flagToUse))
        {
            ++crashes;
            HostDebug::log("  scan CRASHED or worker failed (skipped): " + p);
            continue;
        }

        if (knownPluginList.getNumTypes() - before <= 0)
        {
            ++failures;
            HostDebug::log("  scan failed (0 types): " + p);
        }
    }

    HostDebug::log(formatLabel + " scan done — "
                   + juce::String(knownPluginList.getNumTypes()) + " total plugin(s), "
                   + juce::String(failures) + " path(s) with no types, "
                   + juce::String(crashes) + " crash(es)");
}

void PluginScanner::scanDefaultWindowsVST3Folders()
{
    HostDebug::log("VST3 scan started");

    juce::AudioPluginFormat* vst3Format = nullptr;

    for (auto* f : formatManager.getFormats())
        if (f->getName().containsIgnoreCase("VST3"))
            { vst3Format = f; break; }

    if (vst3Format == nullptr)
    {
        HostDebug::log("VST3 scan aborted: no VST3 format registered");
        return;
    }

    juce::FileSearchPath searchPath;

   #if JUCE_WINDOWS
    searchPath.add(juce::File("C:\\Program Files\\Common Files\\VST3"));
    searchPath.add(juce::File("C:\\Program Files (x86)\\Common Files\\VST3"));
   #endif

    for (const auto& p : customPaths)
        searchPath.add(juce::File(p));

    scanFormatOnPaths(*vst3Format, searchPath, ScanSubprocess::scanVst3Flag, "VST3");
}

void PluginScanner::scanDefaultWindowsVST2Folders()
{
    HostDebug::log("VST2 scan started");

    juce::AudioPluginFormat* vstFormat = nullptr;

    for (auto* f : formatManager.getFormats())
        if (f->getName() == "VST")
            { vstFormat = f; break; }

    if (vstFormat == nullptr)
    {
        HostDebug::log("VST2 scan skipped: VSTPluginFormat not registered (SDK not included at build time)");
        return;
    }

    juce::FileSearchPath searchPath;

   #if JUCE_WINDOWS
    searchPath.add(juce::File("C:\\Program Files\\VSTPlugins"));
    searchPath.add(juce::File("C:\\Program Files (x86)\\VSTPlugins"));
    searchPath.add(juce::File("C:\\Program Files\\Steinberg\\VSTPlugins"));
    searchPath.add(juce::File("C:\\Program Files (x86)\\Steinberg\\VSTPlugins"));
    searchPath.add(juce::File("C:\\Program Files\\Common Files\\VST2"));
   #endif

    for (const auto& p : customPaths)
        searchPath.add(juce::File(p));

    scanFormatOnPaths(*vstFormat, searchPath, ScanSubprocess::scanVst2Flag, "VST2");
}
