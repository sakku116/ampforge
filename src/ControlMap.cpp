#include "ControlMap.h"

#include <algorithm>

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
        case Type::key:         return "Key " + juce::KeyPress(number).getTextDescription();
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
        case Type::nextTemplate:    return "Next Template";
        case Type::prevTemplate:    return "Prev Template";
        case Type::loadTemplate:    return "Load Template " + juce::String(index + 1);
        case Type::toggleBypass:       return "Toggle Bypass slot " + juce::String(index + 1);
        case Type::activatePresetSlot: return "Activate Preset Slot " + juce::String(index + 1);
        case Type::none:
        default:                       return "—";
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

int ControlMap::removeInvalidSlotBindings(const juce::Array<int>& validSlotIds)
{
    const auto before = bindings.size();

    std::erase_if(bindings, [&validSlotIds](const ControlBinding& binding)
    {
        const auto type = binding.action.type;
        const bool isSlotAction = type == ControlAction::Type::toggleBypass
                               || type == ControlAction::Type::activatePresetSlot;
        return isSlotAction && ! validSlotIds.contains(binding.action.index);
    });

    return (int) (before - bindings.size());
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

bool ControlMap::fromValueTree(const juce::ValueTree& tree)
{
    if (! tree.hasType("CONTROLMAP"))
        return false;

    const auto integer = [](const juce::ValueTree& node, const char* key, int& out)
    {
        if (! node.hasProperty(key)) return false;
        const auto text = node.getProperty(key).toString();
        if (! text.containsOnly("-0123456789") || text.isEmpty()) return false;
        out = text.getIntValue();
        return text == juce::String(out);
    };
    std::vector<ControlBinding> newBindings;
    std::vector<ExpressionBinding> newExpressions;
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto node = tree.getChild(i);
        if (node.getNumChildren() != 0) return false;
        if (node.hasType("BINDING"))
        {
            ControlBinding b;
            int trigger = 0, action = 0;
            if (! integer(node, "trigType", trigger) || ! integer(node, "trigChannel", b.trigger.channel)
                || ! integer(node, "trigNumber", b.trigger.number) || ! integer(node, "actType", action)
                || ! integer(node, "actIndex", b.action.index)) return false;
            if (trigger < 1 || trigger > 4 || action < 1 || action > 5
                || b.trigger.channel < 0 || b.trigger.channel > 16 || b.trigger.number < 0
                || b.trigger.number > (trigger == 4 ? 0x00ffffff : 127)
                || (trigger == 4 && b.trigger.channel != 0) || b.action.index < 0)
                return false;
            b.trigger.type = (ControlTrigger::Type) trigger;
            b.action.type = (ControlAction::Type) action;
            newBindings.push_back(b);
        }
        else if (node.hasType("EXPR"))
        {
            ExpressionBinding e;
            if (! integer(node, "channel", e.channel) || ! integer(node, "cc", e.ccNumber)
                || ! integer(node, "slot", e.slotIndex) || ! integer(node, "param", e.paramIndex)
                || e.channel < 0 || e.channel > 16 || e.ccNumber < 0 || e.ccNumber > 127
                || e.slotIndex < 0 || e.paramIndex < 0) return false;
            newExpressions.push_back(e);
        }
        else return false;
    }
    bindings = std::move(newBindings);
    expressions = std::move(newExpressions);
    return true;
}
