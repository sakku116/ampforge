#include "ControllerBridge.h"

#include "HostDebug.h"

void ControllerBridge::setSendCallback(std::function<void(const juce::MidiMessage&)> send)
{
    sendCallback = std::move(send);
}

void ControllerBridge::setHostStateProvider(std::function<HostState()> provider)
{
    hostStateProvider = std::move(provider);
}

ControllerBridge::~ControllerBridge()
{
    // A ready request may still be queued on the message thread; without this, its
    // lambda would dereference a destroyed bridge (UAF) during shutdown.
    *aliveFlag = false;
}

bool ControllerBridge::handleIncomingMidi(const juce::MidiMessage& message, const juce::String& sourceDeviceName)
{
    uint8_t major = 0, minor = 0;

    if (! tf::ctrl::isReadyRequest(message, major, minor))
        return false;   // notes, CCs, other SysEx, malformed SysEx → pass through

    // The ready handler needs host-owned state (control map + chain) and owns the
    // connection/mirror state, so it always runs on the message thread: inline when
    // already there (deterministic tests), otherwise deferred via callAsync. The MIDI
    // thread never mutates bridge state directly. The lambda holds the aliveFlag so it
    // can safely check whether the bridge still exists when it finally runs.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        handleReadyRequest(major, minor, sourceDeviceName);
    else
    {
        auto alive = aliveFlag;
        juce::MessageManager::callAsync([this, alive, major, minor, sourceDeviceName]
        {
            if (alive->load())
                handleReadyRequest(major, minor, sourceDeviceName);
        });
    }

    return true;
}

void ControllerBridge::handleReadyRequest(uint8_t major, uint8_t minor, const juce::String& sourceDeviceName)
{
    controllerDeviceName = sourceDeviceName;
    connected = true;

    if (major != tf::ctrl::protocolMajor || minor != tf::ctrl::protocolMinor)
    {
        incompatible = true;
        mirrorSent = false;
        send(tf::ctrl::makeProtocolMismatch(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor));
        HostDebug::log("Controller Bridge: protocol mismatch (controller v" + juce::String(major) + "."
                       + juce::String(minor) + ", host v" + juce::String(tf::ctrl::protocolMajor) + "."
                       + juce::String(tf::ctrl::protocolMinor) + ") — no synchronization");
        return;
    }

    incompatible = false;
    mirrorSent = false;   // every ready (re)starts with a complete snapshot
    HostDebug::log("Controller Bridge: ready from " + controllerDeviceName
                   + " (v" + juce::String(major) + "." + juce::String(minor) + ")");
    refresh();
}

void ControllerBridge::notifyDisconnected()
{
    if (! connected)
        return;

    connected = false;
    incompatible = false;
    mirrorSent = false;
    HostDebug::log("Controller Bridge: disconnected (" + controllerDeviceName + ")");
}

void ControllerBridge::refresh()
{
    // While disconnected there is nothing to sync, and a version-mismatched controller
    // must not receive mirror data at all (Protocol Incompatibility, #11).
    if (! connected || incompatible)
        return;

    const HostState state = hostStateProvider ? hostStateProvider() : HostState {};

    std::array<tf::ctrl::ButtonDescriptor, tf::ctrl::numButtons> next {};
    for (int i = 0; i < tf::ctrl::numButtons; ++i)
        next[(size_t) i].index = (uint8_t) i;

    for (const auto& button : state.buttons)
    {
        const int idx = tf::ctrl::noteToButton(button.note);
        if (idx < 0)
            continue;   // not a Controller Surface note — not mirrored

        auto& desc = next[(size_t) idx];
        desc.assigned        = true;
        desc.isPreset        = button.isPreset;
        desc.active          = ! button.bypassed;       // stomp on / preset selected
        desc.bypassed        = button.bypassed;         // stomp off / preset inactive
        desc.sectionBypassed = button.sectionBypassed;
        desc.label           = tf::ctrl::canonicalLabel(button.label);
    }

    if (! mirrorSent)
    {
        send(tf::ctrl::makeSnapshot(tf::ctrl::protocolMajor, tf::ctrl::protocolMinor, next));
        currentMirror = next;
        mirrorSent = true;
        return;
    }

    for (int i = 0; i < tf::ctrl::numButtons; ++i)
    {
        if (next[(size_t) i] != currentMirror[(size_t) i])
        {
            send(tf::ctrl::makeButtonUpdate(next[(size_t) i]));
            currentMirror[(size_t) i] = next[(size_t) i];
        }
    }
}

ControllerBridge::Status ControllerBridge::getStatus() const
{
    if (! connected)
        return Status::disconnected;
    return incompatible ? Status::incompatible : Status::connected;
}

void ControllerBridge::send(const juce::MidiMessage& message) const
{
    if (sendCallback)
        sendCallback(message);
}
