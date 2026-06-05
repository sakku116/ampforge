#pragma once

#include <JuceHeader.h>
#include "PluginChain.h"

/** Preset = ordered snapshot of a pedalboard chain (plugin identity + bypass + internal
    plugin state). Serialised to XML so presets are portable and human-inspectable.
    Each slot stores the full PluginDescription, so loading does not depend on the
    current scan list. */
namespace Preset
{
    /** Builds a ValueTree from a captured chain snapshot. */
    juce::ValueTree toValueTree(const juce::Array<PluginChain::SlotSpec>& specs,
                                const juce::String& name);

    /** Parses a ValueTree back into slot specs. Returns false if the tree is not a preset. */
    bool fromValueTree(const juce::ValueTree& tree,
                       juce::Array<PluginChain::SlotSpec>& outSpecs);

    bool saveToFile(const juce::Array<PluginChain::SlotSpec>& specs,
                    const juce::String& name,
                    const juce::File& file);

    bool loadFromFile(const juce::File& file,
                      juce::Array<PluginChain::SlotSpec>& outSpecs);

    /** %APPDATA%/GtrFxSim/presets (created on demand). */
    juce::File getPresetsDirectory();

    constexpr const char* fileExtension = ".tfpreset";
}
