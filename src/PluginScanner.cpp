#include "PluginScanner.h"

PluginScanner::PluginScanner(juce::AudioPluginFormatManager& manager) : formatManager(manager)
{
}

void PluginScanner::scanDefaultWindowsVST3Folders()
{
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
        return;

    juce::StringArray paths;

   #if JUCE_WINDOWS
    paths.add("C:\\Program Files\\Common Files\\VST3");
    paths.add("C:\\Program Files (x86)\\Common Files\\VST3");
   #endif

    for (const auto& path : paths)
    {
        juce::File folder(path);

        if (!folder.isDirectory())
            continue;

        juce::Array<juce::File> vst3Files;
        folder.findChildFiles(vst3Files, juce::File::findDirectories, true, "*.vst3");

        for (const auto& vst3File : vst3Files)
        {
            juce::OwnedArray<juce::PluginDescription> foundTypes;
            knownPluginList.scanAndAddFile(vst3File.getFullPathName(),
                                           true,
                                           foundTypes,
                                           *vst3Format);
        }
    }
}
