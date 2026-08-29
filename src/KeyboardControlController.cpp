#include "KeyboardControlController.h"

namespace
{
    // The fixed Capture Escape Shortcut: Ctrl+Shift+F11. Key codes are the JUCE key
    // space (the thin Win32 adapter normalizes native events into it), so the chord
    // is defined directly from the JUCE constant rather than a magic number. The JUCE
    // constants are not constexpr on Windows (defined in a .cpp), so use the runtime
    // values here.
    const int captureEscapeKeyCode = juce::KeyPress::F11Key;
    const int captureEscapeMods = tf::kb::modCtrl | tf::kb::modShift;

    bool hasModifier(int modifiers)
    {
        return (modifiers & (tf::kb::modCtrl | tf::kb::modAlt | tf::kb::modShift | tf::kb::modWin)) != 0;
    }

    bool isCaptureEscape(int keyCode, int modifiers)
    {
        return keyCode == captureEscapeKeyCode
            && (modifiers & (tf::kb::modCtrl | tf::kb::modAlt | tf::kb::modShift | tf::kb::modWin)) == captureEscapeMods;
    }
}

void KeyboardControlController::setControlMapProvider(std::function<const ControlMap*()> provider)
{
    controlMapProvider = std::move(provider);
}

void KeyboardControlController::setLearnArmedProvider(std::function<bool()> provider)
{
    learnArmedProvider = std::move(provider);
}

void KeyboardControlController::setOwnershipProvider(std::function<bool()> provider)
{
    ownershipProvider = std::move(provider);
}

void KeyboardControlController::setOwnershipReleaseCallback(std::function<void()> callback)
{
    ownershipReleaseCallback = std::move(callback);
}

void KeyboardControlController::setActionCallback(std::function<void(const ControlAction&)> callback)
{
    actionCallback = std::move(callback);
}

void KeyboardControlController::setLearnCallback(std::function<void(const ControlTrigger&)> callback)
{
    learnCallback = std::move(callback);
}

void KeyboardControlController::setStatusCallback(std::function<void(bool, int)> callback)
{
    statusCallback = std::move(callback);
}

KeyboardControlController::~KeyboardControlController()
{
    // Destructor releases ownership even if capture was left enabled: closing the
    // capture-owning instance must restore normal keyboard behavior (US-28).
    releaseOwnership();
}

bool KeyboardControlController::enableCapture()
{
    if (captureEnabled)
        return true;   // already owned; no new failure to report

    // Exclusive cross-process ownership is checked before entering capture. When
    // another instance owns it, the request fails visibly: state stays off, the
    // status callback carries the reason, and no action can be emitted.
    if (ownershipProvider && ! ownershipProvider())
    {
        if (statusCallback)
            statusCallback(false, tf::kb::FailReason::ownershipConflict);

        return false;
    }

    // Hook installation is owned by the Win32 adapter (later ticket); this foundation
    // reports success so the visible state can be tested. Hook-install failures surface
    // through the same status callback once the adapter exists.
    captureEnabled = true;

    if (statusCallback)
        statusCallback(true, tf::kb::FailReason::none);

    return true;
}

void KeyboardControlController::disableCapture()
{
    if (! captureEnabled)
        return;

    captureEnabled = false;
    capturedKeys.clear();
    releaseOwnership();

    if (statusCallback)
        statusCallback(false, tf::kb::FailReason::none);
}

bool KeyboardControlController::handleKeyEvent(const tf::kb::KeyEvent& event)
{
    if (event.injected)
        return false;   // software-generated input is ignored, never consumed

    if (event.type == tf::kb::EventType::keyDown)
        return handleDown(event);

    return handleUp(event);
}

bool KeyboardControlController::handleDown(const tf::kb::KeyEvent& event)
{
    // The Capture Escape Shortcut is evaluated before ordinary mapping, and only
    // while capture is active: consume the chord and disable capture. While off it
    // is not reserved and never maps.
    if (isCaptureEscape(event.keyCode, event.modifiers))
    {
        if (captureEnabled)
        {
            disableCapture();
            return true;   // consumed only while active
        }

        return false;
    }

    if (! captureEnabled)
        return false;   // Local Keyboard Control: nothing consumed here

    // A repeat of an already-captured press stays consumed without another action.
    if ((event.modifiers & tf::kb::modRepeat) != 0 && capturedKeys.contains(event.keyCode))
        return true;

    // Mapped bare keys pass through while Ctrl/Alt/Shift/Win is held; modifiers are
    // never assignable as standalone controls.
    if (hasModifier(event.modifiers))
        return false;

    // Learn Control takes priority over an existing action while capture is on: the
    // next eligible physical bare key is consumed for the binding-completion flow.
    const bool learnArmed = learnArmedProvider ? learnArmedProvider() : false;

    if (learnArmed)
    {
        capturedKeys.addIfNotAlreadyThere(event.keyCode);   // repeats and the key-up stay consumed

        if (learnCallback)
            learnCallback({ ControlTrigger::Type::key, 0, event.keyCode });

        return true;
    }

    const ControlMap* map = controlMapProvider ? controlMapProvider() : nullptr;
    const ControlAction action = map ? map->matchKey(event.keyCode) : ControlAction {};

    if (! action.isValid())
        return false;   // unmapped key passes through

    capturedKeys.addIfNotAlreadyThere(event.keyCode);

    if (actionCallback)
        actionCallback(action);

    return true;   // consumed: must not reach the foreground application
}

bool KeyboardControlController::handleUp(const tf::kb::KeyEvent& event)
{
    // A key-up belonging to a captured press stays consumed even if the template or
    // mapping changed since the initial key-down.
    if (capturedKeys.contains(event.keyCode))
    {
        capturedKeys.removeFirstMatchingValue(event.keyCode);
        return true;
    }

    return false;
}

bool KeyboardControlController::handleLocalKeyPress(const juce::KeyPress& key)
{
    // The hook is authoritative while capture is on: it emits the action for the
    // initial key-down and records the press, so a local JUCE delivery of the same
    // mapped key must be consumed rather than executed again. Unmapped keys and keys
    // the hook passed through (e.g. modified chords, per US-4) keep their normal
    // local behavior, and once capture is off nothing is consumed here. Local
    // Keyboard Control semantics are otherwise unchanged.
    if (! captureEnabled)
        return false;

    return capturedKeys.contains(key.getKeyCode());
}

void KeyboardControlController::releaseOwnership()
{
    if (ownershipReleaseCallback)
        ownershipReleaseCallback();
}
