#include "PluginChain.h"
#include "HostDebug.h"
#include "PluginScanGuard.h"

PluginChain::PluginChain(juce::AudioPluginFormatManager& manager) : formatManager(manager)
{
}

PluginChain::~PluginChain()
{
    clear();
}

std::unique_ptr<PluginChain::Slot> PluginChain::createSlot(const juce::PluginDescription& description,
                                                          juce::String& error)
{
    // Created outside the lock — instance creation hits disk and can be slow.
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

    auto slot = std::make_unique<Slot>();
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

void PluginChain::updateProcessingChannelCount()
{
    int maxChannels = 0;

    for (auto* slot : slots)
        if (slot->instance != nullptr)
            maxChannels = juce::jmax(maxChannels,
                                     slot->instance->getTotalNumInputChannels(),
                                     slot->instance->getTotalNumOutputChannels());

    processingChannelCount.store(maxChannels);
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
        juce::ScopedLock sl(lock);
        slots.add(slot.release());
        updateProcessingChannelCount();
    }

    HostDebug::log("Chain add OK: " + description.name
                   + " (slots=" + juce::String(slots.size()) + ")");
    return true;
}

void PluginChain::removePlugin(int index)
{
    juce::ScopedLock sl(lock);

    if (! juce::isPositiveAndBelow(index, slots.size()))
        return;

    if (slots[index]->instance != nullptr)
        slots[index]->instance->releaseResources();

    slots.remove(index);   // OwnedArray deletes the slot
    updateProcessingChannelCount();
    HostDebug::log("Chain remove slot " + juce::String(index) + " (slots=" + juce::String(slots.size()) + ")");
}

void PluginChain::movePlugin(int fromIndex, int toIndex)
{
    juce::ScopedLock sl(lock);

    if (! juce::isPositiveAndBelow(fromIndex, slots.size()))
        return;

    toIndex = juce::jlimit(0, slots.size() - 1, toIndex);
    slots.move(fromIndex, toIndex);
    HostDebug::log("Chain move " + juce::String(fromIndex) + " -> " + juce::String(toIndex));
}

void PluginChain::setBypass(int index, bool shouldBypass)
{
    juce::ScopedLock sl(lock);

    if (juce::isPositiveAndBelow(index, slots.size()))
        slots[index]->bypassed.store(shouldBypass);
}

void PluginChain::clear()
{
    juce::ScopedLock sl(lock);

    for (auto* slot : slots)
        if (slot->instance != nullptr)
            slot->instance->releaseResources();

    slots.clear();
    processingChannelCount.store(0);
}

bool PluginChain::rebuildFrom(const juce::Array<SlotSpec>& specs)
{
    // Build all instances first (outside the lock), then swap them in atomically.
    juce::OwnedArray<Slot> newSlots;
    bool allOk = true;

    for (const auto& spec : specs)
    {
        juce::String error;
        auto slot = createSlot(spec.description, error);

        if (slot == nullptr)
        {
            HostDebug::log("Chain rebuild: slot FAILED " + spec.description.name + " — " + error);
            allOk = false;
            continue;
        }

        if (spec.state.getSize() > 0)
            slot->instance->setStateInformation(spec.state.getData(), (int) spec.state.getSize());

        slot->bypassed.store(spec.bypassed);
        newSlots.add(slot.release());
    }

    {
        juce::ScopedLock sl(lock);

        for (auto* slot : slots)
            if (slot->instance != nullptr)
                slot->instance->releaseResources();

        slots.clear();
        slots.swapWith(newSlots);
        updateProcessingChannelCount();
    }

    HostDebug::log("Chain rebuilt: " + juce::String(slots.size()) + " slot(s), allOk=" + juce::String((int) allOk));
    return allOk;
}

int PluginChain::getNumSlots() const
{
    juce::ScopedLock sl(lock);
    return slots.size();
}

juce::Array<PluginChain::SlotInfo> PluginChain::getSlotInfos() const
{
    juce::ScopedLock sl(lock);
    juce::Array<SlotInfo> infos;

    for (auto* slot : slots)
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
    juce::ScopedLock sl(lock);
    return juce::isPositiveAndBelow(index, slots.size()) && slots[index]->bypassed.load();
}

juce::Array<PluginChain::SlotSpec> PluginChain::captureSpecs() const
{
    juce::ScopedLock sl(lock);
    juce::Array<SlotSpec> specs;

    for (auto* slot : slots)
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
    juce::ScopedLock sl(lock);

    if (juce::isPositiveAndBelow(index, slots.size()))
        return slots[index]->instance.get();

    return nullptr;
}

void PluginChain::prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels)
{
    juce::ScopedLock sl(lock);

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    currentInputChannels = juce::jmax(1, inputChannels);
    currentOutputChannels = juce::jmax(1, outputChannels);

    HostDebug::log("Chain prepare: " + juce::String(sampleRate, 1) + " Hz, block " + juce::String(blockSize)
                   + ", in=" + juce::String(currentInputChannels) + ", out=" + juce::String(currentOutputChannels)
                   + ", slots=" + juce::String(slots.size()));

    for (auto* slot : slots)
        prepareSlot(*slot);

    updateProcessingChannelCount();
}

void PluginChain::processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedLock sl(lock);

    for (auto* slot : slots)
    {
        if (slot->instance == nullptr || slot->bypassed.load())
            continue;   // bypassed slot: audio passes through untouched

        slot->instance->processBlock(buffer, midiMessages);
    }
}
