// Focused tests for the Win32 key-code translation used by the Global Keyboard
// Capture adapter (Ticket 17.2). The real WH_KEYBOARD_LL installation and
// cross-application suppression are validated manually (they would interfere with the
// developer desktop); the translation itself is pure and unit-testable, so it is
// pinned here against the JUCE key-code constants Local Keyboard Control persists.

#include <cstdio>

#include <JuceHeader.h>

#include "../src/KeyboardCaptureAdapter.h"

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

    // Windows virtual-key codes (winuser.h) — kept local so this test does not need
    // windows.h on the include path beyond what the adapter already brings in.
    enum : int
    {
        VK_F1   = 0x70,
        VK_F5   = 0x74,
        VK_F11  = 0x7A,
        VK_F12  = 0x7B,
        VK_LEFT = 0x25,
        VK_UP   = 0x26,
        VK_RIGHT= 0x27,
        VK_DOWN = 0x28,
        VK_NUMPAD0 = 0x60,
        VK_NUMPAD9 = 0x69,
        VK_SPACE   = 0x20,
        VK_RETURN  = 0x0D,
        VK_ESCAPE  = 0x1B,
        VK_BACK    = 0x08,
        VK_TAB     = 0x09,
        VK_A       = 0x41,
        VK_Z       = 0x5A,
        VK_0       = 0x30,
        VK_9       = 0x39,
        VK_SHIFT   = 0x10,
        VK_CONTROL = 0x11,
        VK_MENU    = 0x12,
        VK_LWIN    = 0x5B,
    };
}

int main()
{
    // Function keys normalize to the JUCE F-key constants (with the extended bit).
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_F1)  == juce::KeyPress::F1Key);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_F5)  == juce::KeyPress::F5Key);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_F11) == juce::KeyPress::F11Key);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_F12) == juce::KeyPress::F12Key);

    // Navigation keys normalize to the JUCE constants (extended).
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_LEFT)  == juce::KeyPress::leftKey);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_RIGHT) == juce::KeyPress::rightKey);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_UP)    == juce::KeyPress::upKey);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_DOWN)  == juce::KeyPress::downKey);

    // Numeric-keypad keys normalize to the JUCE numberPad constants.
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_NUMPAD0) == juce::KeyPress::numberPad0);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_NUMPAD9) == juce::KeyPress::numberPad9);

    // Base keys.
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_SPACE)  == juce::KeyPress::spaceKey);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_RETURN) == juce::KeyPress::returnKey);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_ESCAPE) == juce::KeyPress::escapeKey);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_BACK)   == juce::KeyPress::backspaceKey);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_TAB)    == juce::KeyPress::tabKey);

    // Printable letters/digits translate to the same codes JUCE's local handling
    // produces: an unmodified character via the active keyboard layout, which is a
    // plain uppercase/ASCII code in the JUCE key space. The tests run on Windows, so
    // MapVirtualKeyW(VK_A, MAPVK_VK_TO_CHAR) yields the 'A' key code ('A' = 65).
    // JUCE treats key codes as case-insensitive (compare toLowerCase), so a mapping
    // learned as 'a' matches this physical 'A' key.
    const int keyA = tf::kb::windowsVkToJuceKeyCode(VK_A);
    CHECK(keyA == (int) 'A' || keyA == (int) 'a');   // layout may give upper or lower
    CHECK(juce::KeyPress(VK_A).getTextDescription().isNotEmpty());   // sanity: printable

    // Modifier keys never map (they are not assignable controls).
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_SHIFT)   == 0);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_CONTROL) == 0);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_MENU)    == 0);
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_LWIN)    == 0);

    // The Capture Escape Shortcut (Ctrl+Shift+F11) maps to F11 so the controller can
    // recognize the chord from hook events.
    CHECK(tf::kb::windowsVkToJuceKeyCode(VK_F11) == juce::KeyPress::F11Key);

    std::printf("%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
