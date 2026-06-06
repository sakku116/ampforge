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

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

private:
    ControlAction match(const ControlTrigger& incoming) const;

    std::vector<ControlBinding> bindings;
};
