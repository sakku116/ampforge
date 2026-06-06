#include "PluginChain.h"
#include "HostDebug.h"
#include "PluginScanGuard.h"

#include <algorithm>
#include <cmath>

PluginChain::PluginChain(juce::AudioPluginFormatManager& manager) : formatManager(manager)
{
}

PluginChain::~PluginChain()
{
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
    processingChannelCount.store(computeChannelCount(*next));

    fadeInList.store(nullptr);   // cancel any in-progress crossfade

    auto old = activeList.exchange(next);

    if (old != nullptr)
        retired.push_back(std::move(old));

    reclaimRetired();
}

void PluginChain::publishWithCrossfade(std::shared_ptr<SlotList> next, int crossfadeMs)
{
    processingChannelCount.store(juce::jmax(computeChannelCount(*currentList()),
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
        if (slot->bypassed.load())
            continue;

        slot->instance->processBlock(buffer, midi);
    }
}

void PluginChain::reclaimRetired()
{
    // Drop old lists the audio thread no longer references (use_count() <= 1 means only
    // 'retired' holds it), destroying their removed instances here on the message thread.
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [](const std::shared_ptr<SlotList>& l) { return l.use_count() <= 1; }),
                  retired.end());
}

bool PluginChain::addPlugin(const juce::PluginDescription& description)
{
    juce::String error;
    auto slot = createSlot(description, error);

    if (slot == nullptr)
    {
        HostDebug::log("Chain add FAILED: " + description.name + " — " + error);
        return false;
    }

    {
        const juce::ScopedLock sl(editLock);
        auto next = std::make_shared<SlotList>(*currentList());
        next->push_back(std::move(slot));
        publish(next);
        HostDebug::log("Chain add OK: " + description.name + " (slots=" + juce::String((int) next->size()) + ")");
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

void PluginChain::movePlugin(int fromIndex, int toIndex)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();
    const int n = (int) cur->size();

    if (! juce::isPositiveAndBelow(fromIndex, n))
        return;

    toIndex = juce::jlimit(0, n - 1, toIndex);

    if (toIndex == fromIndex)
        return;

    auto next = std::make_shared<SlotList>(*cur);
    auto slot = (*next)[(size_t) fromIndex];
    next->erase(next->begin() + fromIndex);
    next->insert(next->begin() + toIndex, slot);
    publish(next);
    HostDebug::log("Chain move " + juce::String(fromIndex) + " -> " + juce::String(toIndex));
}

void PluginChain::setBypass(int index, bool shouldBypass)
{
    const juce::ScopedLock sl(editLock);
    auto cur = currentList();

    if (juce::isPositiveAndBelow(index, (int) cur->size()))
        (*cur)[(size_t) index]->bypassed.store(shouldBypass);   // atomic flag; no republish needed
}

void PluginChain::clear()
{
    const juce::ScopedLock sl(editLock);
    publish(std::make_shared<SlotList>());
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
        list->push_back(std::move(slot));
    }

    return list;
}

bool PluginChain::rebuildFrom(const juce::Array<SlotSpec>& specs)
{
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

    if (crossfadeMs <= 0)
        publish(list);
    else
        publishWithCrossfade(list, crossfadeMs);

    HostDebug::log("Activated preloaded chain handle=" + juce::String(handle)
                   + " (crossfade " + juce::String(crossfadeMs) + " ms)");
    return true;
}

void PluginChain::releasePreload(int handle)
{
    const juce::ScopedLock sl(editLock);
    preloaded.erase(handle);
}

int PluginChain::getNumSlots() const
{
    return (int) currentList()->size();
}

juce::Array<PluginChain::SlotInfo> PluginChain::getSlotInfos() const
{
    auto cur = currentList();
    juce::Array<SlotInfo> infos;

    for (const auto& slot : *cur)
    {
        SlotInfo info;
        info.name = slot->instance != nullptr ? slot->instance->getName() : slot->description.name;
        info.format = slot->description.pluginFormatName;
        info.bypassed = slot->bypassed.load();
        infos.add(info);
    }

    return infos;
}

bool PluginChain::isBypassed(int index) const
{
    auto cur = currentList();
    return juce::isPositiveAndBelow(index, (int) cur->size()) && (*cur)[(size_t) index]->bypassed.load();
}

juce::Array<PluginChain::SlotSpec> PluginChain::captureSpecs() const
{
    auto cur = currentList();
    juce::Array<SlotSpec> specs;

    for (const auto& slot : *cur)
    {
        if (slot->instance == nullptr)
            continue;

        SlotSpec spec;
        spec.description = slot->description;
        spec.bypassed = slot->bypassed.load();
        slot->instance->getStateInformation(spec.state);
        specs.add(spec);
    }

    return specs;
}

juce::AudioPluginInstance* PluginChain::getInstance(int index) const
{
    auto cur = currentList();

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

    auto cur = currentList();

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
