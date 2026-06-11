#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <map>
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
    /** A named grouping of slots. Stomp = independent bypass; Preset = exclusive/radio. */
    struct SectionDef
    {
        enum class Type { stomp, preset };
        int          id       = 0;
        juce::String name;
        Type         type     = Type::stomp;
        bool         bypassed = false;
        float        gain     = 1.0f;   // section output gain (linear, applied after last slot)
    };

    struct SlotInfo
    {
        juce::String name;        // display name (customName if set, otherwise plugin name)
        juce::String originalName; // always the plugin's own name
        juce::String format;
        bool  bypassed    = false;
        bool  hasCustomName = false;
        int   sectionId   = 1;
        bool  isPreset    = false;
        int   slotId      = 0;      // stable identity — does not change on reorder/move
        float postGain    = 1.0f;   // slot output gain (linear)
    };

    struct SlotSpec
    {
        juce::PluginDescription description;
        juce::MemoryBlock state;   // AudioPluginInstance::getStateInformation payload (may be empty)
        bool         bypassed  = false;
        juce::String customName;   // empty = use plugin's own name
        int          sectionId = 1;
        int          slotId    = 0;      // 0 = unset (assign new ID on load)
        float        postGain  = 1.0f;  // slot output gain (linear)
    };

    explicit PluginChain(juce::AudioPluginFormatManager& formatManager);
    ~PluginChain();

    // ── Section management (message thread) ─────────────────────────────────
    int  addSection(SectionDef::Type type);
    void removeSection(int sectionId);
    void renameSection(int sectionId, const juce::String& name);
    void moveSectionUp(int sectionId);
    void moveSectionDown(int sectionId);
    void setSectionBypassed(int sectionId, bool shouldBypass);
    bool isSectionBypassed(int sectionId) const;
    juce::Array<SectionDef> getSectionDefs() const;
    int  getDefaultSectionId() const;
    juce::Array<SectionDef> captureSectionDefs() const;

    /** Exclusive-activate one slot inside a preset section (atomic, no republish). */
    void activatePresetSlot(int slotIndex);

    // ── Structural edits (message thread) ────────────────────────────────────
    /** Add plugin into a specific section; inserts after last slot of that section. */
    bool addPlugin(const juce::PluginDescription& description, int sectionId);
    /** Compat overload: adds to the last section. */
    bool addPlugin(const juce::PluginDescription& description);
    /** Duplicate the slot at index, inserting the copy immediately after it. */
    bool duplicatePlugin(int index);
    void removePlugin(int index);
    /** Moves a slot. If sectionIdOverride >= 0, assigns that section instead of inheriting
        from the displaced slot. Handles from == toIndex as a section-only change. */
    void movePlugin(int fromIndex, int toIndex, int sectionIdOverride = -1);
    void setBypass(int index, bool shouldBypass);
    void setSlotGain(int index, float linearGain);
    float getSlotGain(int index) const;
    void setSectionGain(int sectionId, float linearGain);
    float getSectionGain(int sectionId) const;
    /** Returns the peak magnitude (0..1+) of the last audio block at the output of this section. */
    float getSectionPeakLevel(int sectionId) const;
    /** Sets a user-visible display name on a slot. Empty string resets to plugin's own name. */
    void renameSlot(int index, const juce::String& newName);
    void clear();
    bool rebuildFrom(const juce::Array<SlotSpec>& specs);
    bool rebuildFrom(const juce::Array<SlotSpec>& specs, const juce::Array<SectionDef>& sections);

    // ── Live switching (Phase 3) ─────────────────────────────────────────────
    /** Builds a new chain and crossfades into it over crossfadeMs (0 = instant). */
    bool switchWithCrossfade(const juce::Array<SlotSpec>& specs, int crossfadeMs);
    bool switchWithCrossfade(const juce::Array<SlotSpec>& specs,
                             const juce::Array<SectionDef>& sections,
                             int crossfadeMs);
    bool isTransitioning() const { return fadeInList.load() != nullptr; }

    /** Builds a chain ahead of the switch moment; returns a handle (>0). */
    int  preloadChain(const juce::Array<SlotSpec>& specs);
    /** Crossfade-switches to a previously preloaded chain (instant if crossfadeMs<=0).
        The instances are already built, so this is the <50 ms switch path. */
    bool activateChain(int handle, int crossfadeMs);
    void releasePreload(int handle);

    /** Async build on a background thread: loads all plugins without blocking the message
        thread. onProgress(loaded, total) is posted to the message thread after each slot.
        onComplete(handle, allOk) is posted to the message thread when done; the caller should
        pass handle to activateChain() to make the chain live. Cancels any in-progress
        async build first. */
    void buildChainAsync(const juce::Array<SlotSpec>& specs,
                         const juce::Array<SectionDef>& sections,
                         std::function<void(int /*loaded*/, int /*total*/)> onProgress,
                         std::function<void(int /*handle*/, bool /*allOk*/)> onComplete);
    /** Cancels any in-progress async build. The partially-built chain is discarded. */
    void cancelAsyncBuild();

    // ── Queries (message thread) ─────────────────────────────────────────────
    int getNumSlots() const;
    juce::Array<SlotInfo> getSlotInfos() const;
    bool isBypassed(int index) const;
    /** Sum of reported latency (samples) of active, non-bypassed slots. */
    int getTotalLatencySamples() const;
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
        std::atomic<bool>  bypassed           { false };
        std::atomic<bool>  sectionBypassed    { false };
        std::atomic<float> postGain           { 1.0f };   // slot output gain (audio thread)
        std::atomic<float> sectionOutputGain  { 1.0f };   // propagated from SectionDef::gain (audio thread)
        std::atomic<bool>  isLastInSection    { false };   // true for the last slot of its section (audio thread)
        std::atomic<float> peakLevel          { 0.0f };   // peak magnitude of last block, written on audio thread
        juce::PluginDescription description;
        juce::String customName;   // empty = use instance->getName(). Message-thread only.
        int sectionId = 1;         // message-thread only
        int slotId    = 0;         // stable identity assigned at creation, never changes
    };

    using SlotList = std::vector<std::shared_ptr<Slot>>;

    std::shared_ptr<Slot> createSlot(const juce::PluginDescription& description, juce::String& error);
    void prepareSlot(Slot& slot);
    std::shared_ptr<SlotList> buildList(const juce::Array<SlotSpec>& specs, bool& allOk);

    std::shared_ptr<SlotList> currentList() const { return activeList.load(); }
    // During a crossfade, returns the incoming (target) list — used by UI queries so the
    // display reflects the intended new chain immediately rather than waiting ~25 ms for the
    // audio thread to promote fadeInList to activeList.
    std::shared_ptr<SlotList> displayList() const;
    void publish(std::shared_ptr<SlotList> next);                          // call under editLock
    void publishWithCrossfade(std::shared_ptr<SlotList> next, int ms);     // call under editLock
    void reclaimRetired();                                                 // call under editLock
    static int computeChannelCount(const SlotList& list);
    static void runList(const SlotList& list, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    juce::AudioPluginFormatManager& formatManager;

    std::vector<SectionDef> sectionDefs;
    int nextSectionId   = 2;   // 1 is reserved for the default "Stomp 1"
    int nextStompCount  = 1;   // "Stomp 1" already exists
    int nextPresetCount = 0;
    int nextSlotId      = 1;   // incremented on every new slot; never reset

    std::atomic<std::shared_ptr<SlotList>> activeList { std::make_shared<SlotList>() };
    std::atomic<std::shared_ptr<SlotList>> fadeInList { nullptr };   // incoming chain during a crossfade
    std::vector<std::shared_ptr<SlotList>> retired;   // message-thread graveyard for old lists
    juce::CriticalSection editLock;                   // serialises message-thread edits ONLY

    std::atomic<int> requestedFadeSamples { 0 };
    std::atomic<juce::uint32> transitionEpoch { 0 };

    std::map<int, std::shared_ptr<SlotList>> preloaded;   // message-thread: chains built ahead
    int nextPreloadHandle = 1;

    class PluginLoaderThread;
    friend class PluginLoaderThread;

    std::atomic<int> asyncEpoch { 0 };
    std::shared_ptr<std::atomic<bool>> loaderAliveFlag { std::make_shared<std::atomic<bool>>(true) };
    std::unique_ptr<PluginLoaderThread> loaderThread;
    std::vector<std::unique_ptr<PluginLoaderThread>> loaderGraveyard;

    void commitAsyncBuild(const juce::Array<SectionDef>& sections,
                          std::shared_ptr<SlotList> partial,
                          int epoch, int newNextSlotId,
                          std::function<void(int, bool)> onComplete,
                          bool allOk);

    // audio-thread only:
    juce::AudioBuffer<float> fadeScratch;
    int crossfadePos = 0;
    int crossfadeTotal = 0;
    juce::uint32 lastTransitionEpoch = 0;

    double currentSampleRate    = 44100.0;
    int    currentBlockSize     = 512;
    int    currentInputChannels = 2;
    int    currentOutputChannels = 2;
    std::atomic<int> processingChannelCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChain)
};
