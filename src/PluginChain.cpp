#include "PluginChain.h"
#include "HostDebug.h"
#include "PluginScanGuard.h"

#include <algorithm>
#include <cmath>

// ── Background plugin loader thread ─────────────────────────────────────────
// Defined here (before PluginChain methods) so the destructor can call stopThread().

class PluginChain::PluginLoaderThread : public juce::Thread
{
public:
    PluginLoaderThread(PluginChain& owner,
                       std::shared_ptr<std::atomic<bool>> aliveFlag,
                       juce::Array<SlotSpec> specs,
                       juce::Array<SectionDef> sections,
                       int capturedEpoch,
                       int startSlotId,
                       std::function<void(int, int)> onProgress,
                       std::function<void(int, bool)> onComplete)
        : juce::Thread("AmpForge:PluginLoader"),
          owner(owner),
          aliveFlag(std::move(aliveFlag)),
          specs(std::move(specs)),
          sections(std::move(sections)),
          capturedEpoch(capturedEpoch),
          localNextSlotId(startSlotId),
          onProgress(std::move(onProgress)),
          onComplete(std::move(onComplete))
    {}

    void run() override
    {
        auto partial = std::make_shared<PluginChain::SlotList>();
        bool allOk = true;
        const int total = specs.size();

        for (int i = 0; i < total; ++i)
        {
            if (threadShouldExit() || owner.asyncEpoch.load() != capturedEpoch)
                return;

            const auto& spec = specs[i];
            juce::String error;
            auto slot = owner.createSlot(spec.description, error);

            if (slot != nullptr)
            {
                if (spec.state.getSize() > 0)
                    slot->instance->setStateInformation(spec.state.getData(),
                                                         (int) spec.state.getSize());

                slot->bypassed .store(spec.bypassed);
                slot->postGain .store(spec.postGain > 0.0f ? spec.postGain : 1.0f);
                slot->customName = spec.customName;

                if (spec.slotId > 0)
                {
                    slot->slotId = spec.slotId;
                    localNextSlotId = std::max(localNextSlotId, spec.slotId + 1);
                }
                else
                {
                    slot->slotId = localNextSlotId++;
                }

                // Validate sectionId against the target sections.
                bool validSec = false;
                for (const auto& def : sections)
                {
                    if (def.id == spec.sectionId)
                    {
                        validSec = true;
                        slot->sectionBypassed.store(def.bypassed);
                        break;
                    }
                }
                slot->sectionId = validSec ? spec.sectionId
                                           : (sections.isEmpty() ? 1 : sections[0].id);

                partial->push_back(std::move(slot));
            }
            else
            {
                HostDebug::log("Loader thread: slot FAILED " + spec.description.name + " — " + error);
                allOk = false;
            }

            // Post progress to the message thread between each slot.
            const int loaded = i + 1;
            if (onProgress)
            {
                auto cb = onProgress;
                juce::MessageManager::callAsync([cb, loaded, total]() mutable {
                    cb(loaded, total);
                });
            }
        }

        if (threadShouldExit() || owner.asyncEpoch.load() != capturedEpoch)
            return;

        // Post completion to the message thread.
        auto& ownerRef      = owner;
        auto  flag          = aliveFlag;
        auto  capturedSects = sections;
        auto  capturedPart  = partial;
        auto  epoch         = capturedEpoch;
        auto  slotId        = localNextSlotId;
        auto  completeCb    = onComplete;
        const bool ok       = allOk;

        juce::MessageManager::callAsync(
            [flag, &ownerRef, capturedSects, capturedPart, epoch, slotId, completeCb, ok]() mutable
            {
                if (! flag->load())
                    return;   // PluginChain is being destroyed
                ownerRef.commitAsyncBuild(capturedSects, capturedPart, epoch,
                                          slotId, std::move(completeCb), ok);
            });
    }

private:
    PluginChain& owner;
    std::shared_ptr<std::atomic<bool>> aliveFlag;
    juce::Array<SlotSpec>   specs;
    juce::Array<SectionDef> sections;
    int capturedEpoch;
    int localNextSlotId;
    std::function<void(int, int)> onProgress;
    std::function<void(int, bool)> onComplete;
};

// ─────────────────────────────────────────────────────────────────────────────

PluginChain::PluginChain(juce::AudioPluginFormatManager& manager) : formatManager(manager)
{
    sectionDefs.push_back({ 1, "Stomp 1", SectionDef::Type::stomp });
}

PluginChain::~PluginChain()
{
    // Signal all loader threads to stop and wait for them before destroying state they reference.
    loaderAliveFlag->store(false);
    asyncEpoch.fetch_add(1);

    if (loaderThread != nullptr)
        loaderThread->stopThread(2000);

    for (auto& t : loaderGraveyard)
        t->stopThread(2000);
    loaderGraveyard.clear();

    const juce::ScopedLock sl(editLock);
    activeList.store(std::make_shared<SlotList>());
    retired.clear();   // destroys remaining instances on the message thread
}

int PluginChain::computeChannelCount(const SlotList& list)
{
    int maxChannels = 0;

    for (const auto& slot : list)
        if (slot->instance != nullptr)
            maxChannels = juce::jmax(maxChannels,
                                     slot->instance->getTotalNumInputChannels(),
                                     slot->instance->getTotalNumOutputChannels());

    return maxChannels;
}

std::shared_ptr<PluginChain::Slot> PluginChain::createSlot(const juce::PluginDescription& description,
                                                          juce::String& error)
{
    // Built outside the lock — instance creation hits disk and can be slow.
    auto outcome = PluginScanGuard::createPluginInstance(formatManager,
                                                         description,
                                                         currentSampleRate,
                                                         currentBlockSize);

    if (outcome.crashed)
    {
        error = "crashed: " + outcome.error;
        return nullptr;
    }

    if (outcome.instance == nullptr)
    {
        error = outcome.error;
        return nullptr;
    }

    auto slot = std::make_shared<Slot>();
    slot->instance = std::move(outcome.instance);
    slot->description = description;
    prepareSlot(*slot);
    return slot;
}

void PluginChain::prepareSlot(Slot& slot)
{
    if (slot.instance == nullptr)
        return;

    slot.instance->releaseResources();
    slot.instance->setPlayConfigDetails(currentInputChannels, currentOutputChannels,
                                        currentSampleRate, currentBlockSize);
    slot.instance->prepareToPlay(currentSampleRate, currentBlockSize);
}

void PluginChain::publish(std::shared_ptr<SlotList> next)
{
    // Recompute per-slot derived state that the audio thread reads atomically.
    // Must be done before making the list live so audio thread never sees stale data.
    {
        // Build section gain map from current sectionDefs.
        std::map<int, float> secGains;
        for (const auto& def : sectionDefs)
            secGains[def.id] = def.gain;

        const int n = (int) next->size();
        for (int i = 0; i < n; ++i)
        {
            const auto& slot = (*next)[(size_t) i];
            const bool isLast = (i + 1 >= n) || ((*next)[(size_t)(i + 1)]->sectionId != slot->sectionId);
            slot->isLastInSection.store(isLast);

            auto it = secGains.find(slot->sectionId);
            slot->sectionOutputGain.store(it != secGains.end() ? it->second : 1.0f);
        }
    }

    processingChannelCount.store(computeChannelCount(*next));

    fadeInList.store(nullptr);   // cancel any in-progress crossfade

    auto old = activeList.exchange(next);

    if (old != nullptr)
        retired.push_back(std::move(old));

    reclaimRetired();
}

void PluginChain::publishWithCrossfade(std::shared_ptr<SlotList> next, int crossfadeMs)
{
    // Apply the same per-slot derived state as publish() so the incoming list
    // has correct gains when the audio thread runs it during crossfade.
    {
        std::map<int, float> secGains;
        for (const auto& def : sectionDefs)
            secGains[def.id] = def.gain;

        const int n = (int) next->size();
        for (int i = 0; i < n; ++i)
        {
            const auto& slot = (*next)[(size_t) i];
            const bool isLast = (i + 1 >= n) || ((*next)[(size_t)(i + 1)]->sectionId != slot->sectionId);
            slot->isLastInSection.store(isLast);
            auto it = secGains.find(slot->sectionId);
            slot->sectionOutputGain.store(it != secGains.end() ? it->second : 1.0f);
        }
    }

    processingChannelCount.store(juce::jmax(computeChannelCount(*activeList.load()),
                                           computeChannelCount(*next)));

    const int fadeSamples = juce::jmax(1, (int) (crossfadeMs * 0.001 * currentSampleRate));

    // Keep the outgoing list referenced so it is reclaimed on the message thread, not on audio.
    retired.push_back(activeList.load());

    requestedFadeSamples.store(fadeSamples);
    transitionEpoch.fetch_add(1);
    fadeInList.store(next);       // audio thread promotes this to active when the fade completes

    reclaimRetired();
}

void PluginChain::runList(const SlotList& list, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (const auto& slot : list)
    {
        const bool active = !slot->bypassed.load() && !slot->sectionBypassed.load();

        if (active)
        {
            slot->instance->processBlock(buffer, midi);

            const float pg = slot->postGain.load();
            if (pg != 1.0f)
                buffer.applyGain(pg);
        }

        if (slot->isLastInSection.load())
        {
            const float sg = slot->sectionOutputGain.load();
            if (sg != 1.0f)
                buffer.applyGain(sg);

            // Compute and store peak for the level meter (audio thread write).
            slot->peakLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()));
        }
    }
}

std::shared_ptr<PluginChain::SlotList> PluginChain::displayList() const
{
    auto pending = fadeInList.load();
    return pending != nullptr ? pending : activeList.load();
}

void PluginChain::reclaimRetired()
{
    // Drop old lists the audio thread no longer references (use_count() <= 1 means only
    // 'retired' holds it), destroying their removed instances here on the message thread.
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [](const std::shared_ptr<SlotList>& l) { return l.use_count() <= 1; }),
                  retired.end());
}

bool PluginChain::addPlugin(const juce::PluginDescription& description, int targetSectionId)
{
    juce::String error;
    auto slot = createSlot(description, error);

    if (slot == nullptr)
    {
        HostDebug::log("Chain add FAILED: " + description.name + " — " + error);
        return false;
    }

    slot->sectionId = targetSectionId;
    slot->slotId    = nextSlotId++;

    {
        const juce::ScopedLock sl(editLock);
        auto next = std::make_shared<SlotList>(*currentList());

        // Insert after the last slot of targetSectionId, or before the first slot
        // of any later section, so the per-section grouping invariant is maintained.
        int insertPos = (int) next->size();

        int secIdx = -1;
        for (int i = 0; i < (int) sectionDefs.size(); ++i)
            if (sectionDefs[(size_t) i].id == targetSectionId) { secIdx = i; break; }

        if (secIdx >= 0)
        {
            bool foundInSection = false;
            for (int i = (int) next->size() - 1; i >= 0; --i)
            {
                if ((*next)[(size_t) i]->sectionId == targetSectionId)
                {
                    insertPos = i + 1;
                    foundInSection = true;
                    break;
                }
            }

            if (! foundInSection)
            {
                // Section is empty: insert before first slot of any later section.
                for (int li = secIdx + 1; li < (int) sectionDefs.size() && insertPos == (int) next->size(); ++li)
                {
                    const int laterId = sectionDefs[(size_t) li].id;
                    for (int i = 0; i < (int) next->size(); ++i)
                    {
                        if ((*next)[(size_t) i]->sectionId == laterId)
                        {
                            insertPos = i;
                            break;
                        }
                    }
                }
            }
        }

        if (secIdx >= 0)
        {
            // Propagate section-level bypass to the new slot.
            slot->sectionBypassed.store(sectionDefs[(size_t) secIdx].bypassed);

            // All new plugins start bypassed/off regardless of section type.
            slot->bypassed.store(true);
        }

        next->insert(next->begin() + insertPos, std::move(slot));
        publish(next);
        HostDebug::log("Chain add OK: " + description.name + " section=" + juce::String(targetSectionId)
                       + " (slots=" + juce::String((int) next->size()) + ")");
    }

    return true;
}

bool PluginChain::addPlugin(const juce::PluginDescription& description)
{
    const int sid = sectionDefs.empty() ? 1 : sectionDefs.back().id;
    return addPlugin(description, sid);
}

bool PluginChain::duplicatePlugin(int index)
{
    // Capture source slot's spec (message thread, under lock).
    SlotSpec spec;
    {
        const juce::ScopedLock sl(editLock);
        auto cur = currentList();
        if (! juce::isPositiveAndBelow(index, (int) cur->size()))
            return false;

        const auto& src = (*cur)[(size_t) index];
        spec.description = src->description;
        spec.sectionId   = src->sectionId;
        spec.bypassed    = src->bypassed.load();
        spec.postGain    = src->postGain.load();
        spec.customName  = src->customName;
        if (src->instance != nullptr)
            src->instance->getStateInformation(spec.state);
    }

    // Build new instance outside the lock (can be slow).
    juce::String error;
    auto newSlot = createSlot(spec.description, error);
    if (newSlot == nullptr)
    {
        HostDebug::log("Duplicate FAILED: " + spec.description.name + " — " + error);
        return false;
    }

    if (spec.state.getSize() > 0)
        newSlot->instance->setStateInformation(spec.state.getData(), (int) spec.state.getSize());

    newSlot->postGain.store(spec.postGain);
    newSlot->customName = spec.customName;
    newSlot->sectionId  = spec.sectionId;
    newSlot->slotId     = nextSlotId++;

    {
        const juce::ScopedLock sl(editLock);
        auto cur = currentList();

        if (! juce::isPositiveAndBelow(index, (int) cur->size()))
            return false;

        for (const auto& def : sectionDefs)
        {
            if (def.id == newSlot->sectionId)
            {
                newSlot->sectionBypassed.store(def.bypassed);
                // Preset section: new slot must start bypassed to preserve exclusive-active invariant.
                if (def.type == SectionDef::Type::preset)
                    newSlot->bypassed.store(true);
                else
                    newSlot->bypassed.store(spec.bypassed);
                break;
            }
        }

        auto next = std::make_shared<SlotList>(*cur);
        next->insert(next->begin() + index + 1, std::move(newSlot));
        publish(next);
        HostDebug::log("Chain duplicate slot " + juce::String(index)
                       + " -> " + juce::String(index + 1)
                       + " section=" + juce::String(spec.sectionId)
                       + " (slots=" + juce::String((int) next->size()) + ")");
    }

    return true;
}

void PluginChain::removePlugin(int index)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();

    if (! juce::isPositiveAndBelow(index, (int) cur->size()))
        return;

    auto next = std::make_shared<SlotList>(*cur);
    next->erase(next->begin() + index);
    publish(next);
    HostDebug::log("Chain remove slot " + juce::String(index) + " (slots=" + juce::String((int) next->size()) + ")");
}

void PluginChain::movePlugin(int fromIndex, int toIndex, int sectionIdOverride)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();
    const int n = (int) cur->size();

    if (! juce::isPositiveAndBelow(fromIndex, n))
        return;

    toIndex = juce::jlimit(0, n - 1, toIndex);

    const int newSectionId = (sectionIdOverride >= 0)
                              ? sectionIdOverride
                              : (*cur)[(size_t) toIndex]->sectionId;

    if (toIndex == fromIndex)
    {
        // No position change — just update sectionId if overridden.
        if (sectionIdOverride >= 0 && (*cur)[(size_t) fromIndex]->sectionId != sectionIdOverride)
        {
            auto next = std::make_shared<SlotList>(*cur);
            (*next)[(size_t) fromIndex]->sectionId = sectionIdOverride;
            publish(next);
            HostDebug::log("Chain section-reassign " + juce::String(fromIndex)
                           + " sectionId=" + juce::String(sectionIdOverride));
        }
        return;
    }

    auto next = std::make_shared<SlotList>(*cur);
    auto slot = (*next)[(size_t) fromIndex];
    next->erase(next->begin() + fromIndex);
    next->insert(next->begin() + toIndex, slot);
    (*next)[(size_t) toIndex]->sectionId = newSectionId;
    publish(next);
    HostDebug::log("Chain move " + juce::String(fromIndex) + " -> " + juce::String(toIndex)
                   + " sectionId=" + juce::String(newSectionId));
}

void PluginChain::setBypass(int index, bool shouldBypass)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();

    if (juce::isPositiveAndBelow(index, (int) cur->size()))
        (*cur)[(size_t) index]->bypassed.store(shouldBypass);   // atomic flag; no republish needed
}

void PluginChain::renameSlot(int index, const juce::String& newName)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();

    if (juce::isPositiveAndBelow(index, (int) cur->size()))
        (*cur)[(size_t) index]->customName = newName;   // message-thread only; no republish needed
}

void PluginChain::setSlotGain(int index, float linearGain)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();
    if (juce::isPositiveAndBelow(index, (int) cur->size()))
        (*cur)[(size_t) index]->postGain.store(linearGain);   // atomic; no republish needed
}

float PluginChain::getSlotGain(int index) const
{
    auto cur = currentList();
    if (juce::isPositiveAndBelow(index, (int) cur->size()))
        return (*cur)[(size_t) index]->postGain.load();
    return 1.0f;
}

void PluginChain::setSectionGain(int sectionId, float linearGain)
{
    for (auto& def : sectionDefs)
        if (def.id == sectionId) { def.gain = linearGain; break; }

    // Propagate to all slots in this section atomically — no republish needed.
    auto cur = currentList();
    for (const auto& slot : *cur)
        if (slot->sectionId == sectionId)
            slot->sectionOutputGain.store(linearGain);
}

float PluginChain::getSectionGain(int sectionId) const
{
    for (const auto& def : sectionDefs)
        if (def.id == sectionId) return def.gain;
    return 1.0f;
}

float PluginChain::getSectionPeakLevel(int sectionId) const
{
    auto cur = displayList();
    for (const auto& slot : *cur)
        if (slot->sectionId == sectionId && slot->isLastInSection.load())
            return slot->peakLevel.load();
    return 0.0f;
}

void PluginChain::clear()
{
    cancelAsyncBuild();   // discard any in-progress async load
    const juce::ScopedLock sl(editLock);
    publish(std::make_shared<SlotList>());
}

// ── Section management ────────────────────────────────────────────────────────

int PluginChain::addSection(SectionDef::Type type)
{
    SectionDef def;
    def.id   = nextSectionId++;
    def.type = type;
    def.name = (type == SectionDef::Type::stomp)
               ? "Stomp " + juce::String(++nextStompCount)
               : "Preset " + juce::String(++nextPresetCount);
    sectionDefs.push_back(def);
    HostDebug::log("Section added: \"" + def.name + "\" id=" + juce::String(def.id));
    return def.id;
}

void PluginChain::removeSection(int sectionId)
{
    const juce::ScopedLock sl(editLock);

    auto next = std::make_shared<SlotList>();
    for (const auto& slot : *currentList())
        if (slot->sectionId != sectionId)
            next->push_back(slot);

    sectionDefs.erase(std::remove_if(sectionDefs.begin(), sectionDefs.end(),
                                     [sectionId](const SectionDef& d) { return d.id == sectionId; }),
                      sectionDefs.end());

    // Ensure at least one section always exists.
    if (sectionDefs.empty())
        sectionDefs.push_back({ 1, "Stomp 1", SectionDef::Type::stomp });

    publish(next);
    HostDebug::log("Section removed: id=" + juce::String(sectionId));
}

void PluginChain::renameSection(int sectionId, const juce::String& name)
{
    for (auto& def : sectionDefs)
        if (def.id == sectionId) { def.name = name; return; }
}

void PluginChain::moveSectionUp(int sectionId)
{
    auto it = std::find_if(sectionDefs.begin(), sectionDefs.end(),
                           [sectionId](const SectionDef& d) { return d.id == sectionId; });

    if (it == sectionDefs.end() || it == sectionDefs.begin())
        return;

    std::iter_swap(it, std::prev(it));

    const juce::ScopedLock sl(editLock);
    auto old  = currentList();
    auto next = std::make_shared<SlotList>();
    for (const auto& sec : sectionDefs)
        for (const auto& slot : *old)
            if (slot->sectionId == sec.id)
                next->push_back(slot);

    publish(next);
    HostDebug::log("Section move up: id=" + juce::String(sectionId));
}

void PluginChain::moveSectionDown(int sectionId)
{
    auto it = std::find_if(sectionDefs.begin(), sectionDefs.end(),
                           [sectionId](const SectionDef& d) { return d.id == sectionId; });

    if (it == sectionDefs.end())
        return;

    auto nit = std::next(it);
    if (nit == sectionDefs.end())
        return;

    std::iter_swap(it, nit);

    const juce::ScopedLock sl(editLock);
    auto old  = currentList();
    auto next = std::make_shared<SlotList>();
    for (const auto& sec : sectionDefs)
        for (const auto& slot : *old)
            if (slot->sectionId == sec.id)
                next->push_back(slot);

    publish(next);
    HostDebug::log("Section move down: id=" + juce::String(sectionId));
}

void PluginChain::setSectionBypassed(int sectionId, bool shouldBypass)
{
    for (auto& def : sectionDefs)
        if (def.id == sectionId) { def.bypassed = shouldBypass; break; }

    // Atomically update every slot in this section — no republish needed.
    auto cur = currentList();
    for (const auto& slot : *cur)
        if (slot->sectionId == sectionId)
            slot->sectionBypassed.store(shouldBypass);

    HostDebug::log("Section " + juce::String(sectionId) + " bypassed=" + juce::String((int) shouldBypass));
}

bool PluginChain::isSectionBypassed(int sectionId) const
{
    for (const auto& def : sectionDefs)
        if (def.id == sectionId) return def.bypassed;
    return false;
}

juce::Array<PluginChain::SectionDef> PluginChain::getSectionDefs() const
{
    juce::Array<SectionDef> result;
    for (const auto& s : sectionDefs)
        result.add(s);
    return result;
}

int PluginChain::getDefaultSectionId() const
{
    return sectionDefs.empty() ? 1 : sectionDefs.front().id;
}

juce::Array<PluginChain::SectionDef> PluginChain::captureSectionDefs() const
{
    return getSectionDefs();
}

void PluginChain::activatePresetSlot(int slotIndex)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();

    if (! juce::isPositiveAndBelow(slotIndex, (int) cur->size()))
        return;

    const int targetSectionId = (*cur)[(size_t) slotIndex]->sectionId;

    auto sit = std::find_if(sectionDefs.begin(), sectionDefs.end(),
                            [targetSectionId](const SectionDef& d) { return d.id == targetSectionId; });

    if (sit == sectionDefs.end() || sit->type != SectionDef::Type::preset)
        return;

    for (int i = 0; i < (int) cur->size(); ++i)
        if ((*cur)[(size_t) i]->sectionId == targetSectionId)
            (*cur)[(size_t) i]->bypassed.store(i != slotIndex);

    HostDebug::log("Preset slot activated: slot=" + juce::String(slotIndex)
                   + " section=" + juce::String(targetSectionId));
}

std::shared_ptr<PluginChain::SlotList> PluginChain::buildList(const juce::Array<SlotSpec>& specs, bool& allOk)
{
    auto list = std::make_shared<SlotList>();
    allOk = true;

    for (const auto& spec : specs)
    {
        juce::String error;
        auto slot = createSlot(spec.description, error);

        if (slot == nullptr)
        {
            HostDebug::log("Chain build: slot FAILED " + spec.description.name + " — " + error);
            allOk = false;
            continue;
        }

        if (spec.state.getSize() > 0)
            slot->instance->setStateInformation(spec.state.getData(), (int) spec.state.getSize());

        slot->bypassed.store(spec.bypassed);
        slot->postGain.store(spec.postGain > 0.0f ? spec.postGain : 1.0f);
        slot->customName = spec.customName;

        if (spec.slotId > 0)
        {
            slot->slotId = spec.slotId;
            nextSlotId = std::max(nextSlotId, spec.slotId + 1);
        }
        else
        {
            slot->slotId = nextSlotId++;
        }

        // Validate sectionId; fall back to first section if unknown.
        bool validSec = false;
        for (const auto& def : sectionDefs)
        {
            if (def.id == spec.sectionId)
            {
                validSec = true;
                slot->sectionBypassed.store(def.bypassed);
                break;
            }
        }
        slot->sectionId = validSec ? spec.sectionId : (sectionDefs.empty() ? 1 : sectionDefs[0].id);

        list->push_back(std::move(slot));
    }

    return list;
}

bool PluginChain::rebuildFrom(const juce::Array<SlotSpec>& specs)
{
    cancelAsyncBuild();   // discard any in-progress async load
    bool allOk = false;
    auto next = buildList(specs, allOk);

    {
        const juce::ScopedLock sl(editLock);
        publish(next);
    }

    HostDebug::log("Chain rebuilt: " + juce::String((int) next->size()) + " slot(s), allOk=" + juce::String((int) allOk));
    return allOk;
}

bool PluginChain::switchWithCrossfade(const juce::Array<SlotSpec>& specs, int crossfadeMs)
{
    bool allOk = false;
    auto next = buildList(specs, allOk);   // built outside the lock

    {
        const juce::ScopedLock sl(editLock);

        if (crossfadeMs <= 0)
            publish(next);
        else
            publishWithCrossfade(next, crossfadeMs);
    }

    HostDebug::log("Chain switch (crossfade " + juce::String(crossfadeMs) + " ms): "
                   + juce::String((int) next->size()) + " slot(s)");
    return allOk;
}

bool PluginChain::rebuildFrom(const juce::Array<SlotSpec>& specs,
                              const juce::Array<SectionDef>& sections)
{
    cancelAsyncBuild();   // discard any in-progress async load

    // Replace section definitions first (message-thread only, no lock needed yet).
    sectionDefs.clear();
    for (const auto& s : sections)
        sectionDefs.push_back(s);

    if (sectionDefs.empty())
        sectionDefs.push_back({ 1, "Stomp 1", SectionDef::Type::stomp });

    // Recompute generation counters so future addSection() names don't collide.
    nextSectionId   = 1;
    nextStompCount  = 0;
    nextPresetCount = 0;
    for (const auto& def : sectionDefs)
    {
        nextSectionId = std::max(nextSectionId, def.id + 1);
        if (def.type == SectionDef::Type::stomp) ++nextStompCount;
        else                                     ++nextPresetCount;
    }

    bool allOk = false;
    auto next = buildList(specs, allOk);   // built outside the lock

    {
        const juce::ScopedLock sl(editLock);
        publish(next);
    }

    HostDebug::log("Chain rebuilt (" + juce::String(sections.size()) + " section(s)): "
                   + juce::String((int) next->size()) + " slot(s), allOk=" + juce::String((int) allOk));
    return allOk;
}

bool PluginChain::switchWithCrossfade(const juce::Array<SlotSpec>& specs,
                                      const juce::Array<SectionDef>& sections,
                                      int crossfadeMs)
{
    sectionDefs.clear();
    for (const auto& s : sections)
        sectionDefs.push_back(s);

    if (sectionDefs.empty())
        sectionDefs.push_back({ 1, "Stomp 1", SectionDef::Type::stomp });

    nextSectionId   = 1;
    nextStompCount  = 0;
    nextPresetCount = 0;
    for (const auto& def : sectionDefs)
    {
        nextSectionId = std::max(nextSectionId, def.id + 1);
        if (def.type == SectionDef::Type::stomp) ++nextStompCount;
        else                                     ++nextPresetCount;
    }

    bool allOk = false;
    auto next = buildList(specs, allOk);   // built outside the lock

    {
        const juce::ScopedLock sl(editLock);
        if (crossfadeMs <= 0)
            publish(next);
        else
            publishWithCrossfade(next, crossfadeMs);
    }

    HostDebug::log("Chain switch with sections (crossfade " + juce::String(crossfadeMs) + " ms): "
                   + juce::String((int) next->size()) + " slot(s)");
    return allOk;
}

int PluginChain::preloadChain(const juce::Array<SlotSpec>& specs)
{
    bool allOk = false;
    auto list = buildList(specs, allOk);   // built outside the lock

    const juce::ScopedLock sl(editLock);
    const int handle = nextPreloadHandle++;
    preloaded[handle] = list;
    HostDebug::log("Preloaded chain handle=" + juce::String(handle)
                   + " (" + juce::String((int) list->size()) + " slots, allOk=" + juce::String((int) allOk) + ")");
    return handle;
}

bool PluginChain::activateChain(int handle, int crossfadeMs)
{
    const juce::ScopedLock sl(editLock);

    auto it = preloaded.find(handle);

    if (it == preloaded.end())
    {
        HostDebug::log("activateChain: unknown handle " + juce::String(handle));
        return false;
    }

    auto list = it->second;
    preloaded.erase(it);

    const double t0 = juce::Time::getMillisecondCounterHiRes();

    if (crossfadeMs <= 0)
        publish(list);
    else
        publishWithCrossfade(list, crossfadeMs);

    const double switchMs = juce::Time::getMillisecondCounterHiRes() - t0;

    HostDebug::log("Activated preloaded chain handle=" + juce::String(handle)
                   + " (crossfade " + juce::String(crossfadeMs) + " ms)"
                   + " | switch " + juce::String(switchMs, 3) + " ms (target <50)");
    return true;
}

void PluginChain::releasePreload(int handle)
{
    const juce::ScopedLock sl(editLock);
    preloaded.erase(handle);
}

// ── Async build entry points ─────────────────────────────────────────────────

void PluginChain::buildChainAsync(const juce::Array<SlotSpec>& specs,
                                   const juce::Array<SectionDef>& sections,
                                   std::function<void(int, int)> onProgress,
                                   std::function<void(int, bool)> onComplete)
{
    asyncEpoch.fetch_add(1);

    // Move the old thread into the graveyard so it can finish its current slot naturally
    // (it will check asyncEpoch / threadShouldExit and exit without calling back).
    if (loaderThread != nullptr)
    {
        loaderThread->signalThreadShouldExit();
        loaderGraveyard.push_back(std::move(loaderThread));
    }

    // Prune graveyard threads that have already finished.
    loaderGraveyard.erase(
        std::remove_if(loaderGraveyard.begin(), loaderGraveyard.end(),
                       [](const auto& t) { return ! t->isThreadRunning(); }),
        loaderGraveyard.end());

    loaderThread = std::make_unique<PluginLoaderThread>(
        *this, loaderAliveFlag,
        specs, sections,
        asyncEpoch.load(), nextSlotId,
        std::move(onProgress), std::move(onComplete));

    loaderThread->startThread(juce::Thread::Priority::low);
}

void PluginChain::cancelAsyncBuild()
{
    asyncEpoch.fetch_add(1);
    if (loaderThread != nullptr)
        loaderThread->signalThreadShouldExit();
}

void PluginChain::commitAsyncBuild(const juce::Array<SectionDef>& targetSections,
                                    std::shared_ptr<SlotList> partial,
                                    int epoch, int newNextSlotId,
                                    std::function<void(int, bool)> onComplete,
                                    bool allOk)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (asyncEpoch.load() != epoch)
        return;   // a newer build started — discard this result

    // Commit section defs on the message thread.
    sectionDefs.clear();
    for (const auto& s : targetSections)
        sectionDefs.push_back(s);
    if (sectionDefs.empty())
        sectionDefs.push_back({ 1, "Stomp 1", SectionDef::Type::stomp });

    nextSectionId   = 1;
    nextStompCount  = 0;
    nextPresetCount = 0;
    for (const auto& def : sectionDefs)
    {
        nextSectionId = std::max(nextSectionId, def.id + 1);
        if (def.type == SectionDef::Type::stomp) ++nextStompCount;
        else                                     ++nextPresetCount;
    }

    nextSlotId = std::max(nextSlotId, newNextSlotId);

    int handle;
    {
        const juce::ScopedLock sl(editLock);
        handle = nextPreloadHandle++;
        preloaded[handle] = std::move(partial);
    }

    HostDebug::log("Async chain build complete (background): handle=" + juce::String(handle)
                   + " allOk=" + juce::String((int) allOk));

    if (onComplete)
        onComplete(handle, allOk);
}

int PluginChain::getNumSlots() const
{
    return (int) displayList()->size();
}

juce::Array<PluginChain::SlotInfo> PluginChain::getSlotInfos() const
{
    auto cur = displayList();
    juce::Array<SlotInfo> infos;

    for (const auto& slot : *cur)
    {
        SlotInfo info;
        info.originalName = slot->instance != nullptr ? slot->instance->getName() : slot->description.name;
        info.hasCustomName = slot->customName.isNotEmpty();
        info.name      = info.hasCustomName ? slot->customName : info.originalName;
        info.format    = slot->description.pluginFormatName;
        info.bypassed  = slot->bypassed.load();
        info.sectionId = slot->sectionId;
        info.slotId    = slot->slotId;
        info.postGain  = slot->postGain.load();

        for (const auto& def : sectionDefs)
            if (def.id == slot->sectionId) { info.isPreset = (def.type == SectionDef::Type::preset); break; }

        infos.add(info);
    }

    return infos;
}

bool PluginChain::isBypassed(int index) const
{
    auto cur = currentList();
    return juce::isPositiveAndBelow(index, (int) cur->size()) && (*cur)[(size_t) index]->bypassed.load();
}

int PluginChain::getTotalLatencySamples() const
{
    auto cur = currentList();
    int total = 0;

    for (const auto& slot : *cur)
        if (slot->instance != nullptr && ! slot->bypassed.load())
            total += slot->instance->getLatencySamples();

    return total;
}

juce::Array<PluginChain::SlotSpec> PluginChain::captureSpecs() const
{
    auto cur = displayList();
    juce::Array<SlotSpec> specs;

    for (const auto& slot : *cur)
    {
        if (slot->instance == nullptr)
            continue;

        SlotSpec spec;
        spec.description = slot->description;
        spec.bypassed    = slot->bypassed.load();
        spec.customName  = slot->customName;
        spec.sectionId   = slot->sectionId;
        spec.slotId      = slot->slotId;
        spec.postGain    = slot->postGain.load();
        slot->instance->getStateInformation(spec.state);
        specs.add(spec);
    }

    return specs;
}

juce::AudioPluginInstance* PluginChain::getInstance(int index) const
{
    auto cur = displayList();

    if (juce::isPositiveAndBelow(index, (int) cur->size()))
        return (*cur)[(size_t) index]->instance.get();

    return nullptr;
}

void PluginChain::prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels)
{
    const juce::ScopedLock sl(editLock);

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    currentInputChannels = juce::jmax(1, inputChannels);
    currentOutputChannels = juce::jmax(1, outputChannels);

    auto cur = activeList.load();   // the chain currently running on the audio thread

    HostDebug::log("Chain prepare: " + juce::String(sampleRate, 1) + " Hz, block " + juce::String(blockSize)
                   + ", in=" + juce::String(currentInputChannels) + ", out=" + juce::String(currentOutputChannels)
                   + ", slots=" + juce::String((int) cur->size()));

    for (const auto& slot : *cur)
        prepareSlot(*slot);

    if (auto in = fadeInList.load())   // a crossfade target, if mid-transition
        for (const auto& slot : *in)
            prepareSlot(*slot);

    for (auto& entry : preloaded)      // keep preloaded chains ready at the current sr/bs
        for (const auto& slot : *entry.second)
            prepareSlot(*slot);

    processingChannelCount.store(computeChannelCount(*cur));
}

void PluginChain::processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    auto in  = fadeInList.load();
    auto act = activeList.load();   // lock-free: copy shared_ptrs, keep lists alive for this block

    if (in == nullptr)
    {
        runList(*act, buffer, midiMessages);   // steady state
        return;
    }

    // ── Crossfade: run the outgoing (act) and incoming (in) chains in parallel ──
    const auto epoch = transitionEpoch.load();

    if (epoch != lastTransitionEpoch)
    {
        lastTransitionEpoch = epoch;
        crossfadePos = 0;
        crossfadeTotal = juce::jmax(1, requestedFadeSamples.load());
    }

    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    fadeScratch.setSize(numCh, numSamples, false, false, true);

    for (int ch = 0; ch < numCh; ++ch)
        fadeScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    juce::MidiBuffer midiForOutgoing(midiMessages);

    runList(*act, fadeScratch, midiForOutgoing);   // outgoing -> scratch
    runList(*in,  buffer,      midiMessages);       // incoming -> buffer

    // Equal-power mix: out = scratch*cos(g) + buffer*sin(g), g ramps 0 -> pi/2.
    for (int s = 0; s < numSamples; ++s)
    {
        const float t = juce::jlimit(0.0f, 1.0f, (float) (crossfadePos + s) / (float) crossfadeTotal);
        const float gOut = std::cos(t * juce::MathConstants<float>::halfPi);
        const float gIn  = std::sin(t * juce::MathConstants<float>::halfPi);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.setSample(ch, s, fadeScratch.getSample(ch, s) * gOut + buffer.getSample(ch, s) * gIn);
    }

    crossfadePos += numSamples;

    if (crossfadePos >= crossfadeTotal)
    {
        activeList.store(in);          // promote incoming to active (atomic, no deletion here)
        fadeInList.store(nullptr);     // transition complete
    }
}
