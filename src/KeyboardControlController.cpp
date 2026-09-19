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

void KeyboardControlController::setControlMapProvider(std::function<ControlMap()> provider)
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
    // capture-owning instance must restore normal keyboard behavior (US-28). A full
    // disable also removes the hook, so interception never outlives the controller.
    // disableCapture() already releases ownership when capture is active; when it is
    // not, ownership was already released and there is nothing left to do.
    disableCapture();
}

bool KeyboardControlController::isCaptureEnabled() const
{
    return captureEnabled.load();
}

bool KeyboardControlController::enableCapture()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (captureEnabled.load())
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

    // Hook installation is owned by the Win32 adapter. A hook that fails to install
    // must leave capture off (US-27): release the just-acquired ownership and report
    // the failure through the status callback.
    if (hookInstallProvider && ! hookInstallProvider())
    {
        releaseOwnership();

        if (statusCallback)
            statusCallback(false, tf::kb::FailReason::hookInstallFailed);

        return false;
    }

    captureEnabled.store(true);

    if (statusCallback)
        statusCallback(true, tf::kb::FailReason::none);

    return true;
}

void KeyboardControlController::disableCapture()
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

    if (! captureEnabled.load())
        return;

    captureEnabled.store(false);
    capturedKeys.clear();

    // Remove the hook before releasing ownership: no capture window where another
    // instance could take over while our hook is still installed.
    if (hookUninstallCallback)
        hookUninstallCallback();

    releaseOwnership();

    if (statusCallback)
        statusCallback(false, tf::kb::FailReason::none);
}

bool KeyboardControlController::handleKeyEvent(const tf::kb::KeyEvent& event)
{
    std::lock_guard<std::recursive_mutex> lock(mutex);

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
    // is not reserved and never maps. The chord is never recorded as a captured
    // mapped press (S-2): drop it defensively before disabling so its key-up/repeat
    // window is not locally reserved, while suppression of the physical press stays
    // at the hook (the adapter suppresses the chord's own repeats and key-up).
    if (isCaptureEscape(event.keyCode, event.modifiers))
    {
        if (captureEnabled.load())
        {
            capturedKeys.removeFirstMatchingValue(event.keyCode);
            disableCapture();
            return true;   // consumed only while active
        }

        return false;
    }

    if (! captureEnabled.load())
        return false;   // Local Keyboard Control: nothing consumed here

    // A repeat of an already-captured press stays consumed without another action.
    // A repeat of an uncaptured press (the hook passed its initial key-down through,
    // e.g. unmapped, learn-disarmed, or a modified chord) must never re-enter learn
    // or map matching (S-3): pass it through safely instead.
    if ((event.modifiers & tf::kb::modRepeat) != 0)
        return capturedKeys.contains(event.keyCode);

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

    const ControlMap map = controlMapProvider ? controlMapProvider() : ControlMap {};
    const ControlAction action = map.matchKey(event.keyCode);

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
    std::lock_guard<std::recursive_mutex> lock(mutex);

    // The hook is authoritative while capture is on: it emits the action for the
    // initial key-down and records the press, so a local JUCE delivery of the same
    // mapped key must be consumed rather than executed again. Unmapped keys and keys
    // the hook passed through (e.g. modified chords, per US-4) keep their normal
    // local behavior, and once capture is off nothing is consumed here. Local
    // Keyboard Control semantics are otherwise unchanged.
    if (! captureEnabled.load())
        return false;

    return capturedKeys.contains(key.getKeyCode());
}

void KeyboardControlController::setHookInstallProvider(std::function<bool()> provider)
{
    hookInstallProvider = std::move(provider);
}

void KeyboardControlController::setHookUninstallCallback(std::function<void()> callback)
{
    hookUninstallCallback = std::move(callback);
}

void KeyboardControlController::releaseOwnership()
{
    if (ownershipReleaseCallback)
        ownershipReleaseCallback();
}
