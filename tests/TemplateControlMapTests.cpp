// Deterministic tests for the template-owned ControlMap lifecycle. These cover
// startup precedence, empty maps for captured templates, and stale slot actions.

#include <cstdio>

#include <JuceHeader.h>

#include "../src/ControlMap.h"
#include "../src/TemplateManager.h"

namespace
{
    int checks = 0;
    int failures = 0;

    #define CHECK(condition)                                                     \
        do {                                                                     \
            ++checks;                                                            \
            if (! (condition)) {                                                 \
                ++failures;                                                      \
                std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            }                                                                    \
        } while (false)

    ControlBinding keyBinding(int keyCode, ControlAction action)
    {
        return { { ControlTrigger::Type::key, 0, keyCode }, action };
    }
}

int main()
{
    juce::MessageManager::getInstance();

    // An active template always wins over the legacy standalone map restored from
    // application properties. Without a template, the legacy map remains usable.
    {
        ControlMap legacyMap;
        legacyMap.addBinding(keyBinding(juce::KeyPress::F1Key,
                                        { ControlAction::Type::toggleBypass, 11 }));

        TemplateManager templates;
        CHECK(templates.getCurrentControlMapOr(legacyMap).matchKey(juce::KeyPress::F1Key).isValid());

        ControlMap templateMap;
        templateMap.addBinding(keyBinding(juce::KeyPress::F2Key,
                                          { ControlAction::Type::activatePresetSlot, 22 }));
        const int templateIndex = templates.addScene("Template", {}, {}, templateMap);
        templates.setCurrentIndex(templateIndex);

        const auto selected = templates.getCurrentControlMapOr(legacyMap);
        CHECK(! selected.matchKey(juce::KeyPress::F1Key).isValid());
        CHECK(selected.matchKey(juce::KeyPress::F2Key).index == 22);
    }

    // Capturing a new template uses TemplateManager's empty-map default rather
    // than inheriting bindings from the previously active template.
    {
        TemplateManager templates;
        const int templateIndex = templates.addScene("Captured", {}, {});
        templates.setCurrentIndex(templateIndex);

        const auto capturedMap = templates.getCurrentControlMapOr({});
        CHECK(capturedMap.getNumBindings() == 0);
    }

    // Slot actions are invalid once their stable ID is absent from the active
    // chain. Removing them prevents stale mappings from blocking Learn Control.
    {
        ControlMap map;
        map.addBinding(keyBinding(juce::KeyPress::F1Key,
                                  { ControlAction::Type::toggleBypass, 99 }));
        map.addBinding(keyBinding(juce::KeyPress::F2Key,
                                  { ControlAction::Type::activatePresetSlot, 7 }));
        map.addBinding(keyBinding(juce::KeyPress::F3Key,
                                  { ControlAction::Type::nextTemplate, 0 }));

        CHECK(map.removeInvalidSlotBindings({ 7 }) == 1);
        CHECK(! map.matchKey(juce::KeyPress::F1Key).isValid());
        CHECK(map.matchKey(juce::KeyPress::F2Key).index == 7);
        CHECK(map.matchKey(juce::KeyPress::F3Key).type == ControlAction::Type::nextTemplate);
    }

    std::printf("%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
