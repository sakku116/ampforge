#include "PluginScanner.h"
#include "HostDebug.h"
#include "ScanSubprocess.h"

PluginScanner::PluginScanner(juce::AudioPluginFormatManager& manager) : formatManager(manager)
{
}

void PluginScanner::scanAll(bool forceRescan)
{
    if (forceRescan)
    {
        knownPluginList.clear();
        scannedFiles.clear();
        cacheLoaded = true;   // nothing to load; we are rebuilding from scratch
        HostDebug::log("Plugin scan: FULL rescan requested");
    }
    else
    {
        loadCache();
        HostDebug::log("Plugin scan: incremental (cached "
                       + juce::String(knownPluginList.getNumTypes()) + " plugin(s), "
                       + juce::String((int) scannedFiles.size()) + " known file(s))");
    }

    scanDefaultWindowsVST3Folders();
    scanDefaultWindowsVST2Folders();

    pruneMissingFiles();
    saveCache();
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

    int failures = 0, crashes = 0, skipped = 0;

    for (const auto& p : pluginPaths)
    {
        if (isUpToDate(p))
        {
            ++skipped;   // unchanged since last scan — no subprocess needed
            continue;
        }

        // File is new or changed: drop any stale entries before re-scanning.
        removeTypesForFile(p);

        const auto before = knownPluginList.getNumTypes();

        if (! ScanSubprocess::scanFileInChildProcess(p, knownPluginList, flagToUse))
        {
            ++crashes;
            HostDebug::log("  scan CRASHED or worker failed (skipped): " + p);
        }
        else if (knownPluginList.getNumTypes() - before <= 0)
        {
            ++failures;
            HostDebug::log("  scan failed (0 types): " + p);
        }

        // Remember we attempted this file (even on failure) so a clean second
        // launch does not pay the subprocess cost again until the file changes.
        scannedFiles[p] = juce::File(p).getLastModificationTime().toMilliseconds();
    }

    HostDebug::log(formatLabel + " scan done — "
                   + juce::String(knownPluginList.getNumTypes()) + " total plugin(s), "
                   + juce::String(skipped) + " unchanged (cached), "
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

// ---------------------------------------------------------------------------
// On-disk cache
// ---------------------------------------------------------------------------

juce::File PluginScanner::getCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("AmpForge")
        .getChildFile("pluginCache.xml");
}

bool PluginScanner::isUpToDate(const juce::String& path) const
{
    const auto it = scannedFiles.find(path);
    if (it == scannedFiles.end())
        return false;

    return it->second == juce::File(path).getLastModificationTime().toMilliseconds();
}

void PluginScanner::removeTypesForFile(const juce::String& path)
{
    for (const auto& type : knownPluginList.getTypes())
        if (type.fileOrIdentifier == path)
            knownPluginList.removeType(type);
}

void PluginScanner::pruneMissingFiles()
{
    // Plugins uninstalled since the last scan should disappear from the palette.
    for (const auto& type : knownPluginList.getTypes())
        if (! juce::File(type.fileOrIdentifier).exists())
            knownPluginList.removeType(type);

    for (auto it = scannedFiles.begin(); it != scannedFiles.end();)
        it = juce::File(it->first).exists() ? std::next(it) : scannedFiles.erase(it);
}

void PluginScanner::loadCache()
{
    if (cacheLoaded)
        return;

    cacheLoaded = true;

    const auto file = getCacheFile();
    if (! file.existsAsFile())
        return;

    auto root = juce::XmlDocument::parse(file);
    if (root == nullptr)
        return;

    if (auto* knownPlugins = root->getChildByName("KNOWNPLUGINS"))
        knownPluginList.recreateFromXml(*knownPlugins);

    if (auto* scanned = root->getChildByName("SCANNED"))
        for (auto* f : scanned->getChildWithTagNameIterator("FILE"))
            scannedFiles[f->getStringAttribute("path")]
                = f->getStringAttribute("modTime").getLargeIntValue();
}

void PluginScanner::saveCache() const
{
    juce::XmlElement root("PLUGINCACHE");

    if (auto knownPlugins = knownPluginList.createXml())
        root.addChildElement(knownPlugins.release());   // tagged "KNOWNPLUGINS"

    auto* scanned = root.createNewChildElement("SCANNED");
    for (const auto& [path, modTime] : scannedFiles)
    {
        auto* f = scanned->createNewChildElement("FILE");
        f->setAttribute("path", path);
        f->setAttribute("modTime", juce::String(modTime));
    }

    const auto file = getCacheFile();
    file.getParentDirectory().createDirectory();

    if (! root.writeTo(file))
        HostDebug::log("Plugin cache write FAILED: " + file.getFullPathName());
}
