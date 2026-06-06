#pragma once

#include <JuceHeader.h>

/** Runs each plugin scan in a child process so a crashing plugin cannot kill the host. */
namespace ScanSubprocess
{
constexpr const char* scanVst3Flag = "--scan-vst3=";
constexpr const char* scanVst2Flag = "--scan-vst2=";
constexpr const char* scanFlag     = scanVst3Flag;   // backward-compat alias

bool isWorkerCommandLine(const juce::String& commandLine);

/** Worker entry: scans one path, prints KnownPluginList XML to stdout. Exit 0 on success. */
int runWorker(const juce::String& commandLine);

/** Host-side: scan one plugin file and merge results into list. Returns false if worker crashed.
    Pass scanVst2Flag for .dll VST2 files; default is scanVst3Flag. */
bool scanFileInChildProcess(const juce::String& pluginPath,
                             juce::KnownPluginList& destinationList,
                             const char* scanFlagToUse = scanVst3Flag);
}
