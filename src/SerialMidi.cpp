#include "SerialMidi.h"

#include "HostDebug.h"

#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>

#pragma comment(lib, "setupapi.lib")

namespace
{
    /** Incremental parser: reassembles a raw MIDI byte stream into whole messages. */
    struct MidiStreamParser
    {
        std::vector<uint8_t> sysEx;
        bool inSysEx = false;
        uint8_t runningStatus = 0;
        std::vector<uint8_t> data;
        int expected = 0;

        void push(const uint8_t* bytes, int len, std::vector<juce::MidiMessage>& out)
        {
            for (int i = 0; i < len; ++i)
            {
                const uint8_t b = bytes[i];

                if (inSysEx)
                {
                    if (b == 0xF7)
                    {
                        sysEx.push_back(b);
                        inSysEx = false;
                        if (sysEx.size() > 2)   // F0 ... F7 with at least one payload byte
                            out.push_back(juce::MidiMessage::createSysExMessage(sysEx.data() + 1,
                                                                                (int) sysEx.size() - 2));
                        sysEx.clear();
                    }
                    else
                    {
                        sysEx.push_back(b);
                    }
                    continue;
                }

                if (b == 0xF0)
                {
                    sysEx.clear();
                    sysEx.push_back(b);
                    inSysEx = true;
                    continue;
                }

                if (b >= 0xF8)       // real-time byte — ignore
                    continue;

                if (b >= 0x80)       // status byte
                {
                    runningStatus = b;
                    data.clear();
                    expected = (b >= 0xC0 && b <= 0xDF) ? 1 : 2;
                    continue;
                }

                // Data byte (with running status)
                if (runningStatus == 0)
                    continue;
                data.push_back(b);
                if ((int) data.size() == expected)
                {
                    std::vector<uint8_t> message;
                    message.reserve(1 + data.size());
                    message.push_back(runningStatus);
                    message.insert(message.end(), data.begin(), data.end());
                    out.emplace_back(message.data(), (int) message.size());
                    data.clear();
                }
            }
        }
    };

    bool isBluetoothComPort(const juce::String& friendlyName)
    {
        return friendlyName.contains("Bluetooth") && friendlyName.contains("COM");
    }

    juce::String comPortFromFriendlyName(const juce::String& friendlyName)
    {
        const int open = friendlyName.lastIndexOf("(COM");
        if (open < 0)
            return {};
        const int close = friendlyName.indexOfChar(open + 4, ')');
        if (close < 0)
            return {};
        return friendlyName.substring(open + 1, close);
    }

    /** Phantom Bluetooth serial ports carry no real device address in their instance ID. */
    bool isPhantomPort(const juce::String& instanceId)
    {
        return instanceId.contains("000000000000");
    }
}

void SerialMidi::start(std::function<void(const juce::MidiMessage&, const juce::String&)> messageCb,
                       std::function<void(const juce::String&, bool)> stateCb)
{
    onMessage = std::move(messageCb);
    onState = std::move(stateCb);
    running = true;

    for (auto& info : enumerateBluetoothPorts())
    {
        auto port = std::make_unique<Port>();
        port->name = info.name;
        port->instanceId = info.instanceId;
        // Only track ports that actually open; the scanner retries the rest.
        if (openPort(*port))
        {
            startReader(*port);
            ports.push_back(std::move(port));
        }
    }

    // Periodically re-scan: a Windows re-pair (needed after an app process restart
    // changed the RFCOMM channel) creates a NEW COM port that must be picked up.
    scanner = std::thread([this] { scanLoop(); });
}

std::vector<SerialMidi::PortInfo> SerialMidi::enumerateBluetoothPorts()
{
    std::vector<PortInfo> result;

    HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_PROFILE);
    if (devices == INVALID_HANDLE_VALUE)
        return result;

    for (DWORD index = 0;; ++index)
    {
        SP_DEVINFO_DATA info {};
        info.cbSize = sizeof(info);
        if (! SetupDiEnumDeviceInfo(devices, index, &info))
            break;

        wchar_t friendly[MAX_PATH] {};
        wchar_t instance[MAX_PATH] {};
        DWORD size = 0;
        if (SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_FRIENDLYNAME,
                                              nullptr, (PBYTE) friendly, sizeof(friendly), &size)
            && isBluetoothComPort(juce::String(friendly))
            && SetupDiGetDeviceInstanceIdW(devices, &info, instance, sizeof(instance) / sizeof(wchar_t), &size)
            && ! isPhantomPort(juce::String(instance)))
        {
            PortInfo pi;
            pi.name = comPortFromFriendlyName(friendly);
            pi.instanceId = juce::String(instance);
            if (pi.name.isNotEmpty())
                result.push_back(std::move(pi));
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

void SerialMidi::scanLoop()
{
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (! running)
            return;

        std::vector<PortInfo> fresh = enumerateBluetoothPorts();
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& info : fresh)
        {
            bool known = false;
            for (auto& port : ports)
                if (port->instanceId == info.instanceId)
                {
                    known = true;
                    break;
                }
            if (known)
                continue;

            auto port = std::make_unique<Port>();
            port->name = info.name;
            port->instanceId = info.instanceId;
            if (openPort(*port))
            {
                HostDebug::log("SerialMidi: new Bluetooth serial port " + info.name);
                startReader(*port);
                ports.push_back(std::move(port));
            }
        }
    }
}

SerialMidi::~SerialMidi()
{
    stop();
}

void SerialMidi::stop()
{
    running = false;
    if (scanner.joinable())
        scanner.join();
    {
        // Close the handles first so any reader blocked in ReadFile returns.
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& port : ports)
        {
            if (port->handle != nullptr)
            {
                CloseHandle(port->handle);
                port->handle = nullptr;
            }
            port->open = false;
        }
    }
    // Join WITHOUT holding the mutex — the reader's failure path locks it too.
    for (auto& port : ports)
        if (port->reader.joinable())
            port->reader.join();
    std::lock_guard<std::mutex> lock(mutex);
    ports.clear();
}

bool SerialMidi::openPort(Port& port)
{
    const juce::String path = "\\\\.\\" + port.name;
    HANDLE h = CreateFileW(path.toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    DCB dcb {};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(h, &dcb);

    COMMTIMEOUTS timeouts {};
    // All-zero timeouts = fully blocking: ReadFile waits until data arrives (or the
    // connection drops, when it returns an error). MAXDWORD would make ReadFile return
    // immediately with 0 bytes, which must NOT be treated as a failure.
    SetCommTimeouts(h, &timeouts);

    port.handle = h;
    port.open = true;
    return true;
}

void SerialMidi::startReader(Port& port)
{
    // One thread per port for its whole lifetime: read -> fail -> close -> retry -> read.
    // Never re-assign a joinable std::thread (operator= terminates the process).
    port.reader = std::thread([this, &port] { readLoop(port); });
}

void SerialMidi::readLoop(Port& port)
{
    MidiStreamParser parser;
    std::vector<uint8_t> buffer(1024);

    while (running)
    {
        while (running && port.open)
        {
            DWORD read = 0;
            if (! ReadFile(port.handle, buffer.data(), (DWORD) buffer.size(), &read, nullptr) || read == 0)
                break;

            std::vector<juce::MidiMessage> messages;
            parser.push(buffer.data(), (int) read, messages);
            for (auto& message : messages)
            {
                if (onMessage)
                    onMessage(message, port.name);
            }
        }

        // Port dropped (phone app backgrounded, Bluetooth off, unplugged): close and retry.
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (port.handle != nullptr)
            {
                CloseHandle(port.handle);
                port.handle = nullptr;
            }
            port.open = false;
        }
        if (onState)
            onState(port.name, false);

        if (! running)
            return;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (! running)
            return;

        if (openPort(port))
        {
            HostDebug::log("SerialMidi: reconnected " + port.name);
            if (onState)
                onState(port.name, true);
        }
    }
}

void SerialMidi::writeToAll(const juce::MidiMessage& message)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& port : ports)
    {
        if (! port->open || port->handle == nullptr)
            continue;

        if (message.isSysEx())
        {
            const uint8_t* data = message.getSysExData();
            const int size = message.getSysExDataSize();
            std::vector<uint8_t> frame;
            frame.reserve((size_t) size + 2);
            frame.push_back(0xF0);
            frame.insert(frame.end(), data, data + size);
            frame.push_back(0xF7);
            writeBytes(port->handle, frame.data(), (int) frame.size());
        }
        else
        {
            writeBytes(port->handle, message.getRawData(), message.getRawDataSize());
        }
    }
}

void SerialMidi::writeBytes(HANDLE handle, const uint8_t* data, int size)
{
    DWORD written = 0;
    WriteFile(handle, data, (DWORD) size, &written, nullptr);
}
