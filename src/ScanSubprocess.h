#pragma once

#include <JuceHeader.h>

/** Runs each VST3 scan in a child process so a crashing plugin cannot kill the host. */
namespace ScanSubprocess
{
constexpr const char* scanFlag = "--scan-vst3=";

bool isWorkerCommandLine(const juce::String& commandLine);

/** Worker entry: scans one path, prints KnownPluginList XML to stdout. Exit 0 on success. */
int runWorker(const juce::String& commandLine);

/** Host-side: scan one .vst3 path and merge results into list. Returns false if worker crashed. */
bool scanFileInChildProcess(const juce::String& vst3Path, juce::KnownPluginList& destinationList);
}
