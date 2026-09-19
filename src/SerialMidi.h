#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

/** Amp Forge's classic Bluetooth (SPP) controller transport (#13).
 *
 * Windows 11 24H2+ removed the BLE MIDI driver (bthmidi.sys) and Windows MIDI Services
 * on those builds has no BLE transport yet, so the Native Controller App switched to a
 * classic SPP server: Windows pairs it and exposes the service as a COM port
 * ("Standard Serial over Bluetooth link (COMx)"). This module opens every Bluetooth COM
 * port, exchanges raw MIDI bytes over it and transparently reconnects when the phone
 * app comes back to the foreground.
 *
 * The wire contract is unchanged: the stream carries raw MIDI bytes (notes, the READY
 * SysEx) and the host replies with its Controller Snapshot / Update SysEx messages. */
class SerialMidi
{
public:
    SerialMidi() = default;
    ~SerialMidi();

    /** Opens every Bluetooth COM port and keeps reconnecting while [stop] is not called. */
    void start(std::function<void(const juce::MidiMessage&, const juce::String& portName)> onMessage,
               std::function<void(const juce::String& portName, bool connected)> onState);

    void stop();

    /** Serializes a MIDI message to raw bytes and writes it to every open port. */
    void writeToAll(const juce::MidiMessage& message);

private:
    struct Port
    {
        juce::String name;         // "COM3"
        juce::String instanceId;   // BTHENUM instance — identity across re-pairs
        void* handle = nullptr;    // HANDLE (windows.h is only included in the .cpp)
        bool open = false;
        std::thread reader;
    };

    struct PortInfo
    {
        juce::String name;
        juce::String instanceId;
    };

    static std::vector<PortInfo> enumerateBluetoothPorts();
    bool openPort(Port& port);
    void startReader(Port& port);
    void readLoop(Port& port);
    void scanLoop();
    static void writeBytes(void* handle, const uint8_t* data, int size);

    std::vector<std::unique_ptr<Port>> ports;
    std::function<void(const juce::MidiMessage&, const juce::String&)> onMessage;
    std::function<void(const juce::String&, bool)> onState;
    std::atomic<bool> running { false };
    std::thread scanner;
    std::mutex mutex;
};
