#pragma once

#include <JuceHeader.h>
#include <functional>

#include "ControlMap.h"

namespace tf::kb
{
    /** Normalized keyboard event fed to the public Keyboard Control boundary (#17).
        The thin Win32 adapter translates native LLKHF_* events into these; local JUCE
        KeyPress events are translated by the app. Flags stay platform-independent so
        the controller/state-machine is testable without Win32. */
    enum class EventType { keyDown, keyUp };

    /** Platform-independent modifier / injected flags. */
    enum Modifier : int
    {
        modNone    = 0,
        modCtrl    = 1 << 0,
        modAlt     = 1 << 1,
        modShift   = 1 << 2,
        modWin     = 1 << 3,
        modRepeat  = 1 << 4,   // auto-repeat of a held key (LLKHF_UP cleared + repeat count)
    };

    struct KeyEvent
    {
        EventType type = EventType::keyDown;
        int       keyCode = 0;   // JUCE key code (same space ControlMap persists)
        bool      injected = false;
        int       modifiers = 0; // bitmask of Modifier
    };

    /** Session failure reasons for the Capture-ownership / hook-install path. */
    enum FailReason : int
    {
        none = 0,
        ownershipConflict,   // another Amp Forge instance owns capture
        hookInstallFailed,   // Windows refused the low-level hook
    };
}

/** Keyboard Control Controller (Ticket 17.1): the public keyboard-control seam that
    owns capture policy between normalized keyboard events and the existing
    control-action execution flow.

    It is a pure, platform-independent state machine: the thin Win32 adapter (later
    ticket) installs/removes WH_KEYBOARD_LL and translates native events into
    tf::kb::KeyEvent; local JUCE key presses are translated by the app and passed via
    handleLocalKeyPress(). No audio-thread, plugin, dialog, or chain work happens here —
    the controller only decides consume/pass-through and emits actions/learned triggers
    through callbacks.

    Behavior contract (from #17):
      - Global Keyboard Capture is session state: starts off, never persisted, and is
        not restored across a new controller lifetime.
      - While capture is enabled, the hook is authoritative, including when Amp Forge
        has focus: one physical mapped press produces exactly one action (local JUCE
        handling must not re-execute it).
      - A mapped bare key executes once on the initial key-down; auto-repeat key-downs
        and the matching key-up are consumed without extra actions.
      - Unmapped keys pass through. Mapped keys pass through while Ctrl/Alt/Shift/Win is
        held (a mapping is one bare logical key). Injected events are ignored.
      - Ctrl+Shift+F11 (the fixed Capture Escape Shortcut) disables active capture and
        is consumed only while active; it passes through while off and never maps.
      - Learn Control takes priority over an existing action while capture is on.
      - The current active Control Map is consulted for each new press, so template
        changes and live mapping edits take effect immediately; a release belonging to an
        already captured press stays consumed. */
class KeyboardControlController
{
public:
    KeyboardControlController() = default;

    /** Releases exclusive capture ownership if held, so teardown never leaves
        cross-process interception active or a callback target dangling. */
    ~KeyboardControlController();

    // ── boundaries (fakes in tests; the app wires real ones) ─────────────────
    /** Provides the current active Control Map (owned by the active template). */
    void setControlMapProvider(std::function<const ControlMap*()> provider);

    /** Whether Learn Control is armed (the existing binding-completion flow). While
        armed, the next eligible physical bare key is consumed for learning instead of
        executing its previous action. */
    void setLearnArmedProvider(std::function<bool()> provider);

    /** Acquires exclusive cross-process capture ownership for this controller. Returns
        false when another Amp Forge instance owns capture (ownership conflict), which
        leaves capture off with a visible failure reason. The Win32 adapter implements
        this with a process-level primitive; tests inject a fake. */
    void setOwnershipProvider(std::function<bool()> provider);

    /** Releases exclusive capture ownership. Invoked on disableCapture() and from the
        destructor, so teardown never leaves cross-process ownership held. The Win32
        adapter implements it with the same process-level primitive; tests observe it
        through a fake to prove destruction releases ownership and drops the target. */
    void setOwnershipReleaseCallback(std::function<void()> callback);

    /** Emits an action to execute. Called on the caller's thread (message thread). */
    void setActionCallback(std::function<void(const ControlAction&)> callback);

    /** Emits a learned key trigger for the existing binding-completion flow. */
    void setLearnCallback(std::function<void(const ControlTrigger&)> callback);

    /** Reports capture-mode changes and activation failures for the UI. */
    void setStatusCallback(std::function<void(bool enabled, int failReason)> callback);

    // ── session mode ─────────────────────────────────────────────────────────
    /** Attempts to enter Global Keyboard Capture. Returns false (and reports a
        failReason via the status callback) when ownership or hook activation fails. */
    bool enableCapture();

    /** Leaves Global Keyboard Capture. */
    void disableCapture();

    /** True while Global Keyboard Capture is active (visible "Global Keys: ON"). */
    bool isCaptureEnabled() const { return captureEnabled; }

    // ── normalized input seam ────────────────────────────────────────────────
    /** Drives a normalized key event. Returns true when the event is consumed
        (must not reach other applications / local JUCE handling). */
    bool handleKeyEvent(const tf::kb::KeyEvent& event);

    /** Local JUCE KeyPress delivery while the Amp Forge window has focus. Returns
        true when consumed. While capture is enabled the hook is authoritative and
        tracks captured presses, so this consumes only a key the global boundary
        previously captured (one physical mapped press → one action). Unmapped keys
        and keys the hook passed through keep their normal local behavior, and once
        capture is off nothing is consumed here. */
    bool handleLocalKeyPress(const juce::KeyPress& key);

private:
    bool handleDown(const tf::kb::KeyEvent& event);
    bool handleUp(const tf::kb::KeyEvent& event);
    void releaseOwnership();

    std::function<const ControlMap*()> controlMapProvider;
    std::function<bool()> learnArmedProvider;
    std::function<bool()> ownershipProvider;
    std::function<void()> ownershipReleaseCallback;
    std::function<void(const ControlAction&)> actionCallback;
    std::function<void(const ControlTrigger&)> learnCallback;
    std::function<void(bool, int)> statusCallback;

    bool captureEnabled = false;

    // Captured physical presses: each emits once on its initial key-down, then its
    // auto-repeat key-downs and matching key-up stay consumed. Tracked per key and
    // independently of later template/mapping changes so remaining events are handled
    // consistently (a guitarist can hold more than one mapped key at a time).
    juce::Array<int> capturedKeys;
};
