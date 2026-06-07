#pragma once

#include <JuceHeader.h>
#include "PluginChain.h"

/** Façade over a PluginChain: owns the format manager and the live chain, and manages
    the plugin editor window. The audio engine talks to this class (prepare / processAudio /
    getProcessingChannelCount); the UI uses the chain-editing API. */
class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    PluginChain& getChain() { return chain; }

    // ── Section management (message thread) ─────────────────────────────────
    int  addSection(PluginChain::SectionDef::Type t)          { return chain.addSection(t); }
    void removeSection(int id)                                 { chain.removeSection(id); }
    void renameSection(int id, const juce::String& n)         { chain.renameSection(id, n); }
    void moveSectionUp(int id)                                 { chain.moveSectionUp(id); }
    void moveSectionDown(int id)                               { chain.moveSectionDown(id); }
    juce::Array<PluginChain::SectionDef> getSectionDefs() const { return chain.getSectionDefs(); }
    int  getDefaultSectionId() const                           { return chain.getDefaultSectionId(); }
    juce::Array<PluginChain::SectionDef> captureSectionDefs() const { return chain.captureSectionDefs(); }
    void activatePresetSlot(int i)                             { chain.activatePresetSlot(i); }

    // ── Chain editing (message thread) ───────────────────────────────────────
    bool addPlugin(const juce::PluginDescription& description);
    bool addPlugin(const juce::PluginDescription& description, int sectionId);
    void removePlugin(int index);
    void movePlugin(int fromIndex, int toIndex, int sectionIdOverride = -1);
    void setBypass(int index, bool shouldBypass);
    void setSectionBypassed(int sectionId, bool shouldBypass) { chain.setSectionBypassed(sectionId, shouldBypass); }
    void setSlotGain(int index, float linearGain)             { chain.setSlotGain(index, linearGain); }
    float getSlotGain(int index) const                        { return chain.getSlotGain(index); }
    void setSectionGain(int sectionId, float linearGain)      { chain.setSectionGain(sectionId, linearGain); }
    float getSectionGain(int sectionId) const                 { return chain.getSectionGain(sectionId); }
    float getSectionPeakLevel(int sectionId) const            { return chain.getSectionPeakLevel(sectionId); }
    void renameSlot(int index, const juce::String& newName)   { chain.renameSlot(index, newName); }
    void clearChain();

    int getNumSlots() const { return chain.getNumSlots(); }
    juce::Array<PluginChain::SlotInfo> getSlotInfos() const { return chain.getSlotInfos(); }
    int getChainLatencySamples() const { return chain.getTotalLatencySamples(); }

    /** Sets a normalised (0..1) value on a slot's plugin parameter (expression mapping). */
    void setParameter(int slotIndex, int paramIndex, float value);
    int getParameterCount(int slotIndex) const;

    /** Rebuilds the whole chain from preset/scene specs (closes editors first). */
    bool rebuildChain(const juce::Array<PluginChain::SlotSpec>& specs);
    bool rebuildChain(const juce::Array<PluginChain::SlotSpec>& specs,
                      const juce::Array<PluginChain::SectionDef>& sections);
    /** Crossfade-switches the chain to a new preset/scene (closes editors first). */
    bool switchChainWithCrossfade(const juce::Array<PluginChain::SlotSpec>& specs, int crossfadeMs);
    bool switchChainWithCrossfade(const juce::Array<PluginChain::SlotSpec>& specs,
                                  const juce::Array<PluginChain::SectionDef>& sections,
                                  int crossfadeMs);
    juce::Array<PluginChain::SlotSpec> captureChain() const { return chain.captureSpecs(); }

    // Preloaded switching (build ahead, then switch in <50 ms) — used by scenes.
    int  preloadChain(const juce::Array<PluginChain::SlotSpec>& specs) { return chain.preloadChain(specs); }
    bool activatePreloaded(int handle, int crossfadeMs) { closeAllEditors(); return chain.activateChain(handle, crossfadeMs); }
    void releasePreload(int handle) { chain.releasePreload(handle); }

    // ── Editor ───────────────────────────────────────────────────────────────
    void openEditorWindow(int index);
    void closeAllEditors();

    // ── Backwards-compatible single-plugin API (kept until UI is reworked) ────
    bool loadPlugin(const juce::PluginDescription& description, double sampleRate, int blockSize);
    void unloadPlugin();
    bool hasLoadedPlugin() const { return chain.getNumSlots() > 0; }
    void openEditorWindow() { openEditorWindow(0); }

    // ── Audio (audio thread) ─────────────────────────────────────────────────
    void prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels);
    void processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    int getProcessingChannelCount() const { return chain.getProcessingChannelCount(); }

private:
    class PluginEditorWindow;

    juce::AudioPluginFormatManager formatManager;
    PluginChain chain;
    std::unique_ptr<PluginEditorWindow> editorWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
