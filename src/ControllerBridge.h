#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "ControllerProtocol.h"

/** Host-side Controller Bridge (#11): the single seam that consumes the Android
    controller's MIDI, derives the Controller Mirror from host-owned state, and emits
    Controller Snapshot / Controller Feedback SysEx over the Controller MIDI Protocol.

    Threading rules:
      - Message thread owns: connection state, the mirror, refresh(), the host-state
        provider, and the outgoing endpoint.
      - MIDI thread: handleIncomingMidi() only queues a ready request to the message
        thread; it never touches bridge or host state directly.
      - Audio thread: never touched. Without a controller the bridge emits nothing and
        passes every message through, so normal host behavior is unchanged.

    UI and Bluetooth-device mechanics (device polling, pairing) live outside this seam;
    the app feeds notifyDisconnected() when it detects the device is gone. */
class ControllerBridge
{
public:
    enum class Status { disconnected, connected, incompatible };

    /** Host-owned state used to derive the mirror: only learned Stomp/Preset
        Controller Assignments. Built by the app from the ControlMap and the chain. */
    struct HostState
    {
        struct Button
        {
            int          note = 0;             // 60..67 (Controller Note Set)
            juce::String label;
            bool         isPreset = false;
            bool         bypassed = false;      // slot bypassed
            bool         sectionBypassed = false;
        };

        std::vector<Button> buttons;
    };

    ControllerBridge() = default;

    /** Drops any ready requests still queued on the message thread, so the bridge can
        be destroyed safely while the message loop is winding down. */
    ~ControllerBridge();

    /** Outgoing MIDI endpoint. Tests install a fake; the app wires AudioEngine. */
    void setSendCallback(std::function<void(const juce::MidiMessage&)> send);

    /** Source of host-owned mirror state, called on the message thread only. */
    void setHostStateProvider(std::function<HostState()> provider);

    /** Recomputes the mirror from host state and emits feedback: a complete SNAPSHOT
        when no mirror was sent yet (first ready / after a reconnect), otherwise
        incremental UPDATEs for changed buttons. No-op while disconnected. Call on the
        message thread after any control/chain/template change. */
    void refresh();

    /** Consumes an incoming MIDI message. Returns true when the bridge handled it
        (a well-formed READY request) and it must not reach the control map. Every
        other message returns false and passes through. Ready handling runs on the
        message thread: inline when already there, otherwise deferred via callAsync. */
    bool handleIncomingMidi(const juce::MidiMessage& message, const juce::String& sourceDeviceName);

    /** Called when the app detects the controller's MIDI device is gone. Drops the
        connection and forces a full SNAPSHOT on the next READY. */
    void notifyDisconnected();

    /** Connection status for the CONTROL-area UI. */
    Status getStatus() const;

    /** True while a ready request has established a live session. */
    bool isConnected() const { return connected; }

    /** MIDI device name that last sent a ready request (device-gone detection). */
    const juce::String& getControllerDeviceName() const { return controllerDeviceName; }

private:
    void handleReadyRequest(uint8_t major, uint8_t minor, const juce::String& sourceDeviceName);   // message thread only
    void send(const juce::MidiMessage& message) const;

    std::function<void(const juce::MidiMessage&)> sendCallback;
    std::function<HostState()> hostStateProvider;

    bool connected = false;
    bool incompatible = false;
    bool mirrorSent = false;                                   // a SNAPSHOT was delivered
    juce::String controllerDeviceName;

    // Outlives the bridge so a queued handleReadyRequest lambda can check it safely
    // after the bridge itself is gone (same pattern as PluginChain::loaderAliveFlag).
    std::shared_ptr<std::atomic<bool>> aliveFlag { std::make_shared<std::atomic<bool>>(true) };

    std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons> currentMirror {};
};
