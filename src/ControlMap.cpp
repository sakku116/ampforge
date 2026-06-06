#include "ControlMap.h"

bool ControlTrigger::matches(const ControlTrigger& incoming) const
{
    if (type != incoming.type)
        return false;

    if (number != incoming.number)
        return false;

    // channel 0 = "any" on either side
    if (channel != 0 && incoming.channel != 0 && channel != incoming.channel)
        return false;

    return true;
}

juce::String ControlTrigger::toString() const
{
    const auto chan = channel == 0 ? juce::String("any") : juce::String(channel);

    switch (type)
    {
        case Type::midiNote:    return "Note " + juce::String(number) + " (ch " + chan + ")";
        case Type::midiCC:      return "CC " + juce::String(number) + " (ch " + chan + ")";
        case Type::midiProgram: return "Program " + juce::String(number) + " (ch " + chan + ")";
        case Type::key:         return "Key " + juce::String(number);
        case Type::none:
        default:                return "—";
    }
}

juce::String ControlAction::toString() const
{
    switch (type)
    {
        case Type::nextScene:    return "Next Scene";
        case Type::prevScene:    return "Prev Scene";
        case Type::loadScene:    return "Load Scene " + juce::String(index + 1);
        case Type::toggleBypass: return "Toggle Bypass slot " + juce::String(index + 1);
        case Type::none:
        default:                 return "—";
    }
}

void ControlMap::addBinding(ControlBinding binding)
{
    bindings.push_back(std::move(binding));
}

void ControlMap::removeBinding(int index)
{
    if (juce::isPositiveAndBelow(index, (int) bindings.size()))
        bindings.erase(bindings.begin() + index);
}

void ControlMap::clear()
{
    bindings.clear();
}

ControlTrigger ControlMap::triggerFromMidi(const juce::MidiMessage& message)
{
    ControlTrigger trigger;
    trigger.channel = message.getChannel();   // 1-16, or 0 if no channel

    if (message.isNoteOn())
    {
        trigger.type = ControlTrigger::Type::midiNote;
        trigger.number = message.getNoteNumber();
    }
    else if (message.isController())
    {
        trigger.type = ControlTrigger::Type::midiCC;
        trigger.number = message.getControllerNumber();
    }
    else if (message.isProgramChange())
    {
        trigger.type = ControlTrigger::Type::midiProgram;
        trigger.number = message.getProgramChangeNumber();
    }

    return trigger;
}

ControlAction ControlMap::match(const ControlTrigger& incoming) const
{
    if (! incoming.isValid())
        return {};

    for (const auto& binding : bindings)
        if (binding.trigger.matches(incoming))
            return binding.action;

    return {};
}

ControlAction ControlMap::matchMidi(const juce::MidiMessage& message) const
{
    return match(triggerFromMidi(message));
}

ControlAction ControlMap::matchKey(int keyCode) const
{
    ControlTrigger trigger;
    trigger.type = ControlTrigger::Type::key;
    trigger.number = keyCode;
    return match(trigger);
}

juce::ValueTree ControlMap::toValueTree() const
{
    juce::ValueTree root("CONTROLMAP");

    for (const auto& binding : bindings)
    {
        juce::ValueTree node("BINDING");
        node.setProperty("trigType", (int) binding.trigger.type, nullptr);
        node.setProperty("trigChannel", binding.trigger.channel, nullptr);
        node.setProperty("trigNumber", binding.trigger.number, nullptr);
        node.setProperty("actType", (int) binding.action.type, nullptr);
        node.setProperty("actIndex", binding.action.index, nullptr);
        root.addChild(node, -1, nullptr);
    }

    return root;
}

void ControlMap::fromValueTree(const juce::ValueTree& tree)
{
    bindings.clear();

    if (! tree.hasType("CONTROLMAP"))
        return;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto node = tree.getChild(i);

        if (! node.hasType("BINDING"))
            continue;

        ControlBinding binding;
        binding.trigger.type    = (ControlTrigger::Type) (int) node.getProperty("trigType");
        binding.trigger.channel = (int) node.getProperty("trigChannel");
        binding.trigger.number  = (int) node.getProperty("trigNumber");
        binding.action.type     = (ControlAction::Type) (int) node.getProperty("actType");
        binding.action.index    = (int) node.getProperty("actIndex");
        bindings.push_back(binding);
    }
}
