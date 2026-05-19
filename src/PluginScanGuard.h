#pragma once

#include <JuceHeader.h>

/** Catches Windows access violations during VST3 DLL load (scan/load). */
namespace PluginScanGuard
{
enum class ScanOutcome
{
    addedTypes,
    noTypes,
    crashed
};

ScanOutcome scanFile(juce::KnownPluginList& list,
                     const juce::String& path,
                     juce::AudioPluginFormat& format);

struct LoadOutcome
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::String error;
    bool crashed = false;
};

LoadOutcome createPluginInstance(juce::AudioPluginFormatManager& formatManager,
                                 const juce::PluginDescription& description,
                                 double sampleRate,
                                 int blockSize);
}
