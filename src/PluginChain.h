#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

/** An ordered chain of plugin instances:
        Input -> slot0 -> slot1 -> ... -> Output

    Thread-safety: structural edits (add/remove/move/bypass/rebuild) take a lock that
    the audio thread also acquires in processAudio. Plugin instances are *created*
    outside the lock (slow, disk I/O) and only swapped in under the lock, to keep the
    critical section short. Phase 3 will replace this with lock-free chain swapping. */
class PluginChain
{
public:
    /** Lightweight view of a slot for the UI. */
    struct SlotInfo
    {
        juce::String name;
        juce::String format;
        bool bypassed = false;
    };

    /** Full description of a slot used to save/rebuild a chain (presets, scenes). */
    struct SlotSpec
    {
        juce::PluginDescription description;
        juce::MemoryBlock state;   // payload from AudioPluginInstance::getStateInformation (may be empty)
        bool bypassed = false;
    };

    explicit PluginChain(juce::AudioPluginFormatManager& formatManager);
    ~PluginChain();

    // ── Structural edits (message thread) ────────────────────────────────────
    /** Creates an instance for the description and appends it to the chain. */
    bool addPlugin(const juce::PluginDescription& description);
    void removePlugin(int index);
    void movePlugin(int fromIndex, int toIndex);
    void setBypass(int index, bool shouldBypass);
    void clear();

    /** Replaces the whole chain from preset/scene specs. Instances are built first,
        then swapped in atomically under the lock. Returns false if any slot failed. */
    bool rebuildFrom(const juce::Array<SlotSpec>& specs);

    // ── Queries (message thread) ─────────────────────────────────────────────
    int getNumSlots() const;
    juce::Array<SlotInfo> getSlotInfos() const;
    bool isBypassed(int index) const;

    /** Snapshot of the chain (captures live plugin state) — for saving presets. */
    juce::Array<SlotSpec> captureSpecs() const;

    /** Instance pointer for opening an editor. nullptr if out of range. */
    juce::AudioPluginInstance* getInstance(int index) const;

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    // ── Audio (audio thread) ─────────────────────────────────────────────────
    void prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels);
    void processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    int getProcessingChannelCount() const { return processingChannelCount.load(); }

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::atomic<bool> bypassed { false };
        juce::PluginDescription description;
    };

    /** Builds a prepared instance for a description (outside the lock). nullptr on failure. */
    std::unique_ptr<Slot> createSlot(const juce::PluginDescription& description, juce::String& error);
    void prepareSlot(Slot& slot);
    void updateProcessingChannelCount();   // call under lock

    juce::AudioPluginFormatManager& formatManager;
    juce::OwnedArray<Slot> slots;
    mutable juce::CriticalSection lock;

    double currentSampleRate    = 44100.0;
    int    currentBlockSize     = 512;
    int    currentInputChannels = 2;
    int    currentOutputChannels = 2;
    std::atomic<int> processingChannelCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChain)
};
