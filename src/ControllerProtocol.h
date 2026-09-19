#pragma once

#include <JuceHeader.h>
#include <array>

/** Controller MIDI Protocol (#11): a private, versioned SysEx protocol carried over
    bidirectional Bluetooth MIDI between Amp Forge and the Native Controller App.

    Message shape (every data byte is 7-bit safe):
        F0  7D  10  <cmd>  <payload...>  F7
        │   │   │
        │   │   └── device id 0x10 (Amp Forge controller)
        │   └────── manufacturer id 0x7D (educational use — private protocol)
        └──────── SysEx

    Commands:
        0x01 READY    controller → host: requested version major, minor. The host
                      replies with a SNAPSHOT when compatible, or a MISMATCH otherwise.
        0x10 SNAPSHOT host → controller: host version major, minor + eight Button
                      descriptors (the complete Controller Mirror).
        0x11 UPDATE   host → controller: one Button descriptor (incremental feedback).
        0x12 MISMATCH host → controller: host version major, minor (no synchronization).

    Button descriptor:
        <index:1> <flags:1> <len:1> <label:len bytes (ASCII, 7-bit safe)>
        flags bit0 assigned, bit1 isPreset, bit2 active (stomp on / preset selected),
              bit3 bypassed (stomp off / preset inactive), bit4 sectionBypassed (muted). */
namespace tf::ctrl
{
    constexpr uint8_t manufacturerId = 0x7D;
    constexpr uint8_t deviceId       = 0x10;
    constexpr uint8_t cmdReady       = 0x01;   // controller → host
    constexpr uint8_t cmdSnapshot    = 0x10;   // host → controller
    constexpr uint8_t cmdUpdate      = 0x11;   // host → controller
    constexpr uint8_t cmdMismatch    = 0x12;   // host → controller

    constexpr uint8_t protocolMajor  = 1;
    constexpr uint8_t protocolMinor  = 0;

    // Controller Surface: eight fixed notes on the Android MIDI Channel, in reading order.
    constexpr int  noteSetStart = 60;
    constexpr int  numButtons   = 8;
    constexpr int  midiChannel  = 16;

    /** Longest button label sent over the wire (1-byte length, keeps SysEx small). */
    constexpr int  maxLabelBytes = 40;

    /** One Controller Surface button as seen by the phone. */
    struct ButtonDescriptor
    {
        uint8_t     index = 0;
        bool        assigned = false;
        bool        isPreset = false;
        bool        active = false;           // stomp on / preset selected
        bool        bypassed = false;         // stomp off / preset inactive
        bool        sectionBypassed = false;  // muted section-bypassed target
        juce::String label;
    };

    inline bool operator==(const ButtonDescriptor& a, const ButtonDescriptor& b)
    {
        return a.index == b.index
            && a.assigned == b.assigned
            && a.isPreset == b.isPreset
            && a.active == b.active
            && a.bypassed == b.bypassed
            && a.sectionBypassed == b.sectionBypassed
            && a.label == b.label;
    }

    inline bool operator!=(const ButtonDescriptor& a, const ButtonDescriptor& b) { return ! (a == b); }

    inline int buttonToNote(int buttonIndex) { return noteSetStart + buttonIndex; }
    inline int noteToButton(int note) { return (note >= noteSetStart && note < noteSetStart + numButtons) ? note - noteSetStart : -1; }

    /** Truncated ASCII form actually sent on the wire (7-bit safe, max maxLabelBytes). */
    inline juce::String canonicalLabel(const juce::String& label)
    {
        juce::String out;

        for (auto c : label)
        {
            if (out.length() >= maxLabelBytes)
                break;
            out += (juce::juce_wchar) ((c >= 0x20 && c <= 0x7E) ? c : (juce_wchar) '?');
        }

        return out;
    }

    // ── host → controller encoding ───────────────────────────────────────────

    inline void appendDescriptor(juce::MemoryBlock& block, const ButtonDescriptor& button)
    {
        block.append(&button.index, 1);

        uint8_t flags = 0;
        if (button.assigned)         flags |= 0x01;
        if (button.isPreset)         flags |= 0x02;
        if (button.active)           flags |= 0x04;
        if (button.bypassed)         flags |= 0x08;
        if (button.sectionBypassed)  flags |= 0x10;
        block.append(&flags, 1);

        const auto label = canonicalLabel(button.label);
        const uint8_t len = (uint8_t) juce::jmin((int) label.length(), maxLabelBytes);
        block.append(&len, 1);
        for (int i = 0; i < len; ++i)
        {
            const uint8_t c = (uint8_t) ((juce::juce_wchar) label[i] < 0x80 ? (juce_wchar) label[i] : (juce_wchar) '?');
            block.append(&c, 1);
        }
    }

    inline juce::MidiMessage makeSnapshot(uint8_t major, uint8_t minor,
                                          const std::array<ButtonDescriptor, numButtons>& buttons)
    {
        juce::MemoryBlock block;
        const uint8_t header[] = { manufacturerId, deviceId, cmdSnapshot, major, minor };
        block.append(header, sizeof header);
        for (const auto& b : buttons)
            appendDescriptor(block, b);
        return juce::MidiMessage::createSysExMessage(block.getData(), (int) block.getSize());
    }

    inline juce::MidiMessage makeButtonUpdate(const ButtonDescriptor& button)
    {
        juce::MemoryBlock block;
        const uint8_t header[] = { manufacturerId, deviceId, cmdUpdate };
        block.append(header, sizeof header);
        appendDescriptor(block, button);
        return juce::MidiMessage::createSysExMessage(block.getData(), (int) block.getSize());
    }

    inline juce::MidiMessage makeProtocolMismatch(uint8_t major, uint8_t minor)
    {
        const uint8_t payload[] = { manufacturerId, deviceId, cmdMismatch, major, minor };
        return juce::MidiMessage::createSysExMessage(payload, (int) sizeof payload);
    }

    // ── controller → host recognition ────────────────────────────────────────

    /** True when the message is a well-formed READY request; copies the requested
        protocol version out. Everything else (notes, CCs, other SysEx, malformed
        SysEx) returns false so the message passes through to the control map. */
    inline bool isReadyRequest(const juce::MidiMessage& message, uint8_t& majorOut, uint8_t& minorOut)
    {
        if (! message.isSysEx())
            return false;

        const auto* data = message.getSysExData();      // skips the leading F0
        const int size = message.getSysExDataSize();    // excludes F0 and F7

        if (size != 5)
            return false;
        if (data[0] != manufacturerId || data[1] != deviceId || data[2] != cmdReady)
            return false;

        majorOut = data[3];
        minorOut = data[4];
        return true;
    }

    inline bool parseDescriptor(const uint8_t* data, int& pos, int size, ButtonDescriptor& out)
    {
        if (pos + 3 > size)
            return false;

        out.index = data[pos++];
        const uint8_t flags = data[pos++];
        const int len = data[pos++];

        if (len > maxLabelBytes || pos + len > size)
            return false;

        out.assigned        = (flags & 0x01) != 0;
        out.isPreset        = (flags & 0x02) != 0;
        out.active          = (flags & 0x04) != 0;
        out.bypassed        = (flags & 0x08) != 0;
        out.sectionBypassed = (flags & 0x10) != 0;

        juce::String label;
        for (int i = 0; i < len; ++i)
            label += (juce::juce_wchar) data[pos++];
        out.label = label;
        return true;
    }

    // ── host → controller decoding (also the Native Controller App's reader) ──

    inline bool isSnapshot(const juce::MidiMessage& message, uint8_t& major, uint8_t& minor,
                           std::array<ButtonDescriptor, numButtons>& buttons)
    {
        if (! message.isSysEx())
            return false;

        const auto* data = message.getSysExData();
        const int size = message.getSysExDataSize();

        if (size < 5 || data[0] != manufacturerId || data[1] != deviceId || data[2] != cmdSnapshot)
            return false;

        major = data[3];
        minor = data[4];
        int pos = 5;

        for (auto& b : buttons)
            if (! parseDescriptor(data, pos, size, b))
                return false;

        return pos == size;
    }

    inline bool isButtonUpdate(const juce::MidiMessage& message, ButtonDescriptor& button)
    {
        if (! message.isSysEx())
            return false;

        const auto* data = message.getSysExData();
        const int size = message.getSysExDataSize();

        if (size < 3 || data[0] != manufacturerId || data[1] != deviceId || data[2] != cmdUpdate)
            return false;

        int pos = 3;
        return parseDescriptor(data, pos, size, button) && pos == size;
    }

    inline bool isProtocolMismatch(const juce::MidiMessage& message, uint8_t& major, uint8_t& minor)
    {
        if (! message.isSysEx())
            return false;

        const auto* data = message.getSysExData();
        const int size = message.getSysExDataSize();

        if (size != 5 || data[0] != manufacturerId || data[1] != deviceId || data[2] != cmdMismatch)
            return false;

        major = data[3];
        minor = data[4];
        return true;
    }
}
