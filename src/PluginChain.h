#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

/** An ordered chain of plugin instances:
        Input -> slot0 -> slot1 -> ... -> Output

    Realtime model (Phase 3): the audio thread reads the active slot list through an
    atomic shared_ptr and never takes a long lock. Structural edits build a *new*
    immutable list on the message thread (sharing the existing Slot objects) and publish
    it atomically. Removed slots are reclaimed on the message thread once the audio thread
    has dropped its reference, so plugin instances are never destroyed on the audio thread.
    Bypass is a per-slot atomic flag, so toggling it needs no republish. */
class PluginChain
{
public:
    struct SlotInfo
    {
        juce::String name;
        juce::String format;
        bool bypassed = false;
    };

    struct SlotSpec
    {
        juce::PluginDescription description;
        juce::MemoryBlock state;   // AudioPluginInstance::getStateInformation payload (may be empty)
        bool bypassed = false;
    };

    explicit PluginChain(juce::AudioPluginFormatManager& formatManager);
    ~PluginChain();

    // ── Structural edits (message thread) ────────────────────────────────────
    bool addPlugin(const juce::PluginDescription& description);
    void removePlugin(int index);
    void movePlugin(int fromIndex, int toIndex);
    void setBypass(int index, bool shouldBypass);
    void clear();
    bool rebuildFrom(const juce::Array<SlotSpec>& specs);

    // ── Queries (message thread) ─────────────────────────────────────────────
    int getNumSlots() const;
    juce::Array<SlotInfo> getSlotInfos() const;
    bool isBypassed(int index) const;
    juce::Array<SlotSpec> captureSpecs() const;
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

    using SlotList = std::vector<std::shared_ptr<Slot>>;

    std::shared_ptr<Slot> createSlot(const juce::PluginDescription& description, juce::String& error);
    void prepareSlot(Slot& slot);

    std::shared_ptr<SlotList> currentList() const { return activeList.load(); }
    void publish(std::shared_ptr<SlotList> next);   // call under editLock
    void reclaimRetired();                          // call under editLock
    static int computeChannelCount(const SlotList& list);

    juce::AudioPluginFormatManager& formatManager;

    std::atomic<std::shared_ptr<SlotList>> activeList { std::make_shared<SlotList>() };
    std::vector<std::shared_ptr<SlotList>> retired;   // message-thread graveyard for old lists
    juce::CriticalSection editLock;                   // serialises message-thread edits ONLY

    double currentSampleRate    = 44100.0;
    int    currentBlockSize     = 512;
    int    currentInputChannels = 2;
    int    currentOutputChannels = 2;
    std::atomic<int> processingChannelCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChain)
};
