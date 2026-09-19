#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <thread>

#include "KeyboardControlController.h"

namespace tf::kb
{
    /** Maps a native Windows virtual key to the JUCE key code space persisted by the
        Control Map (Ticket 17.2). Replicates JUCE's own translation
        (juce_Windowing_windows.cpp), so a mapping learned locally (JUCE KeyPress
        codes) and a physical key seen by the WH_KEYBOARD_LL hook normalize to the
        same key code.

        The 0x10000 "extended" bit is part of JUCE's Windows key code space (arrows,
        F-keys, and numeric-keypad keys carry it); it is baked into the constants this
        function returns, so those keys round-trip exactly. Printable keys use
        MapVirtualKeyW(VK, MAPVK_VK_TO_CHAR) — the same translation JUCE's local
        handling uses — which also makes letter/digit/punctuation codes consistent
        across keyboard layouts. Unmapped native keys (modifiers, media keys, OEM
        noise) return 0.

        Platform-independent signature so the adapter's translation is unit-testable
        without a real hook: tests pass the raw Windows VK_* value. */
    int windowsVkToJuceKeyCode(int vk);

    /** Current modifier key state (modCtrl/modAlt/modShift/modWin) for a hook event,
        read with GetAsyncKeyState so the decision matches what the user is
        physically holding. */
    int modifiersFromState();
}

/** Thin Windows WH_KEYBOARD_LL adapter (Ticket 17.2).

    Owns the low-level hook and the exclusive cross-process ownership primitive for
    Global Keyboard Capture, and translates native keyboard events into the normalized
    tf::kb::KeyEvent boundary consumed by KeyboardControlController. It is the only
    module that includes <windows.h> for this feature; everything upstream stays
    platform-independent.

    Threading contract (bounded, non-blocking hook):
      - The hook callback runs on a dedicated Windows thread that is neither the JUCE
        message thread nor the audio thread. It must return quickly or Windows silently
        removes the hook.
      - The callback only translates the event and feeds the controller (itself
        lock-guarded); the app defers all app work (actions, learn, status) to the
        message thread. Nothing here blocks or allocates heavily.
      - install()/uninstall() never block: uninstall() flags the hook thread to stop
        and wakes its message loop; the thread unhooks itself and detaches on its own
        (a hook thread never joins itself — the capture-escape path can disable capture
        from inside a callback). waitForHookExit() joins the thread and is called by
        the app's message-thread teardown only.

    Ownership: a process-wide named mutex is acquired on enable and released on
    disable/teardown, so only one Amp Forge instance can own capture at a time
    (US-25/26/28). A crashed owner leaves the mutex abandoned; the next instance takes
    it over. */
class KeyboardCaptureAdapter
{
public:
    KeyboardCaptureAdapter() = default;
    ~KeyboardCaptureAdapter();

    /** Sink for a normalized key event. Called on the hook thread; must return true
        when the event is consumed (suppressed from the foreground application). The
        app wires it to the controller, which defers app work to the message thread. */
    void setEventSink(std::function<bool(const tf::kb::KeyEvent&)> sink);

    /** Owns exclusive cross-process capture ownership. Called by the controller on
        enable (message thread). True once ownership is held; the app stays off on
        failure. */
    bool acquireOwnership();

    /** Releases cross-process capture ownership. Called on disable and teardown. */
    void releaseOwnership();

    /** Installs the WH_KEYBOARD_LL hook. Call after ownership is held. Returns true
        when the hook is live. Non-blocking. */
    bool install();

    /** Removes the hook. Non-blocking and safe from any thread: flags the hook thread
        to stop and wakes its message loop; the thread unhooks itself and detaches.
        Idempotent. */
    void uninstall();

    /** Joins the hook thread. Call from the message thread only, and never while the
        controller lock is held (the hook callback can be inside the controller). */
    void waitForHookExit();

    /** True while the hook is installed. */
    bool isInstalled() const;

    /** True while this adapter holds capture ownership. */
    bool ownsCapture() const { return ownsCaptureFlag.load(); }

    /** Session failure reason reported to the UI (mirrors tf::kb::FailReason). */
    int lastFailReason() const { return lastFailReasonValue.load(); }

    // Shared hook context. The hook thread holds its own shared_ptr (captured at
    // thread creation), so the context — and the hook state it owns — outlives the
    // adapter until the thread exits and detaches. Nothing ever joins from a
    // non-message thread. Exposed so the .cpp's hook trampoline (which must live in
    // the translation unit to stay free of Win32 types) can hold it.
    struct HookContext
    {
        std::atomic<bool> running { false };
        std::atomic<void*> hookHandle { nullptr };
        std::atomic<unsigned long> threadId { 0 };
        std::set<unsigned int> downKeys;
        std::set<unsigned int> suppressedKeys;
        std::function<bool(const tf::kb::KeyEvent&)> sink;
        std::thread thread;
    };

    /** Internal: hook-event handler invoked by the .cpp's Win32 hook proc on the
        hook thread. Public only so the translation-unit hook callback can reach it
        without leaking Win32 types into this header; it is not part of the public
        contract. Returns true when the event is consumed. */
    intptr_t handleHookEvent(int code, intptr_t wParam, intptr_t lParam, HookContext& ctx);

private:
    void hookThreadMain(std::shared_ptr<HookContext> self);

    std::function<bool(const tf::kb::KeyEvent&)> eventSink;
    std::shared_ptr<HookContext> hook;

    void* ownershipHandle = nullptr;
    std::atomic<bool> ownsCaptureFlag { false };
    std::atomic<int> lastFailReasonValue { tf::kb::FailReason::none };
};
