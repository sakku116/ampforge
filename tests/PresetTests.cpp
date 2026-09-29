#include <JuceHeader.h>
#include "../src/Preset.h"
#include "../src/TemplateManager.h"
#include <cstdio>
#include <limits>
#define NOMINMAX
#include <windows.h>

namespace
{
    juce::ValueTree validV2()
    {
        juce::ValueTree root("TONEFORGE_PRESET");
        root.setProperty("version", 2, nullptr);
        juce::ValueTree section("SECTION");
        section.setProperty("sectionId", 1, nullptr);
        section.setProperty("sectionName", "Stomp", nullptr);
        section.setProperty("sectionType", "stomp", nullptr);
        root.addChild(section, -1, nullptr);
        juce::ValueTree slot("SLOT");
        slot.setProperty("sectionId", 1, nullptr);
        auto plugin = juce::ValueTree("PLUGIN");
        plugin.setProperty("name", "Fixture", nullptr);
        plugin.setProperty("format", "VST3", nullptr);
        plugin.setProperty("file", "fixture.vst3", nullptr);
        plugin.setProperty("uid", "0", nullptr);
        slot.addChild(plugin, -1, nullptr);
        root.addChild(slot, -1, nullptr);
        return root;
    }

    bool rejects(juce::ValueTree tree)
    {
        juce::Array<PluginChain::SlotSpec> specs;
        juce::Array<PluginChain::SectionDef> sections;
        specs.add({});
        sections.add({ 77, "preserve", PluginChain::SectionDef::Type::stomp });
        return ! Preset::fromValueTree(tree, specs, sections) && specs.size() == 1
            && sections.size() == 1 && sections[0].id == 77;
    }
}

int main()
{
    juce::MessageManager::getInstance();
    auto v1 = juce::ValueTree("TONEFORGE_PRESET");
    v1.setProperty("version", 1, nullptr);
    juce::Array<PluginChain::SlotSpec> specs;
    juce::Array<PluginChain::SectionDef> sections;
    if (! Preset::fromValueTree(v1, specs, sections) || ! specs.isEmpty()
        || sections.size() != 1 || sections[0].name != "Stomp 1") return 1;
    auto oldEmptyV2 = juce::ValueTree("TONEFORGE_PRESET");
    oldEmptyV2.setProperty("version", 2, nullptr); // Older builds wrote empty v2 files without a SECTION.
    if (! Preset::fromValueTree(oldEmptyV2, specs, sections) || ! specs.isEmpty()
        || sections.size() != 1 || sections[0].name != "Stomp 1") return 49;
    auto missingV2Section = validV2();
    missingV2Section.removeChild(0, nullptr);
    if (! rejects(missingV2Section)) return 50;

    PluginChain::SlotSpec source;
    source.description.name = "Fixture";
    source.description.pluginFormatName = "VST3";
    source.description.fileOrIdentifier = "fixture.vst3";
    source.sectionId = 4;
    source.slotId = 21;
    source.customName = "Custom";
    source.bypassed = true;
    source.postGain = 0.0f;
    source.state.append("state", 5);
    juce::Array<PluginChain::SlotSpec> sourceSpecs;
    sourceSpecs.add(source);
    juce::Array<PluginChain::SectionDef> sourceSections;
    sourceSections.add({ 4, "Preset", PluginChain::SectionDef::Type::preset, true, 0.5f });
    auto tree = Preset::toValueTree(sourceSpecs, sourceSections, "roundtrip");
    if (! Preset::fromValueTree(tree, specs, sections) || specs.size() != 1
        || specs[0].slotId != 21 || specs[0].sectionId != 4 || specs[0].customName != "Custom"
        || ! specs[0].bypassed || specs[0].postGain != 0.0f || specs[0].state != source.state
        || sections.size() != 1 || sections[0].id != 4 || sections[0].gain != 0.5f
        || ! sections[0].bypassed) return 2;
    auto v1WithSection = tree.createCopy();
    v1WithSection.setProperty("version", 1, nullptr);
    if (! rejects(v1WithSection)) return 26;

    auto legacy = tree.createCopy();
    legacy.setProperty("version", 1, nullptr);
    legacy.removeChild(0, nullptr);
    legacy.getChild(0).removeProperty("sectionId", nullptr);
    legacy.getChild(0).removeProperty("slotId", nullptr);
    if (! Preset::fromValueTree(legacy, specs, sections) || specs.size() != 1
        || specs[0].slotId != 0 || specs[0].sectionId != 1 || sections.size() != 1) return 16;

    auto bad = validV2();
    bad.getChild(0).setProperty("sectionType", "unknown", nullptr);
    if (! rejects(bad)) return 3;
    bad = validV2(); bad.getChild(1).setProperty("bypassed", "perhaps", nullptr);
    if (! rejects(bad)) return 4;
    bad = validV2(); bad.getChild(1).setProperty("postGain", "NaN", nullptr);
    if (! rejects(bad)) return 5;
    bad = validV2(); bad.getChild(1).setProperty("state", "%%%", nullptr);
    if (! rejects(bad)) return 6;
    bad = validV2(); bad.getChild(1).getChild(0).setProperty("name", "", nullptr);
    if (! rejects(bad)) return 7;
    for (const auto* key : { "numInputs", "numOutputs", "fileTime", "infoUpdateTime",
                              "isInstrument", "isShell", "hasARAExtension" })
    {
        bad = validV2(); bad.getChild(1).getChild(0).setProperty(key, "bogus", nullptr);
        if (! rejects(bad)) return 42;
    }
    bad = validV2(); bad.getChild(0).setProperty("sectionName", "", nullptr);
    if (! rejects(bad)) return 17;
    bad = validV2(); bad.setProperty("version", "2x", nullptr);
    if (! rejects(bad)) return 18;
    bad = validV2(); bad.addChild(bad.getChild(0).createCopy(), -1, nullptr);
    if (! rejects(bad)) return 19;
    bad = validV2(); auto duplicateSlot = bad.getChild(1).createCopy();
    duplicateSlot.setProperty("slotId", 5, nullptr); bad.getChild(1).setProperty("slotId", 5, nullptr);
    bad.addChild(duplicateSlot, -1, nullptr);
    if (! rejects(bad)) return 20;
    bad = validV2(); bad.addChild(juce::ValueTree("OTHER"), -1, nullptr);
    if (! rejects(bad)) return 8;

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ampforge-preset-tests", "", true);
    struct Cleanup { juce::File dir; ~Cleanup() { dir.deleteRecursively(); } } cleanup { tempDir };
    if (! tempDir.createDirectory()) return 9;
    const auto file = tempDir.getChildFile("roundtrip.tfpreset");
    juce::String error;
    auto loadTree = [&](juce::ValueTree tree)
    {
        auto xml = tree.createXml();
        return xml != nullptr && xml->writeTo(file)
            && Preset::loadFromFile(file, specs, sections, &error);
    };
    specs.clear(); sections.clear();
    auto missingVersion = validV2(); missingVersion.removeProperty("version", nullptr);
    if (loadTree(missingVersion) || ! specs.isEmpty() || ! sections.isEmpty()) return 21;
    for (const auto* key : { "uniqueId", "uid" })
    {
        auto oversized = validV2(); oversized.getChild(1).getChild(0).setProperty(key, "100000001", nullptr);
        if (loadTree(oversized)) return 45;
    }
    for (const auto* key : { "fileTime", "infoUpdateTime" })
    {
        auto oversized = validV2(); oversized.getChild(1).getChild(0).setProperty(key, "10000000000000000", nullptr);
        if (loadTree(oversized)) return 46;
    }
    for (const auto* key : { "numInputs", "numOutputs" })
    {
        auto oversized = validV2(); oversized.getChild(1).getChild(0).setProperty(key, "2147483648", nullptr);
        if (loadTree(oversized)) return 47;
    }
    auto validMetadata = validV2();
    auto validPlugin = validMetadata.getChild(1).getChild(0);
    validPlugin.setProperty("uniqueId", "00000001", nullptr);
    validPlugin.setProperty("fileTime", "ffffffffffffffff", nullptr);
    validPlugin.setProperty("numInputs", std::numeric_limits<int>::max(), nullptr);
    if (! loadTree(validMetadata)) return 48;
    auto versionlessV1 = juce::ValueTree("TONEFORGE_PRESET");
    if (! loadTree(versionlessV1) || sections.size() != 1 || sections[0].name != "Stomp 1") return 22;
    auto edgeId = validV2(); edgeId.getChild(0).setProperty("sectionId", std::numeric_limits<int>::max(), nullptr);
    if (loadTree(edgeId)) return 23;
    edgeId = validV2(); edgeId.getChild(1).setProperty("state", "-1.", nullptr);
    if (loadTree(edgeId)) return 24;
    PluginChain::SlotSpec allocationSpec;
    juce::Array<PluginChain::SlotSpec> allocationSpecs;
    allocationSpec.slotId = std::numeric_limits<int>::max() - 2;
    allocationSpecs.add(allocationSpec);
    allocationSpecs.add({}); allocationSpecs.add({});
    if (PluginChain::canAllocateSlotIds(allocationSpecs, 1)
        || PluginChain::canAllocateSlotIds(allocationSpecs, std::numeric_limits<int>::max() - 2)) return 39;
    allocationSpecs.clear();
    allocationSpec.slotId = 100;
    allocationSpecs.add(allocationSpec); allocationSpecs.add({});
    if (! PluginChain::canAllocateSlotIds(allocationSpecs, 1)) return 40;

    edgeId = validV2();
    edgeId.getChild(1).setProperty("slotId", std::numeric_limits<int>::max() - 2, nullptr);
    auto overflowMissingSlot = edgeId.getChild(1).createCopy();
    overflowMissingSlot.removeProperty("slotId", nullptr);
    edgeId.addChild(overflowMissingSlot, -1, nullptr);
    if (loadTree(edgeId)) return 41;

    edgeId = validV2(); edgeId.getChild(0).setProperty("sectionId", std::numeric_limits<int>::max() - 2, nullptr);
    edgeId.getChild(1).setProperty("sectionId", std::numeric_limits<int>::max() - 2, nullptr);
    edgeId.getChild(1).setProperty("slotId", std::numeric_limits<int>::max() - 2, nullptr);
    auto missingSlot = edgeId.getChild(1).createCopy();
    missingSlot.removeProperty("slotId", nullptr);
    edgeId.addChild(missingSlot, -1, nullptr);
    edgeId.addChild(missingSlot.createCopy(), -1, nullptr);
    if (loadTree(edgeId)) return 27;
    edgeId = validV2(); edgeId.getChild(1).getChild(0).removeProperty("uid", nullptr);
    if (loadTree(edgeId)) return 28;
    juce::Array<PluginChain::SectionDef> noSections;
    auto emptyTree = Preset::toValueTree({}, noSections, "empty");
    if (! Preset::fromValueTree(emptyTree, specs, sections) || sections.size() != 1) return 25;
    auto v1FileTree = Preset::toValueTree(sourceSpecs, "legacy");
    v1FileTree.setProperty("version", 1, nullptr);
    v1FileTree.removeChild(0, nullptr);
    v1FileTree.getChild(0).removeProperty("sectionId", nullptr);
    v1FileTree.getChild(0).removeProperty("slotId", nullptr);
    auto v1Xml = v1FileTree.createXml();
    if (! v1Xml || ! v1Xml->writeTo(file)) return 29;
    specs.clear(); sections.clear();
    if (! Preset::loadFromFile(file, specs, sections, &error) || specs.size() != 1
        || specs[0].slotId != 0 || specs[0].customName != "Custom" || sections.size() != 1
        || sections[0].name != "Stomp 1" || sections[0].type != PluginChain::SectionDef::Type::stomp) return 30;

    auto second = source; second.slotId = 22; second.sectionId = 8; second.customName = "Second";
    second.bypassed = false; second.postGain = 0.25f;
    juce::Array<PluginChain::SlotSpec> multiSpecs; multiSpecs.add(source); multiSpecs.add(second);
    juce::Array<PluginChain::SectionDef> multiSections;
    multiSections.add({ 4, "Preset", PluginChain::SectionDef::Type::preset, true, 0.5f });
    multiSections.add({ 8, "Stomp", PluginChain::SectionDef::Type::stomp, false, 0.75f });
    if (! Preset::saveToFile(multiSpecs, multiSections, "multi", file, &error)) return 31;
    specs.clear(); sections.clear();
    if (! Preset::loadFromFile(file, specs, sections, &error) || specs.size() != 2 || sections.size() != 2
        || specs[0].slotId != 21 || specs[0].sectionId != 4 || specs[0].description.name != "Fixture"
        || specs[0].customName != "Custom" || ! specs[0].bypassed || specs[0].postGain != 0.0f
        || specs[0].state != source.state || specs[1].slotId != 22 || specs[1].sectionId != 8
        || specs[1].customName != "Second" || specs[1].bypassed || specs[1].postGain != 0.25f
        || sections[0].id != 4 || sections[0].type != PluginChain::SectionDef::Type::preset
        || ! sections[0].bypassed || sections[0].gain != 0.5f || sections[1].id != 8
        || sections[1].type != PluginChain::SectionDef::Type::stomp || sections[1].gain != 0.75f) return 32;

    for (auto invalid : { [] { auto t = validV2(); t.setProperty("version", 3, nullptr); return t; }(),
                          [] { auto t = validV2(); t.addChild(juce::ValueTree("OTHER"), -1, nullptr); return t; }() })
    {
        auto xml = invalid.createXml();
        if (! xml || ! xml->writeTo(file)) return 33;
        juce::MemoryBlock originalBytes;
        if (! file.loadFileAsData(originalBytes)) return 35;
        specs.clear(); sections.clear(); specs.add(source); sections.add(multiSections[0]);
        if (Preset::loadFromFile(file, specs, sections, &error) || error.isEmpty()
            || specs.size() != 1 || specs[0].slotId != source.slotId || sections.size() != 1
            || sections[0].id != multiSections[0].id) return 34;
        juce::MemoryBlock afterBytes;
        if (! file.loadFileAsData(afterBytes) || afterBytes != originalBytes) return 36;
    }

    if (! Preset::saveToFile(sourceSpecs, sourceSections, "roundtrip", file, &error)) return 10;
    const auto savedBytes = file.loadFileAsString();
    const auto lock = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                  0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return 37;
    const auto overwriteFailed = ! Preset::saveToFile(sourceSpecs, sourceSections, "overwrite", file, &error);
    CloseHandle(lock);
    if (! overwriteFailed || file.loadFileAsString() != savedBytes) return 38;
    specs.clear(); sections.clear();
    if (! Preset::loadFromFile(file, specs, sections, &error) || specs.size() != 1
        || specs[0].slotId != 21 || specs[0].postGain != 0.0f || sections.size() != 1) return 11;

    const auto blocked = tempDir.getChildFile("blocked");
    blocked.replaceWithText("keep me");
    if (Preset::saveToFile(sourceSpecs, sourceSections, "fail", blocked.getChildFile("child"), &error)
        || blocked.loadFileAsString() != "keep me") return 12;
    specs.add({}); sections.add({ 88, "preserve", PluginChain::SectionDef::Type::stomp });
    if (Preset::loadFromFile(tempDir.getChildFile("missing"), specs, sections, &error)
        || error.isEmpty() || specs.size() != 2 || sections.size() != 2) return 13;
    file.replaceWithText("<broken");
    if (Preset::loadFromFile(file, specs, sections, &error) || error.isEmpty()
        || specs.size() != 2 || sections.size() != 2) return 14;

    ControlMap map;
    map.addBinding({ { ControlTrigger::Type::key, 0, juce::KeyPress::F2Key },
                     { ControlAction::Type::activatePresetSlot, 21 } });
    TemplateManager manager;
    const auto index = manager.addScene("Template", sourceSpecs, sourceSections, map);
    manager.setCurrentIndex(index);
    TemplateManager restored;
    restored.fromValueTree(manager.toValueTree());
    if (restored.getNumScenes() != 1 || restored.getScene(0).specs.size() != 1
        || restored.getCurrentControlMapOr({}).matchKey(juce::KeyPress::F2Key).index != 21) return 15;
    TemplateManager guarded;
    guarded.addScene("kept", {}, {});
    guarded.setCurrentIndex(0);
    auto invalidTemplates = juce::ValueTree("SCENES");
    invalidTemplates.setProperty("current", 0, nullptr);
    auto invalidEntry = validV2();
    invalidEntry.removeProperty("version", nullptr);
    invalidTemplates.addChild(invalidEntry, -1, nullptr);
    auto validEntry = Preset::toValueTree({}, {}, "following");
    invalidTemplates.addChild(validEntry, -1, nullptr);
    if (guarded.fromValueTree(invalidTemplates) || guarded.getNumScenes() != 1
        || guarded.getCurrentIndex() != 0 || guarded.getScene(0).name != "kept") return 43;
    TemplateManager emptyGuard;
    if (emptyGuard.fromValueTree(invalidTemplates) || emptyGuard.getNumScenes() != 0
        || emptyGuard.getCurrentIndex() != -1) return 44;

    std::puts("Preset tests passed");
    return 0;
}
