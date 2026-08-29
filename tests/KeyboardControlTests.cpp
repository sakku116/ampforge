// Deterministic host-side tests for the public Keyboard Control event boundary (#17,
// Ticket 17.1). Exercises KeyboardControlController's normalized key-down/repeat/up
// input plus fake action, learn, ownership, and status callbacks — no Win32 hook, no
// device, no timing dependency, no audio thread. Tiny harness (no framework):
// CHECK macros, exit code 0 = pass. Mirrors tests/ControllerBridgeTests.cpp.

#include <cstdio>
#include <memory>

#include <JuceHeader.h>

#include "../src/KeyboardControlController.h"
#include "../src/ControlMap.h"

namespace
{
    int checks = 0;
    int failures = 0;

    #define CHECK(cond)                                                        \
        do {                                                                   \
            ++checks;                                                          \
            if (! (cond)) {                                                    \
                ++failures;                                                    \
                std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            }                                                                  \
        } while (false)

    // ── normalized key event helpers ─────────────────────────────────────────
    using namespace tf::kb;

    KeyEvent down(int keyCode)          { return { EventType::keyDown,  keyCode, false, {} }; }
    KeyEvent repeatDown(int keyCode)    { return { EventType::keyDown,  keyCode, false, { Modifier::modRepeat } }; }
    KeyEvent up(int keyCode)            { return { EventType::keyUp,    keyCode, false, {} }; }
    KeyEvent injectedDown(int keyCode)  { return { EventType::keyDown,  keyCode, true,  {} }; }
    KeyEvent modDown(int keyCode, int modifiers)
    {
        return { EventType::keyDown, keyCode, false, modifiers };
    }

    // ── fake boundaries ──────────────────────────────────────────────────────
    struct FakeActions
    {
        int count = 0;
        juce::Array<ControlAction> actions;
        juce::Array<int> keyCodes;

        void clear() { count = 0; actions.clear(); keyCodes.clear(); }
    };

    struct FakeLearn
    {
        int count = 0;
        juce::Array<ControlTrigger> triggers;

        void clear() { count = 0; triggers.clear(); }
    };

    struct FakeStatus
    {
        int modeChanges = 0;
        int lastFailedReason = -1;

        void clear() { modeChanges = 0; lastFailedReason = -1; }
    };

    juce::KeyPress keyPressFor(int keyCode)
    {
        return { keyCode, juce::ModifierKeys::noModifiers, 0 };
    }
}

int main()
{
    // The thread that first creates the MessageManager becomes the message thread.
    juce::MessageManager::getInstance();

    // ── Slice 1: session state; enable/disable; local pass-through; no restore ──
    {
        FakeActions actions;
        FakeStatus status;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger&) {});
        controller.setStatusCallback([&](bool enabled, int reason)
        {
            ++status.modeChanges;
            if (! enabled && reason != 0)
                status.lastFailedReason = reason;
        });

        // starts off; local (focused) delivery passes through with no action
        CHECK(! controller.isCaptureEnabled());
        CHECK(! controller.handleLocalKeyPress(keyPressFor(juce::KeyPress::F5Key)));
        CHECK(actions.actions.isEmpty());

        // enable -> visible state on
        CHECK(controller.enableCapture());
        CHECK(controller.isCaptureEnabled());

        // disable -> visible state off, pass-through restored
        controller.disableCapture();
        CHECK(! controller.isCaptureEnabled());
        CHECK(! controller.handleLocalKeyPress(keyPressFor(juce::KeyPress::F5Key)));
        CHECK(actions.actions.isEmpty());
        CHECK(status.modeChanges >= 2);
    }

    // ── Slice 2: mapped key fires once per physical press; repeat/up consumed ──
    {
        FakeActions actions;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger&) {});
        controller.setStatusCallback([&](bool, int) {});

        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::toggleBypass, 0 } });
        controller.setControlMapProvider([&] { return map; });

        CHECK(controller.enableCapture());
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
        CHECK(actions.actions.getFirst().type == ControlAction::Type::toggleBypass);

        // auto-repeat key-down and matching key-up: consumed, no extra action
        CHECK(controller.handleKeyEvent(repeatDown(juce::KeyPress::F5Key)));
        CHECK(controller.handleKeyEvent(up(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
    }

    // ── Slice 3: unmapped keys, modifiers, capture escape, injected events ────
    {
        FakeActions actions;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger&) {});
        controller.setStatusCallback([&](bool, int) {});

        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::toggleBypass, 0 } });
        controller.setControlMapProvider([&] { return map; });

        CHECK(controller.enableCapture());

        // unmapped key passes through without an action
        CHECK(! controller.handleKeyEvent(down(juce::KeyPress::F6Key)));
        CHECK(actions.actions.isEmpty());

        // mapped key with Ctrl/Alt/Shift/Win held passes through without an action
        CHECK(! controller.handleKeyEvent(modDown(juce::KeyPress::F5Key, tf::kb::modCtrl)));
        CHECK(! controller.handleKeyEvent(modDown(juce::KeyPress::F5Key, tf::kb::modAlt)));
        CHECK(! controller.handleKeyEvent(modDown(juce::KeyPress::F5Key, tf::kb::modShift)));
        CHECK(! controller.handleKeyEvent(modDown(juce::KeyPress::F5Key, tf::kb::modWin)));
        CHECK(actions.actions.isEmpty());

        // injected events are ignored: never consumed, no action
        CHECK(! controller.handleKeyEvent(injectedDown(juce::KeyPress::F5Key)));
        CHECK(actions.actions.isEmpty());

        // Ctrl+Shift+F11 disables active capture, is consumed while active, never maps
        CHECK(controller.isCaptureEnabled());
        CHECK(controller.handleKeyEvent(modDown(juce::KeyPress::F11Key, tf::kb::modCtrl | tf::kb::modShift)));
        CHECK(! controller.isCaptureEnabled());
        CHECK(actions.actions.isEmpty());

        // while off the escape chord passes through and stays unassigned
        CHECK(! controller.handleKeyEvent(modDown(juce::KeyPress::F11Key, tf::kb::modCtrl | tf::kb::modShift)));
        CHECK(! controller.isCaptureEnabled());
    }

    // ── Slice 4: live Control Map changes take effect for the next press ─────
    {
        FakeActions actions;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger&) {});
        controller.setStatusCallback([&](bool, int) {});

        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::nextTemplate, 0 } });
        controller.setControlMapProvider([&] { return map; });

        CHECK(controller.enableCapture());
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
        CHECK(actions.actions.getFirst().type == ControlAction::Type::nextTemplate);

        // a release belonging to the captured press stays consumed even after the
        // mapping changed
        map.clear();
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F6Key },
                         { ControlAction::Type::prevTemplate, 0 } });
        CHECK(controller.handleKeyEvent(up(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);

        // the next press uses the current Control Map
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F6Key)));
        CHECK(actions.actions.size() == 2);
        CHECK(actions.actions.getLast().type == ControlAction::Type::prevTemplate);
    }

    // ── Slice 5: Learn Control precedence + focused-window single action ────
    {
        FakeActions actions;
        FakeLearn learn;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger& t) { learn.triggers.add(t); });
        controller.setStatusCallback([&](bool, int) {});

        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::toggleBypass, 0 } });
        controller.setControlMapProvider([&] { return map; });

        CHECK(controller.enableCapture());

        // learn armed: the key already has an action; learning emits the trigger and
        // consumes the press without executing the old action
        bool learnArmed = true;
        controller.setLearnArmedProvider([&] { return learnArmed; });
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(learn.triggers.size() == 1);
        CHECK(learn.triggers.getFirst().type == ControlTrigger::Type::key);
        CHECK(learn.triggers.getFirst().number == juce::KeyPress::F5Key);
        CHECK(actions.actions.isEmpty());   // old action NOT executed

        // repeats and the key-up of the learned press stay consumed
        CHECK(controller.handleKeyEvent(repeatDown(juce::KeyPress::F5Key)));
        CHECK(controller.handleKeyEvent(up(juce::KeyPress::F5Key)));
        CHECK(learn.triggers.size() == 1);
        CHECK(actions.actions.isEmpty());

        // learn disarmed: the same key now executes its action normally
        learnArmed = false;
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
        CHECK(learn.triggers.size() == 1);
        controller.handleKeyEvent(up(juce::KeyPress::F5Key));

        // focused-window delivery: the hook is authoritative while capture is on.
        // A bare mapped press the hook captured is consumed locally so it cannot
        // execute a second action; unmapped keys and passed-through keys keep
        // their normal local behavior (typing, dialog use).
        actions.clear();
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
        CHECK(controller.handleLocalKeyPress(keyPressFor(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);   // no duplicate action

        // unmapped local key passes through while capture is on
        CHECK(! controller.handleLocalKeyPress(keyPressFor(juce::KeyPress::F6Key)));
        CHECK(actions.actions.size() == 1);

        // release: the captured key-up is consumed and the key stops being tracked,
        // so a later key the hook passed through is not consumed locally
        CHECK(controller.handleKeyEvent(up(juce::KeyPress::F5Key)));
        CHECK(! controller.handleLocalKeyPress(keyPressFor(juce::KeyPress::F5Key)));

        // after disable, local delivery passes through to existing JUCE behavior
        controller.disableCapture();
        CHECK(! controller.handleLocalKeyPress(keyPressFor(juce::KeyPress::F5Key)));
    }

    // ── Slice 6: activation failure, ownership contention, teardown ──────────
    {
        FakeActions actions;
        FakeLearn learn;
        FakeStatus status;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger&) {});
        controller.setStatusCallback([&](bool enabled, int reason)
        {
            ++status.modeChanges;
            if (! enabled && reason != 0)
                status.lastFailedReason = reason;
        });

        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::toggleBypass, 0 } });
        controller.setControlMapProvider([&] { return map; });

        // activation failure: request is refused, state stays off, reason reported,
        // no actions emitted
        bool ownershipAvailable = false;
        controller.setOwnershipProvider([&] { return ownershipAvailable; });
        CHECK(! controller.enableCapture());
        CHECK(! controller.isCaptureEnabled());
        CHECK(status.lastFailedReason == tf::kb::FailReason::ownershipConflict);
        CHECK(actions.actions.isEmpty());

        // ownership released: a later activation succeeds
        ownershipAvailable = true;
        CHECK(controller.enableCapture());
        CHECK(controller.isCaptureEnabled());
        status.clear();

        // a competing enable while already owned stays on (no new failure)
        CHECK(controller.enableCapture());
        CHECK(controller.isCaptureEnabled());
        CHECK(status.modeChanges == 0);

        controller.disableCapture();

        // teardown: destruction releases ownership and leaves no active callback
        // target. The stateful fake ownership records every release, so we can
        // observe the release itself (not just trust the comment).
        int owned = 0;
        int released = 0;
        {
            KeyboardControlController transient;
            transient.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
            transient.setLearnCallback([&](const ControlTrigger&) {});
            transient.setStatusCallback([&](bool, int) {});
            transient.setControlMapProvider([&] { return map; });
            transient.setOwnershipProvider([&]
            {
                if (owned > released)   // ownership held until released
                    return false;
                ++owned;
                return true;
            });
            transient.setOwnershipReleaseCallback([&] { ++released; });

            CHECK(transient.enableCapture());
            CHECK(transient.isCaptureEnabled());
            CHECK(owned == 1 && released == 0);   // held

            // explicit disable releases ownership synchronously
            transient.disableCapture();
            CHECK(released == 1);
            CHECK(! transient.isCaptureEnabled());

            // re-enable acquires again; the release callback stays wired to the
            // controller's lifetime
            CHECK(transient.enableCapture());
            CHECK(owned == 2 && released == 1);
            CHECK(transient.isCaptureEnabled());
            // destroyed here: destructor must release ownership and drop the
            // callback target so no dangling release/action can fire later
        }

        // observable post-destruction: ownership was released by the destructor, the
        // stateful fake can re-acquire, and no action was emitted after teardown
        CHECK(released == 2);   // set above via the lambda captured by reference
        CHECK(actions.actions.isEmpty());   // teardown emitted nothing

        // the original controller can re-acquire ownership after teardown and works
        ownershipAvailable = true;
        CHECK(controller.enableCapture());
        CHECK(controller.isCaptureEnabled());
        CHECK(! controller.handleKeyEvent(down(juce::KeyPress::F6Key)));   // unmapped: pass
        controller.handleKeyEvent(up(juce::KeyPress::F6Key));
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);   // mapped press works after teardown
        controller.handleKeyEvent(up(juce::KeyPress::F5Key));
        controller.disableCapture();
    }

    // ── Slice 7: Control Map key matching + serialization round-trip ────────
    {
        // existing key trigger numbers round-trip unchanged and remain usable
        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::toggleBypass, 0 } });
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::numberPad0 },
                         { ControlAction::Type::activatePresetSlot, 2 } });

        const auto tree = map.toValueTree();
        ControlMap restored;
        restored.fromValueTree(tree);

        CHECK(restored.getNumBindings() == 2);
        CHECK(restored.getBinding(0).trigger.type == ControlTrigger::Type::key);
        CHECK(restored.getBinding(0).trigger.number == juce::KeyPress::F5Key);
        CHECK(restored.getBinding(0).action.type == ControlAction::Type::toggleBypass);
        CHECK(restored.getBinding(1).trigger.number == juce::KeyPress::numberPad0);
        CHECK(restored.getBinding(1).action.type == ControlAction::Type::activatePresetSlot);
        CHECK(restored.getBinding(1).action.index == 2);

        // the restored map matches through the public boundary
        FakeActions actions;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger&) {});
        controller.setStatusCallback([&](bool, int) {});
        controller.setControlMapProvider([&] { return restored; });

        CHECK(controller.enableCapture());
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
        CHECK(actions.actions.getFirst().type == ControlAction::Type::toggleBypass);
        controller.handleKeyEvent(up(juce::KeyPress::F5Key));

        CHECK(controller.handleKeyEvent(down(juce::KeyPress::numberPad0)));
        CHECK(actions.actions.size() == 2);
        CHECK(actions.actions.getLast().type == ControlAction::Type::activatePresetSlot);
        CHECK(actions.actions.getLast().index == 2);
        controller.handleKeyEvent(up(juce::KeyPress::numberPad0));
        controller.disableCapture();
    }

    // ── Slice 8: edge cases ──────────────────────────────────────────────────
    {
        FakeActions actions;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger&) {});
        controller.setStatusCallback([&](bool, int) {});

        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::toggleBypass, 0 } });
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F6Key },
                         { ControlAction::Type::toggleBypass, 1 } });
        controller.setControlMapProvider([&] { return map; });

        CHECK(controller.enableCapture());

        // a key-up with no captured press passes through
        CHECK(! controller.handleKeyEvent(up(juce::KeyPress::F5Key)));

        // a second mapped key pressed while the first is held: emits once, both
        // releases stay consumed
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F6Key)));
        CHECK(actions.actions.size() == 2);
        CHECK(controller.handleKeyEvent(up(juce::KeyPress::F5Key)));
        CHECK(controller.handleKeyEvent(up(juce::KeyPress::F6Key)));
        CHECK(actions.actions.size() == 2);

        // injected key-up is ignored
        CHECK(! controller.handleKeyEvent({ EventType::keyUp, juce::KeyPress::F5Key, true, {} }));

        controller.disableCapture();

        // disabled: no consumption, no actions, key-up passes through
        CHECK(! controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(! controller.handleKeyEvent(up(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 2);
    }

    // ── Slice 9: hook activation failure leaves capture off and ownership released ──
    {
        FakeStatus status;
        KeyboardControlController controller;
        controller.setStatusCallback([&](bool enabled, int reason)
        {
            ++status.modeChanges;
            if (! enabled && reason != 0)
                status.lastFailedReason = reason;
        });

        bool ownershipAcquired = false;
        bool hookInstallOk = false;
        int hookUninstalls = 0;
        int ownershipReleases = 0;
        controller.setOwnershipProvider([&] { ownershipAcquired = true; return true; });
        controller.setOwnershipReleaseCallback([&] { ++ownershipReleases; });
        controller.setHookInstallProvider([&] { return hookInstallOk; });
        controller.setHookUninstallCallback([&] { ++hookUninstalls; });

        // hook install failure: capture stays off, ownership released, reason reported
        CHECK(! controller.enableCapture());
        CHECK(! controller.isCaptureEnabled());
        CHECK(status.lastFailedReason == tf::kb::FailReason::hookInstallFailed);
        CHECK(ownershipReleases == 1);   // the just-acquired ownership was released

        // hook install succeeds: capture on
        hookInstallOk = true;
        CHECK(controller.enableCapture());
        CHECK(controller.isCaptureEnabled());
        CHECK(ownershipAcquired);

        // disable removes the hook and releases ownership
        controller.disableCapture();
        CHECK(hookUninstalls == 1);
        CHECK(ownershipReleases == 2);
        CHECK(! controller.isCaptureEnabled());
    }

    // ── Slice 10: S-2 escape chord is not a captured mapped press; S-3 uncaptured
    // repeats never re-enter learn/map matching ───────────────────────────────
    {
        FakeActions actions;
        FakeLearn learn;
        KeyboardControlController controller;
        controller.setActionCallback([&](const ControlAction& a) { actions.actions.add(a); });
        controller.setLearnCallback([&](const ControlTrigger& t) { learn.triggers.add(t); });
        controller.setStatusCallback([&](bool, int) {});

        ControlMap map;
        map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F5Key },
                         { ControlAction::Type::toggleBypass, 0 } });
        controller.setControlMapProvider([&] { return map; });

        CHECK(controller.enableCapture());

        // S-2: the capture-escape chord (Ctrl+Shift+F11) disables capture and is
        // consumed, but is NOT recorded as a captured mapped press. After it, a
        // local JUCE delivery of the same F11 key is not consumed (the chord's
        // key-up/repeat window is not locally reserved), and a bare F11 is not
        // treated as an ongoing captured press.
        CHECK(controller.isCaptureEnabled());
        CHECK(controller.handleKeyEvent(modDown(juce::KeyPress::F11Key, tf::kb::modCtrl | tf::kb::modShift)));
        CHECK(! controller.isCaptureEnabled());
        CHECK(actions.actions.isEmpty());
        CHECK(learn.triggers.isEmpty());

        // Capture is now off, so local F11 passes through (nothing consumed).
        CHECK(! controller.handleLocalKeyPress(keyPressFor(juce::KeyPress::F11Key)));
        CHECK(actions.actions.isEmpty());

        // Re-enable and press the escape chord again: still not captured, and the
        // chord's matching key-up passes through (it was never recorded).
        CHECK(controller.enableCapture());
        CHECK(controller.handleKeyEvent(modDown(juce::KeyPress::F11Key, tf::kb::modCtrl | tf::kb::modShift)));
        CHECK(! controller.isCaptureEnabled());
        CHECK(! controller.handleKeyEvent(up(juce::KeyPress::F11Key)));
        CHECK(actions.actions.isEmpty());

        // S-3: an uncaptured repeat never re-enters learn or map matching. Enable
        // capture, then deliver a repeat for a key whose initial press was NOT
        // captured (e.g. the hook passed it through because it was unmapped at the
        // time). While learn is armed, the repeat must NOT emit a second learn
        // trigger; with learn disarmed it must NOT execute the mapped action.
        CHECK(controller.enableCapture());

        bool learnArmed = true;
        controller.setLearnArmedProvider([&] { return learnArmed; });

        // Uncaptured repeat with learn armed: passes through, no learn trigger.
        CHECK(! controller.handleKeyEvent(repeatDown(juce::KeyPress::F6Key)));
        CHECK(learn.triggers.isEmpty());
        CHECK(actions.actions.isEmpty());

        // Uncaptured repeat with learn disarmed and the key NOW mapped: passes
        // through, no action (the repeat must not execute a fresh mapping).
        learnArmed = false;
        CHECK(! controller.handleKeyEvent(repeatDown(juce::KeyPress::F5Key)));
        CHECK(learn.triggers.isEmpty());
        CHECK(actions.actions.isEmpty());

        // Control: a genuine captured press still works (repeat of a captured key
        // stays consumed, matching key-up consumed), so the new pass-through branch
        // did not disturb the captured-press path.
        CHECK(controller.handleKeyEvent(down(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
        CHECK(controller.handleKeyEvent(repeatDown(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);
        CHECK(controller.handleKeyEvent(up(juce::KeyPress::F5Key)));
        CHECK(actions.actions.size() == 1);

        controller.disableCapture();
    }

    // ── Slice 11: Ticket 17.3 UI-toggle contract — status callback is the visible
    // mirror: enable → ON reported; disable → OFF; failed ownership → OFF + short
    // reason; failed hook → OFF + short reason; session-only means a fresh controller
    // starts OFF and never reports a persisted state. ───────────────────────────
    {
        struct UiMirror
        {
            bool enabled = false;
            int lastFailReason = tf::kb::FailReason::none;
            int changes = 0;

            void clear() { enabled = false; lastFailReason = tf::kb::FailReason::none; changes = 0; }
        } ui;

        bool ownershipAvailable = true;
        bool hookInstallOk = true;
        int ownershipReleases = 0;
        int hookUninstalls = 0;

        KeyboardControlController controller;
        controller.setStatusCallback([&](bool enabled, int failReason)
        {
            ++ui.changes;
            ui.enabled = enabled;
            ui.lastFailReason = failReason;
        });
        controller.setOwnershipProvider([&] { return ownershipAvailable; });
        controller.setOwnershipReleaseCallback([&] { ++ownershipReleases; });
        controller.setHookInstallProvider([&] { return hookInstallOk; });
        controller.setHookUninstallCallback([&] { ++hookUninstalls; });

        // Session-only initialization: a new controller is OFF and reports nothing.
        CHECK(! controller.isCaptureEnabled());
        CHECK(ui.changes == 0);

        // Enable: the UI mirror flips to ON with no failure reason.
        CHECK(controller.enableCapture());
        CHECK(ui.enabled);
        CHECK(ui.lastFailReason == tf::kb::FailReason::none);
        CHECK(controller.isCaptureEnabled());

        // Disable: mirror flips to OFF, hook removed and ownership released.
        controller.disableCapture();
        CHECK(! ui.enabled);
        CHECK(hookUninstalls == 1);
        CHECK(ownershipReleases == 1);

        // Ownership conflict: stays OFF, reason reported, no hook installed.
        ui.clear();
        ownershipAvailable = false;
        CHECK(! controller.enableCapture());
        CHECK(! controller.isCaptureEnabled());
        CHECK(! ui.enabled);
        CHECK(ui.lastFailReason == tf::kb::FailReason::ownershipConflict);
        CHECK(hookUninstalls == 1);   // no hook install was even attempted

        // Hook-install failure: ownership was acquired then released, reason reported.
        ui.clear();
        ownershipAvailable = true;
        hookInstallOk = false;
        CHECK(! controller.enableCapture());
        CHECK(! controller.isCaptureEnabled());
        CHECK(! ui.enabled);
        CHECK(ui.lastFailReason == tf::kb::FailReason::hookInstallFailed);
        CHECK(ownershipReleases == 2);   // just-acquired ownership released again

        // Recovery: a later enable works and reports ON.
        ui.clear();
        hookInstallOk = true;
        CHECK(controller.enableCapture());
        CHECK(ui.enabled);
        CHECK(controller.isCaptureEnabled());
        controller.disableCapture();
        CHECK(! ui.enabled);
    }

    std::printf("%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
