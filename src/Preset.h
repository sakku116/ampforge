#pragma once

#include <JuceHeader.h>
#include "PluginChain.h"

/** Preset = ordered snapshot of a pedalboard chain (plugin identity + bypass + internal
    plugin state). Serialised to XML so presets are portable and human-inspectable.
    Each slot stores the full PluginDescription, so loading does not depend on the
    current scan list. */
namespace Preset
{
        /** Builds a ValueTree from a captured chain snapshot (version 1, no sections). */
    juce::ValueTree toValueTree(const juce::Array<PluginChain::SlotSpec>& specs,
                                const juce::String& name);

    /** Builds a ValueTree including section definitions (version 2). */
    juce::ValueTree toValueTree(const juce::Array<PluginChain::SlotSpec>& specs,
                                const juce::Array<PluginChain::SectionDef>& sections,
                                const juce::String& name);

    /** Parses a ValueTree back into slot specs (version 1 compat). Returns false if not a preset. */
    bool fromValueTree(const juce::ValueTree& tree,
                       juce::Array<PluginChain::SlotSpec>& outSpecs);

    /** Parses a ValueTree into slot specs + section defs. Migrates version 1 files automatically. */
    bool fromValueTree(const juce::ValueTree& tree,
                       juce::Array<PluginChain::SlotSpec>& outSpecs,
                       juce::Array<PluginChain::SectionDef>& outSections);

    bool saveToFile(const juce::Array<PluginChain::SlotSpec>& specs,
                    const juce::Array<PluginChain::SectionDef>& sections,
                    const juce::String& name,
                    const juce::File& file);

    bool loadFromFile(const juce::File& file,
                      juce::Array<PluginChain::SlotSpec>& outSpecs,
                      juce::Array<PluginChain::SectionDef>& outSections);

    /** %APPDATA%/AmpForge/presets (created on demand). */
    juce::File getPresetsDirectory();

    constexpr const char* fileExtension = ".tfpreset";
}
