#include "ScanSubprocess.h"
#include "HostDebug.h"

namespace ScanSubprocess
{
namespace
{
juce::AudioPluginFormat* findVst3Format(juce::AudioPluginFormatManager& manager)
{
    for (auto* format : manager.getFormats())
        if (format->getName().containsIgnoreCase("VST3"))
            return format;

    return nullptr;
}

juce::AudioPluginFormat* findVst2Format(juce::AudioPluginFormatManager& manager)
{
    for (auto* format : manager.getFormats())
        if (format->getName() == "VST")
            return format;

    return nullptr;
}

juce::String extractScanPath(const juce::String& commandLine, const char* flag)
{
    const auto flagIndex = commandLine.indexOf(flag);

    if (flagIndex < 0)
        return {};

    auto path = commandLine.substring(flagIndex + juce::String(flag).length());

    if (path.startsWithChar('"'))
        path = path.unquoted();

    return path.trim();
}

juce::File getScanWorkerExecutable()
{
   #if JUCE_WINDOWS
    const auto hostExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto workerExe = hostExe.getSiblingFile("AmpForgeScanWorker.exe");

    if (workerExe.existsAsFile())
        return workerExe;
   #endif

    return juce::File::getSpecialLocation(juce::File::currentExecutableFile);
}

int mergeXmlIntoList(const juce::String& xmlText, juce::KnownPluginList& destinationList)
{
    if (xmlText.trim().isEmpty())
        return 0;

    auto parsed = juce::XmlDocument::parse(xmlText);

    if (parsed == nullptr)
        return 0;

    juce::KnownPluginList scannedList;
    scannedList.recreateFromXml(*parsed);

    const auto typesBefore = destinationList.getNumTypes();

    for (const auto& type : scannedList.getTypes())
        destinationList.addType(type);

    return destinationList.getNumTypes() - typesBefore;
}
}

bool isWorkerCommandLine(const juce::String& commandLine)
{
    return commandLine.contains(scanVst3Flag) || commandLine.contains(scanVst2Flag);
}

int runWorker(const juce::String& commandLine)
{
    const bool isVst2 = commandLine.contains(scanVst2Flag);
    const char* flag  = isVst2 ? scanVst2Flag : scanVst3Flag;

    const auto path = extractScanPath(commandLine, flag);

    if (path.isEmpty())
        return 2;

    juce::AudioPluginFormatManager formatManager;
    formatManager.addDefaultFormats();

    auto* pluginFormat = isVst2 ? findVst2Format(formatManager)
                                : findVst3Format(formatManager);

    if (pluginFormat == nullptr)
        return 3;

    juce::KnownPluginList list;
    juce::OwnedArray<juce::PluginDescription> foundTypes;
    list.scanAndAddFile(path, true, foundTypes, *pluginFormat);

    if (auto xml = list.createXml())
        std::cout << xml->toString() << std::endl;

    return 0;
}

bool scanFileInChildProcess(const juce::String& pluginPath,
                             juce::KnownPluginList& destinationList,
                             const char* scanFlagToUse)
{
    const auto exe = getScanWorkerExecutable();

    if (! exe.existsAsFile())
    {
        HostDebug::log("Scan worker missing: " + exe.getFullPathName());
        return false;
    }

    const auto command = "\"" + exe.getFullPathName() + "\" " + juce::String(scanFlagToUse) + "\"" + pluginPath + "\"";
    HostDebug::log("Scan worker spawn: " + command);

    juce::ChildProcess child;

    if (! child.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        HostDebug::log("Scan worker failed to start");
        return false;
    }

    juce::String output;
    constexpr int chunkSize = 4096;

    for (;;)
    {
        char buffer[chunkSize];
        const auto bytesRead = child.readProcessOutput(buffer, (int) sizeof(buffer));

        if (bytesRead <= 0)
            break;

        output += juce::String::fromUTF8(buffer, (size_t) bytesRead);
    }

    child.waitForProcessToFinish(120000);
    const auto exitCode = child.getExitCode();
    HostDebug::log("Scan worker exit " + juce::String(exitCode) + ", output " + juce::String(output.length()) + " chars");

    if (exitCode != 0)
        return false;

    const auto added = mergeXmlIntoList(output, destinationList);

    if (added > 0)
    {
        auto parsed = juce::XmlDocument::parse(output);

        if (parsed != nullptr)
        {
            juce::KnownPluginList scannedList;
            scannedList.recreateFromXml(*parsed);

            for (const auto& type : scannedList.getTypes())
                HostDebug::log("  registered: " + type.name + " <- " + pluginPath);
        }
    }
    else
    {
        HostDebug::log("Scan worker merged 0 types from " + pluginPath);
    }

    return added >= 0;
}

}
