#pragma once

#include <JuceHeader.h>
#include <vector>

/** A control trigger: an incoming MIDI event or a computer-key press. */
struct ControlTrigger
{
    enum class Type { none, midiNote, midiCC, midiProgram, key };

    Type type = Type::none;
    int channel = 0;   // MIDI channel 1-16, 0 = any (keys use 0)
    int number = 0;    // note / CC / program number, or key code

    bool isValid() const { return type != Type::none; }
    bool matches(const ControlTrigger& incoming) const;
    juce::String toString() const;
};

/** What a trigger does. Scene actions become live once scenes exist (Phase 4.5). */
struct ControlAction
{
    enum class Type { none, nextScene, prevScene, loadScene, toggleBypass };

    Type type = Type::none;
    int index = 0;   // scene index (loadScene) or slot index (toggleBypass)

    bool isValid() const { return type != Type::none; }
    juce::String toString() const;
};

struct ControlBinding
{
    ControlTrigger trigger;
    ControlAction action;
};

/** Continuous mapping: a MIDI CC drives a plugin parameter (expression pedal). */
struct ExpressionBinding
{
    int channel = 0;     // 1-16, 0 = any
    int ccNumber = 0;
    int slotIndex = 0;
    int paramIndex = 0;
    juce::String toString() const;
};

/** A resolved target to apply (slot/param + normalised 0..1 value). */
struct ExpressionTarget
{
    int slotIndex = 0;
    int paramIndex = 0;
    float value = 0.0f;
};

/** Maps triggers (MIDI / keyboard / footswitch) to actions. Pure lookup + storage;
    the owner executes the returned actions. */
class ControlMap
{
public:
    void addBinding(ControlBinding binding);
    void removeBinding(int index);
    void clear();

    int getNumBindings() const { return (int) bindings.size(); }
    const ControlBinding& getBinding(int index) const { return bindings[(size_t) index]; }

    /** Builds a trigger from a MIDI message (for matching or MIDI-learn). type==none if not mappable. */
    static ControlTrigger triggerFromMidi(const juce::MidiMessage& message);

    ControlAction matchMidi(const juce::MidiMessage& message) const;
    ControlAction matchKey(int keyCode) const;

    // ── Expression (continuous CC -> parameter) ──────────────────────────────
    void addExpression(ExpressionBinding binding);
    void removeExpression(int index);
    int getNumExpressions() const { return (int) expressions.size(); }
    const ExpressionBinding& getExpression(int index) const { return expressions[(size_t) index]; }
    /** Resolves any expression bindings that a CC message drives. */
    juce::Array<ExpressionTarget> matchExpressions(const juce::MidiMessage& message) const;

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

private:
    ControlAction match(const ControlTrigger& incoming) const;

    std::vector<ControlBinding> bindings;
    std::vector<ExpressionBinding> expressions;
};
