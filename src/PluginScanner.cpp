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

void PluginScanner::scanDefaultWindowsVST3Folders()
{
    HostDebug::log("VST3 scan started");

    juce::AudioPluginFormat* vst3Format = nullptr;

    for (auto* format : formatManager.getFormats())
    {
        if (format->getName().containsIgnoreCase("VST3"))
        {
            vst3Format = format;
            break;
        }
    }

    if (vst3Format == nullptr)
    {
        HostDebug::log("VST3 scan aborted: no VST3 format registered");
        return;
    }

    juce::StringArray paths;

   #if JUCE_WINDOWS
    paths.add("C:\\Program Files\\Common Files\\VST3");
    paths.add("C:\\Program Files (x86)\\Common Files\\VST3");
   #endif

    juce::FileSearchPath searchPath;

    for (const auto& path : paths)
        searchPath.add(path);

    // JUCE search finds both bundle folders (AmpliTube) and single-file .vst3 DLLs
    // (Neural DSP, Overloud, Tonocracy). findChildFiles(findDirectories) missed the latter.
    const auto pluginPaths = vst3Format->searchPathsForPlugins(searchPath, true, false);

    HostDebug::log("VST3 paths on disk: " + juce::String(pluginPaths.size()));

    for (const auto& pluginPath : pluginPaths)
        HostDebug::log("  found: " + pluginPath);

    int scanFailures = 0;
    int scanCrashes = 0;

    for (const auto& pluginPath : pluginPaths)
    {
        const auto typesBefore = knownPluginList.getNumTypes();

        if (! ScanSubprocess::scanFileInChildProcess(pluginPath, knownPluginList))
        {
            ++scanCrashes;
            HostDebug::log("  scan CRASHED or worker failed (skipped): " + pluginPath);
            continue;
        }

        const auto added = knownPluginList.getNumTypes() - typesBefore;

        if (added <= 0)
        {
            ++scanFailures;
            HostDebug::log("  scan failed (0 types): " + pluginPath);
        }
    }

    HostDebug::log("VST3 scan done — " + juce::String(knownPluginList.getNumTypes()) + " plugin(s), "
                   + juce::String(scanFailures) + " path(s) with no types, "
                   + juce::String(scanCrashes) + " crash(es)");
}

void PluginScanner::scanDefaultWindowsVST2Folders()
{
    HostDebug::log("VST2 scan started");

    juce::AudioPluginFormat* vstFormat = nullptr;

    for (auto* format : formatManager.getFormats())
    {
        if (format->getName() == "VST")
        {
            vstFormat = format;
            break;
        }
    }

    if (vstFormat == nullptr)
    {
        HostDebug::log("VST2 scan skipped: VSTPluginFormat not registered (SDK not included at build time)");
        return;
    }

    juce::StringArray paths;

   #if JUCE_WINDOWS
    paths.add("C:\\Program Files\\VSTPlugins");
    paths.add("C:\\Program Files (x86)\\VSTPlugins");
    paths.add("C:\\Program Files\\Steinberg\\VSTPlugins");
    paths.add("C:\\Program Files (x86)\\Steinberg\\VSTPlugins");
    paths.add("C:\\Program Files\\Common Files\\VST2");
   #endif

    juce::FileSearchPath searchPath;

    for (const auto& path : paths)
        searchPath.add(path);

    const auto pluginPaths = vstFormat->searchPathsForPlugins(searchPath, true, false);

    HostDebug::log("VST2 paths on disk: " + juce::String(pluginPaths.size()));

    for (const auto& pluginPath : pluginPaths)
        HostDebug::log("  found: " + pluginPath);

    int scanFailures = 0;
    int scanCrashes  = 0;

    for (const auto& pluginPath : pluginPaths)
    {
        const auto typesBefore = knownPluginList.getNumTypes();

        if (! ScanSubprocess::scanFileInChildProcess(pluginPath, knownPluginList,
                                                      ScanSubprocess::scanVst2Flag))
        {
            ++scanCrashes;
            HostDebug::log("  scan CRASHED or worker failed (skipped): " + pluginPath);
            continue;
        }

        const auto added = knownPluginList.getNumTypes() - typesBefore;

        if (added <= 0)
        {
            ++scanFailures;
            HostDebug::log("  scan failed (0 types): " + pluginPath);
        }
    }

    HostDebug::log("VST2 scan done — " + juce::String(knownPluginList.getNumTypes()) + " total plugin(s), "
                   + juce::String(scanFailures) + " path(s) with no types, "
                   + juce::String(scanCrashes) + " crash(es)");
}
