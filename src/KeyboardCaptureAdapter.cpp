#include "KeyboardCaptureAdapter.h"

#include <chrono>
#include <windows.h>

namespace
{
    // A fixed name namespace for the cross-process ownership mutex. The OS reclaims
    // the object when the owning process exits (US-28), and an abandoned mutex
    // (crashed owner) is granted to the next waiter (takeover).
    const wchar_t* kOwnershipMutexName = L"Local\\AmpForge.KeyboardCapture.Ownership";
}

// WH_KEYBOARD_LL does not forward SetWindowsHookEx's lpParam, so the hook proc
// recovers the adapter + hook context from this thread-local (bound by the hook
// thread before installing). The adapter pointer stays valid for the hook thread's
// whole lifetime because the app joins the thread (waitForHookExit) before
// destroying the adapter. Declared in the global namespace so the adapter's friend
// declaration names it exactly; the hook proc is a static member so friendship
// grants it access to the adapter's private event handler.
struct KeyboardCaptureHookTrampoline
{
    KeyboardCaptureAdapter* adapter = nullptr;
    std::shared_ptr<KeyboardCaptureAdapter::HookContext> ctx;
};

thread_local KeyboardCaptureHookTrampoline hookTrampoline;

LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (hookTrampoline.adapter != nullptr && hookTrampoline.ctx != nullptr)
        return (LRESULT) hookTrampoline.adapter->handleHookEvent(code, (intptr_t) wParam,
                                                                 (intptr_t) lParam,
                                                                 *hookTrampoline.ctx);
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

namespace tf::kb
{
    // The 0x10000 "extended" bit is baked into the JUCE key-code constants returned
    // by windowsVkToJuceKeyCode (F-keys, arrows, numeric-keypad keys); it matches
    // JUCE's own Windows key code space (juce_Windowing_windows.cpp).

    int windowsVkToJuceKeyCode(int vk)
    {
        switch (vk)
        {
            case VK_SPACE:   return KeyPress::spaceKey;
            case VK_RETURN:  return KeyPress::returnKey;
            case VK_ESCAPE:  return KeyPress::escapeKey;
            case VK_BACK:    return KeyPress::backspaceKey;
            case VK_TAB:     return KeyPress::tabKey;

            case VK_LEFT:      return KeyPress::leftKey;
            case VK_RIGHT:     return KeyPress::rightKey;
            case VK_UP:        return KeyPress::upKey;
            case VK_DOWN:      return KeyPress::downKey;
            case VK_PRIOR:     return KeyPress::pageUpKey;
            case VK_NEXT:      return KeyPress::pageDownKey;
            case VK_HOME:      return KeyPress::homeKey;
            case VK_END:       return KeyPress::endKey;
            case VK_DELETE:    return KeyPress::deleteKey;
            case VK_INSERT:    return KeyPress::insertKey;

            case VK_F1:  return KeyPress::F1Key;  case VK_F2:  return KeyPress::F2Key;
            case VK_F3:  return KeyPress::F3Key;  case VK_F4:  return KeyPress::F4Key;
            case VK_F5:  return KeyPress::F5Key;  case VK_F6:  return KeyPress::F6Key;
            case VK_F7:  return KeyPress::F7Key;  case VK_F8:  return KeyPress::F8Key;
            case VK_F9:  return KeyPress::F9Key;  case VK_F10: return KeyPress::F10Key;
            case VK_F11: return KeyPress::F11Key; case VK_F12: return KeyPress::F12Key;
            case VK_F13: return KeyPress::F13Key; case VK_F14: return KeyPress::F14Key;
            case VK_F15: return KeyPress::F15Key; case VK_F16: return KeyPress::F16Key;
            case VK_F17: return KeyPress::F17Key; case VK_F18: return KeyPress::F18Key;
            case VK_F19: return KeyPress::F19Key; case VK_F20: return KeyPress::F20Key;
            case VK_F21: return KeyPress::F21Key; case VK_F22: return KeyPress::F22Key;
            case VK_F23: return KeyPress::F23Key; case VK_F24: return KeyPress::F24Key;

            case VK_NUMPAD0: return KeyPress::numberPad0;
            case VK_NUMPAD1: return KeyPress::numberPad1;
            case VK_NUMPAD2: return KeyPress::numberPad2;
            case VK_NUMPAD3: return KeyPress::numberPad3;
            case VK_NUMPAD4: return KeyPress::numberPad4;
            case VK_NUMPAD5: return KeyPress::numberPad5;
            case VK_NUMPAD6: return KeyPress::numberPad6;
            case VK_NUMPAD7: return KeyPress::numberPad7;
            case VK_NUMPAD8: return KeyPress::numberPad8;
            case VK_NUMPAD9: return KeyPress::numberPad9;
            case VK_ADD:       return KeyPress::numberPadAdd;
            case VK_SUBTRACT:  return KeyPress::numberPadSubtract;
            case VK_MULTIPLY:  return KeyPress::numberPadMultiply;
            case VK_DIVIDE:    return KeyPress::numberPadDivide;
            case VK_DECIMAL:   return KeyPress::numberPadDecimalPoint;
            case VK_SEPARATOR: return KeyPress::numberPadSeparator;
            default:           break;
        }

        // Printable keys: translate with the keyboard layout to an unmodified
        // character, matching JUCE's local translation (case-insensitive key code).
        // MapVirtualKeyW(VK, MAPVK_VK_TO_CHAR) returns the same codes JUCE's local
        // WM_KEYDOWN handling produces (LOWORD of the translated character), so a
        // letter mapped locally matches the same physical key seen by the hook.
        const UINT keyChar = MapVirtualKeyW((UINT) vk, MAPVK_VK_TO_CHAR);
        if (keyChar != 0)
            return (int) LOWORD(keyChar);

        return 0;   // modifiers, media keys, OEM noise: never mapped
    }

    int modifiersFromState()
    {
        int modifiers = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000)  modifiers |= tf::kb::modCtrl;
        if (GetAsyncKeyState(VK_MENU)    & 0x8000)  modifiers |= tf::kb::modAlt;
        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000)  modifiers |= tf::kb::modShift;
        if (GetAsyncKeyState(VK_LWIN)    & 0x8000
            || GetAsyncKeyState(VK_RWIN) & 0x8000)  modifiers |= tf::kb::modWin;
        return modifiers;
    }
}

KeyboardCaptureAdapter::~KeyboardCaptureAdapter()
{
    // Stop the hook (non-blocking; the hook thread owns its own lifetime and detaches
    // itself), then release cross-process ownership. The app's message-thread teardown
    // calls waitForHookExit() before destroying the adapter so no hook event can be
    // in flight.
    uninstall();
    releaseOwnership();
}

void KeyboardCaptureAdapter::setEventSink(std::function<bool(const tf::kb::KeyEvent&)> sink)
{
    eventSink = std::move(sink);
}

bool KeyboardCaptureAdapter::acquireOwnership()
{
    if (ownsCaptureFlag.load())
        return true;

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kOwnershipMutexName);
    if (mutex == nullptr)
    {
        lastFailReasonValue = tf::kb::FailReason::ownershipConflict;
        return false;
    }

    // A crashed owner leaves the mutex abandoned (WAIT_ABANDONED); Windows grants it
    // to us, so we can take over. WAIT_OBJECT_0 means we won the initial ownership.
    const DWORD result = WaitForSingleObject(mutex, 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED_0)
    {
        CloseHandle(mutex);
        lastFailReasonValue = tf::kb::FailReason::ownershipConflict;
        return false;
    }

    ownershipHandle = mutex;
    ownsCaptureFlag.store(true);
    lastFailReasonValue = tf::kb::FailReason::none;
    return true;
}

void KeyboardCaptureAdapter::releaseOwnership()
{
    if (! ownsCaptureFlag.load())
        return;

    if (ownershipHandle != nullptr)
    {
        ReleaseMutex((HANDLE) ownershipHandle);
        CloseHandle((HANDLE) ownershipHandle);
        ownershipHandle = nullptr;
    }

    ownsCaptureFlag.store(false);
    lastFailReasonValue = tf::kb::FailReason::none;
}

bool KeyboardCaptureAdapter::install()
{
    if (hook != nullptr && hook->hookHandle.load() != nullptr)
        return true;

    if (! ownsCaptureFlag.load())
    {
        lastFailReasonValue = tf::kb::FailReason::hookInstallFailed;
        return false;
    }

    // Create the hook context before starting the thread: the thread captures a
    // shared_ptr to it, so the context (and the hook it owns) outlives this adapter
    // until the thread exits and detaches itself. No join is ever performed from a
    // hook callback.
    auto ctx = std::make_shared<HookContext>();
    hook = ctx;
    hook->sink = eventSink;
    ctx->running.store(true);
    ctx->thread = std::thread([this, ctx] { hookThreadMain(ctx); });

    // Wait briefly for the thread to report whether SetWindowsHookEx succeeded.
    // A hook that fails to install must leave the app visibly off (US-27).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ctx->hookHandle.load() == nullptr && ctx->running.load()
           && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    if (ctx->hookHandle.load() == nullptr)
    {
        lastFailReasonValue = tf::kb::FailReason::hookInstallFailed;
        return false;
    }

    lastFailReasonValue = tf::kb::FailReason::none;
    return true;
}

void KeyboardCaptureAdapter::uninstall()
{
    auto ctx = hook;
    if (ctx == nullptr)
        return;

    ctx->running.store(false);

    // Wake the hook thread's message loop so it can exit promptly. The thread
    // unhooks itself and detaches before returning; nothing joins it.
    if (ctx->threadId.load() != 0)
        PostThreadMessageW((DWORD) ctx->threadId.load(), WM_NULL, 0, 0);

    hook.reset();   // drop the adapter's reference; the thread keeps its own until it exits
}

void KeyboardCaptureAdapter::waitForHookExit()
{
    auto ctx = hook;
    if (ctx == nullptr)
        return;

    ctx->running.store(false);
    if (ctx->threadId.load() != 0)
        PostThreadMessageW((DWORD) ctx->threadId.load(), WM_NULL, 0, 0);

    if (ctx->thread.joinable())
        ctx->thread.join();
}

bool KeyboardCaptureAdapter::isInstalled() const
{
    return hook != nullptr && hook->hookHandle.load() != nullptr;
}

intptr_t KeyboardCaptureAdapter::handleHookEvent(int code, intptr_t wParam, intptr_t lParam,
                                                 HookContext& ctx)
{
    if (code < 0)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (info == nullptr)
        return CallNextHookEx(nullptr, code, wParam, lParam);

    const bool isUp = (info->flags & LLKHF_UP) != 0;
    const bool injected = (info->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
    const bool extended = (info->flags & LLKHF_EXTENDED) != 0;

    if (injected)
        return CallNextHookEx(nullptr, code, wParam, lParam);   // US-12: ignore injected

    const int keyCode = tf::kb::windowsVkToJuceKeyCode((int) info->vkCode);

    // Auto-repeat detection: Windows fires repeated WM_KEYDOWN for a held key. Track
    // down keys ourselves (more reliable than LLKHF_UP combined with repeat counts).
    const bool wasDown = ctx.downKeys.find((unsigned int) info->vkCode) != ctx.downKeys.end();

    // Suppressed press: the sink consumed its initial key-down; its repeats and
    // matching key-up stay suppressed (US-10) and are still reported to the
    // controller so its captured-press tracking stays consistent.
    const bool suppressed = ctx.suppressedKeys.find((unsigned int) info->vkCode) != ctx.suppressedKeys.end();

    if (! isUp)
    {
        if (wasDown)
        {
            // Auto-repeat of a held key. If the original press was suppressed, keep
            // suppressing; otherwise pass through to the sink as a repeat so the
            // controller can consume a captured press without a second action.
            if (suppressed)
                return 1;

            tf::kb::KeyEvent event;
            event.type = tf::kb::EventType::keyDown;
            event.keyCode = keyCode;
            event.injected = false;
            event.modifiers = tf::kb::modRepeat;

            if (ctx.sink && event.keyCode != 0 && ctx.sink(event))
                return 1;

            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        // First key-down of a press.
        ctx.downKeys.insert((unsigned int) info->vkCode);

        tf::kb::KeyEvent event;
        event.type = tf::kb::EventType::keyDown;
        event.keyCode = keyCode;
        event.injected = false;
        event.modifiers = tf::kb::modifiersFromState();

        if (ctx.sink && event.keyCode != 0 && ctx.sink(event))
        {
            ctx.suppressedKeys.insert((unsigned int) info->vkCode);
            return 1;   // consumed: must not reach the foreground application (US-2)
        }

        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    // Key-up.
    ctx.downKeys.erase((unsigned int) info->vkCode);

    tf::kb::KeyEvent event;
    event.type = tf::kb::EventType::keyUp;
    event.keyCode = keyCode;
    event.injected = false;
    event.modifiers = tf::kb::modifiersFromState();

    const bool wasSuppressed = suppressed;
    ctx.suppressedKeys.erase((unsigned int) info->vkCode);

    // Report the release to the controller so its captured-press tracking drops the
    // key (a later press of the same key must be treated as new). The controller
    // returns true for a captured release, which keeps it suppressed from the
    // foreground application (US-10); a suppressed press is never let through.
    if (ctx.sink && event.keyCode != 0 && ctx.sink(event))
        return 1;

    return wasSuppressed ? 1 : CallNextHookEx(nullptr, code, wParam, lParam);
}

void KeyboardCaptureAdapter::hookThreadMain(std::shared_ptr<HookContext> ctx)
{
    ctx->threadId.store(GetCurrentThreadId());

    // Bind the trampoline for this hook thread, then install the hook. The static
    // hookProc recovers the adapter + context from the thread-local. The hook thread
    // is joined (waitForHookExit) before the adapter is destroyed, so `this` stays
    // valid for the whole hook lifetime.
    hookTrampoline.adapter = this;
    hookTrampoline.ctx = ctx;

    HHOOK installed = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc,
                                        GetModuleHandleW(nullptr), 0);
    if (installed == nullptr)
    {
        ctx->running.store(false);
        ctx->thread.detach();
        return;
    }

    ctx->hookHandle.store(installed);

    // Message pump: keeps the low-level hook alive and processes its callbacks.
    MSG msg;
    while (ctx->running.load() && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(installed);
    ctx->hookHandle.store(nullptr);
    ctx->running.store(false);
    ctx->thread.detach();
}
