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

juce::String ExpressionBinding::toString() const
{
    const auto chan = channel == 0 ? juce::String("any") : juce::String(channel);
    return "CC " + juce::String(ccNumber) + " (ch " + chan + ") -> slot "
         + juce::String(slotIndex + 1) + " param " + juce::String(paramIndex);
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
    // Footswitches/buttons usually send a CC "on" (>=64) then "off" (<64); only act on "on"
    // so a discrete action fires once per press, not again on release.
    if (message.isController() && message.getControllerValue() < 64)
        return {};

    return match(triggerFromMidi(message));
}

ControlAction ControlMap::matchKey(int keyCode) const
{
    ControlTrigger trigger;
    trigger.type = ControlTrigger::Type::key;
    trigger.number = keyCode;
    return match(trigger);
}

void ControlMap::addExpression(ExpressionBinding binding)
{
    expressions.push_back(binding);
}

void ControlMap::removeExpression(int index)
{
    if (juce::isPositiveAndBelow(index, (int) expressions.size()))
        expressions.erase(expressions.begin() + index);
}

juce::Array<ExpressionTarget> ControlMap::matchExpressions(const juce::MidiMessage& message) const
{
    juce::Array<ExpressionTarget> targets;

    if (! message.isController())
        return targets;

    const int cc = message.getControllerNumber();
    const int chan = message.getChannel();
    const float value = (float) message.getControllerValue() / 127.0f;

    for (const auto& e : expressions)
        if (e.ccNumber == cc && (e.channel == 0 || chan == 0 || e.channel == chan))
            targets.add({ e.slotIndex, e.paramIndex, value });

    return targets;
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

    for (const auto& e : expressions)
    {
        juce::ValueTree node("EXPR");
        node.setProperty("channel", e.channel, nullptr);
        node.setProperty("cc", e.ccNumber, nullptr);
        node.setProperty("slot", e.slotIndex, nullptr);
        node.setProperty("param", e.paramIndex, nullptr);
        root.addChild(node, -1, nullptr);
    }

    return root;
}

void ControlMap::fromValueTree(const juce::ValueTree& tree)
{
    bindings.clear();
    expressions.clear();

    if (! tree.hasType("CONTROLMAP"))
        return;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto node = tree.getChild(i);

        if (node.hasType("BINDING"))
        {
            ControlBinding binding;
            binding.trigger.type    = (ControlTrigger::Type) (int) node.getProperty("trigType");
            binding.trigger.channel = (int) node.getProperty("trigChannel");
            binding.trigger.number  = (int) node.getProperty("trigNumber");
            binding.action.type     = (ControlAction::Type) (int) node.getProperty("actType");
            binding.action.index    = (int) node.getProperty("actIndex");
            bindings.push_back(binding);
        }
        else if (node.hasType("EXPR"))
        {
            ExpressionBinding e;
            e.channel    = (int) node.getProperty("channel");
            e.ccNumber   = (int) node.getProperty("cc");
            e.slotIndex  = (int) node.getProperty("slot");
            e.paramIndex = (int) node.getProperty("param");
            expressions.push_back(e);
        }
    }
}
