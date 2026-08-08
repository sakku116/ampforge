// Deterministic host-side tests for the Controller Bridge (#11).
// Exercises the bridge's public MIDI boundary with simulated incoming messages and a
// fake outgoing endpoint — no device, no timing, no audio thread involvement.
// Tiny harness (no framework): CHECK macros, exit code 0 = pass.

#include <cstdio>
#include <array>
#include <memory>
#include <thread>

#include <JuceHeader.h>

#include "../src/ControllerBridge.h"

namespace
{
    int checks = 0;
    int failures = 0;

    #define CHECK(cond)                                                        \
        do {                                                                   \
            ++checks;                                                          \
            if (! (cond)) {                                                    \
                ++failures;                                                    \
                std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            }                                                                  \
        } while (false)

    // ── fake outgoing MIDI endpoint ──────────────────────────────────────────
    struct FakeEndpoint
    {
        juce::Array<juce::MidiMessage> sent;

        void send(const juce::MidiMessage& message) { sent.add(message); }
        void clear() { sent.clear(); }
    };

    // ── host-state provider helpers ──────────────────────────────────────────
    struct ButtonSpec
    {
        int note;
        juce::String label;
        bool isPreset = false;
        bool bypassed = false;
        bool sectionBypassed = false;
    };

    ControllerBridge::HostState stateFrom(const std::initializer_list<ButtonSpec>& specs)
    {
        ControllerBridge::HostState state;

        for (const auto& s : specs)
        {
            ControllerBridge::HostState::Button button;
            button.note = s.note;
            button.label = s.label;
            button.isPreset = s.isPreset;
            button.bypassed = s.bypassed;
            button.sectionBypassed = s.sectionBypassed;
            state.buttons.push_back(button);
        }

        return state;
    }

    // ── protocol wire helpers (what a compatible phone sends) ───────────────
    juce::MidiMessage ready(uint8_t major, uint8_t minor)
    {
        const uint8_t payload[] = { tf::ctrl::manufacturerId, tf::ctrl::deviceId, tf::ctrl::cmdReady, major, minor };
        return juce::MidiMessage::createSysExMessage(payload, (int) sizeof payload);
    }

    juce::MidiMessage sysEx(const std::initializer_list<uint8_t>& payload)
    {
        juce::MemoryBlock block;
        for (const auto b : payload)
            block.append(&b, 1);
        return juce::MidiMessage::createSysExMessage(block.getData(), (int) block.getSize());
    }

    bool decodeSnapshot(const juce::MidiMessage& message,
                        std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons>& out)
    {
        uint8_t major = 0, minor = 0;
        return tf::ctrl::isSnapshot(message, major, minor, out);
    }

    int countUpdates(const FakeEndpoint& endpoint, std::array<bool, tf::ctrl::numButtons>* seen = nullptr)
    {
        int updates = 0;

        for (const auto& message : endpoint.sent)
        {
            tf::ctrl::ButtonDescriptor button;
            if (tf::ctrl::isButtonUpdate(message, button))
            {
                ++updates;
                if (seen != nullptr && button.index < tf::ctrl::numButtons)
                    (*seen)[button.index] = true;
            }
        }

        return updates;
    }

    const tf::ctrl::ButtonDescriptor& buttonAt(const std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons>& mirror, int index)
    {
        return mirror[(size_t) index];
    }
}

int main()
{
    // The thread that first creates the MessageManager becomes the message thread,
    // so handleIncomingMidi's ready handling runs synchronously here.
    juce::MessageManager::getInstance();

    // 1. No controller: everything passes through, nothing is sent, status stays disconnected.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });

        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::noteOn(16, 60, 100.0f), "Phone"));
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::noteOn(1, 60, 100.0f), "Keyboard"));
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::controllerEvent(16, 64, 127), "Phone"));
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::programChange(16, 3), "Phone"));
        CHECK(endpoint.sent.isEmpty());
        CHECK(bridge.getStatus() == ControllerBridge::Status::disconnected);
        CHECK(! bridge.isConnected());

        // Ready never sent → refresh() stays silent too.
        bridge.refresh();
        CHECK(endpoint.sent.isEmpty());
    }

    // 2. Ready → complete versioned snapshot with every visual state variant.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });
        bridge.setHostStateProvider([]
        {
            return stateFrom({
                { 60, "Drive" },                              // button 0: active stomp
                { 65, "Lead Solo", true, true, true },        // button 5: inactive preset, section bypassed
            });
        });

        CHECK(bridge.handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone"));
        CHECK(endpoint.sent.size() == 1);

        std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons> mirror {};
        uint8_t major = 0, minor = 0;
        CHECK(tf::ctrl::isSnapshot(endpoint.sent.getFirst(), major, minor, mirror));
        CHECK(major == tf::ctrl::protocolMajor);
        CHECK(minor == tf::ctrl::protocolMinor);

        const auto& stomp = buttonAt(mirror, 0);
        CHECK(stomp.assigned);
        CHECK(! stomp.isPreset);
        CHECK(stomp.active);
        CHECK(! stomp.bypassed);
        CHECK(! stomp.sectionBypassed);
        CHECK(stomp.label == "Drive");

        const auto& preset = buttonAt(mirror, 5);
        CHECK(preset.assigned);
        CHECK(preset.isPreset);
        CHECK(! preset.active);
        CHECK(preset.bypassed);
        CHECK(preset.sectionBypassed);
        CHECK(preset.label == "Lead Solo");

        for (int i = 0; i < tf::ctrl::numButtons; ++i)
        {
            CHECK(buttonAt(mirror, i).index == (uint8_t) i);
            if (i != 0 && i != 5)
            {
                CHECK(! buttonAt(mirror, i).assigned);
                CHECK(buttonAt(mirror, i).label.isEmpty());
            }
        }

        CHECK(bridge.getStatus() == ControllerBridge::Status::connected);
    }

    // 3. Malformed / foreign messages are rejected: no consumption, no output.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });
        bridge.setHostStateProvider([]{ return stateFrom({ { 60, "Drive" } }); });

        uint8_t major = 0, minor = 0;

        // wrong manufacturer id
        CHECK(! bridge.handleIncomingMidi(sysEx({ 0x00, tf::ctrl::deviceId, tf::ctrl::cmdReady, 1, 0 }), "Phone"));
        // wrong device id
        CHECK(! bridge.handleIncomingMidi(sysEx({ tf::ctrl::manufacturerId, 0x11, tf::ctrl::cmdReady, 1, 0 }), "Phone"));
        // wrong command
        CHECK(! bridge.handleIncomingMidi(sysEx({ tf::ctrl::manufacturerId, tf::ctrl::deviceId, 0x99, 1, 0 }), "Phone"));
        // truncated ready (no version bytes)
        CHECK(! bridge.handleIncomingMidi(sysEx({ tf::ctrl::manufacturerId, tf::ctrl::deviceId, tf::ctrl::cmdReady }), "Phone"));
        // ready with trailing junk
        CHECK(! bridge.handleIncomingMidi(sysEx({ tf::ctrl::manufacturerId, tf::ctrl::deviceId, tf::ctrl::cmdReady, 1, 0, 0x42 }), "Phone"));
        // foreign SysEx (other manufacturer)
        CHECK(! bridge.handleIncomingMidi(sysEx({ 0x00, 0x20, 0x7B, 0x01 }), "Phone"));
        // ordinary channel messages
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::noteOn(16, 62, 100.0f), "Phone"));
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::controllerEvent(16, 1, 100), "Phone"));
        // a ready for a different protocol version never came either
        CHECK(! tf::ctrl::isReadyRequest(juce::MidiMessage::noteOn(16, 60, 100.0f), major, minor));

        CHECK(endpoint.sent.isEmpty());
        CHECK(bridge.getStatus() == ControllerBridge::Status::disconnected);
    }

    // 4. Protocol mismatch: visible rejection, no synchronization, full snapshot on next compatible ready.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });
        bridge.setHostStateProvider([]{ return stateFrom({ { 60, "Drive" } }); });

        CHECK(bridge.handleIncomingMidi(ready(0, 0), "Old Phone"));
        CHECK(endpoint.sent.size() == 1);

        uint8_t major = 0, minor = 0;
        CHECK(tf::ctrl::isProtocolMismatch(endpoint.sent.getFirst(), major, minor));
        CHECK(major == tf::ctrl::protocolMajor);
        CHECK(minor == tf::ctrl::protocolMinor);
        CHECK(bridge.getStatus() == ControllerBridge::Status::incompatible);

        // still no mirror synchronization after a mismatch
        bridge.refresh();
        CHECK(endpoint.sent.size() == 1);

        // a compatible ready restores synchronization with a full snapshot
        CHECK(bridge.handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone"));
        CHECK(endpoint.sent.size() == 2);

        std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons> mirror {};
        CHECK(decodeSnapshot(endpoint.sent.getLast(), mirror));
        CHECK(buttonAt(mirror, 0).assigned);
        CHECK(bridge.getStatus() == ControllerBridge::Status::connected);
    }

    // 5. Incremental updates: only changed buttons, none on identical refresh.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });

        auto provider = [] { return stateFrom({ { 60, "Drive" }, { 65, "Lead", true, true } }); };
        bridge.setHostStateProvider(provider);

        CHECK(bridge.handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone"));
        CHECK(endpoint.sent.size() == 1);   // snapshot only

        // change: button 0 relabelled, button 2 assigned, button 5 un-bypassed
        bridge.setHostStateProvider([]
        {
            return stateFrom({ { 60, "Crunch" }, { 62, "Delay", false, false }, { 65, "Lead", true, false } });
        });

        bridge.refresh();
        std::array<bool, tf::ctrl::numButtons> seen {};
        seen.fill(false);
        CHECK(countUpdates(endpoint, &seen) == 3);
        CHECK(seen[0] && seen[2] && seen[5]);
        CHECK(! seen[1] && ! seen[3] && ! seen[4] && ! seen[6] && ! seen[7]);

        // identical refresh → no redundant feedback
        endpoint.clear();
        bridge.refresh();
        CHECK(endpoint.sent.isEmpty());
    }

    // 6. Disconnect + template replacement: reconnect always restarts with a full snapshot.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });

        bridge.setHostStateProvider([]{ return stateFrom({ { 60, "Old" } }); });
        CHECK(bridge.handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone"));
        CHECK(endpoint.sent.size() == 1);

        // controller gone → connection dropped, no audio/chain side effects here
        bridge.notifyDisconnected();
        CHECK(bridge.getStatus() == ControllerBridge::Status::disconnected);
        CHECK(! bridge.isConnected());
        bridge.refresh();   // silent while disconnected
        CHECK(endpoint.sent.size() == 1);

        // template replaced while away; reconnect gets the new mirror as a full snapshot
        bridge.setHostStateProvider([]{ return stateFrom({ { 67, "New Reverb", true, false } }); });
        CHECK(bridge.handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone"));
        CHECK(endpoint.sent.size() == 2);

        std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons> mirror {};
        CHECK(decodeSnapshot(endpoint.sent.getLast(), mirror));
        CHECK(! buttonAt(mirror, 0).assigned);
        CHECK(buttonAt(mirror, 7).assigned);
        CHECK(buttonAt(mirror, 7).isPreset);
        CHECK(buttonAt(mirror, 7).active);
        CHECK(buttonAt(mirror, 7).label == "New Reverb");
    }

    // 7. Channel-16 coexistence: other devices' MIDI passes through untouched and
    //    never perturbs the mirror.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });
        bridge.setHostStateProvider([]{ return stateFrom({ { 60, "Drive" } }); });

        CHECK(bridge.handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone"));
        const int snapshotCount = endpoint.sent.size();
        CHECK(snapshotCount == 1);

        // same note on another channel, unrelated CCs/programs, foreign SysEx
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::noteOn(1, 60, 100.0f), "Keyboard"));
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::controllerEvent(16, 64, 127), "Pedal"));
        CHECK(! bridge.handleIncomingMidi(juce::MidiMessage::programChange(16, 5), "Pedal"));
        CHECK(! bridge.handleIncomingMidi(sysEx({ 0x00, 0x20, 0x7B, 0x01 }), "Other"));

        CHECK(endpoint.sent.size() == snapshotCount);   // no feedback emitted

        // the mirror still holds its state
        std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons> mirror {};
        CHECK(decodeSnapshot(endpoint.sent.getFirst(), mirror));
        CHECK(buttonAt(mirror, 0).label == "Drive");
    }

    // 8. Label canonicalization: 7-bit ASCII, truncated to maxLabelBytes.
    {
        FakeEndpoint endpoint;
        ControllerBridge bridge;
        bridge.setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });

        const juce::String longLabel = juce::String::repeatedString("x", tf::ctrl::maxLabelBytes + 12);
        const juce::String accented = juce::String("C") + juce::String::charToString(0x00E7) + "runch";
        bridge.setHostStateProvider([&]
        {
            return stateFrom({ { 60, accented }, { 61, longLabel } });
        });

        CHECK(bridge.handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone"));
        CHECK(endpoint.sent.size() == 1);

        std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons> mirror {};
        CHECK(decodeSnapshot(endpoint.sent.getFirst(), mirror));
        CHECK(buttonAt(mirror, 0).label == "C?runch");
        CHECK(buttonAt(mirror, 1).label.length() == tf::ctrl::maxLabelBytes);
    }

    // 9. Deferred ready handling from a non-message thread is safe after the bridge
    //    is destroyed (the queued lambda is dropped via the aliveFlag guard).
    {
        auto bridge = std::make_unique<ControllerBridge>();
        FakeEndpoint endpoint;
        bridge->setSendCallback([&endpoint](const juce::MidiMessage& m) { endpoint.send(m); });
        bridge->setHostStateProvider([]{ return stateFrom({ { 60, "Drive" } }); });

        // A MIDI callback thread would call this; here a worker thread does.
        std::atomic<bool> consumed { false };
        std::thread worker([&]
        {
            consumed = bridge->handleIncomingMidi(ready(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor), "Phone");
        });
        worker.join();

        CHECK(consumed.load());
        // Deferred to the message thread, which never pumped in the meantime.
        CHECK(bridge->getStatus() == ControllerBridge::Status::disconnected);
        CHECK(endpoint.sent.isEmpty());

        // Destroy the bridge while the ready lambda is still queued, then pump the
        // real dispatch loop: the guard must drop it without touching the bridge.
        bridge.reset();

        juce::MessageManager::getInstance()->callAsync([]
        {
            juce::MessageManager::getInstance()->stopDispatchLoop();
        });
        juce::MessageManager::getInstance()->runDispatchLoop();
        CHECK(true);   // reached without a use-after-free
    }

    std::printf("%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
