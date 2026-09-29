#include <JuceHeader.h>
#include "SettingsSafety.h"
#include "Preset.h"
#include <windows.h>
#include <iostream>

namespace
{
juce::PropertiesFile::Options options(int saveDelay = -1)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "SettingsSafetyTests";
    o.filenameSuffix = "xml";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = saveDelay;
    return o;
}

bool sameBytes(const juce::File& file, const juce::String& before)
{
    return file.loadFileAsString() == before;
}

bool testFreshAndRoundTrip()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    auto file = dir.getChildFile("settings.xml");
    TemplateManager manager;
    {
        juce::PropertiesFile props(file, options());
        if (SettingsSafety::restore(props, manager) != SettingsSafety::RestoreResult::fresh) return false;
        manager.addScene("Test", {}, {});
        TemplateManager direct;
        if (! direct.fromValueTree(manager.toValueTree()) || ! SettingsSafety::writeTemplates(props, manager))
            return false;
    }
    TemplateManager loaded;
    juce::PropertiesFile reopened(file, options());
    return SettingsSafety::restore(reopened, loaded) == SettingsSafety::RestoreResult::restored
        && loaded.getNumScenes() == 1 && loaded.getScene(0).name == "Test";
}

bool testRejectedDataIsUntouched()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    auto file = dir.getChildFile("settings.xml");
    juce::PropertiesFile props(file, options());
    props.setValue("scenes", "<SCENES current=\"bogus\"/>");
    if (! props.saveIfNeeded()) return false;
    const auto bytes = file.loadFileAsString();
    TemplateManager manager;
    SettingsSafety::Warning observedWarning = SettingsSafety::Warning::none;
    const auto result = SettingsSafety::restore(props, manager, nullptr,
        [&](SettingsSafety::Warning warning) { observedWarning = warning; });
    const bool protectedSettings = ! SettingsSafety::writesAllowed(result);
    const bool failedRestore = result == SettingsSafety::RestoreResult::invalid;
    const bool setChain = SettingsSafety::setValue(props, protectedSettings, "chainViewMode", true);
    const bool setWindow = SettingsSafety::setValue(props, protectedSettings, "windowState", "changed");
    const bool setPath = SettingsSafety::setValue(props, protectedSettings, "lastPresetPath", "changed");
    const bool allowedSave = SettingsSafety::saveIfAllowed(props, protectedSettings);
    if (! failedRestore || observedWarning != SettingsSafety::Warning::unreadableSettings
        || setChain || setWindow || setPath || allowedSave || ! sameBytes(file, bytes)) return false;
    props.setNeedsToBeSaved(false); // simulated MainComponent shutdown leaves protected properties clean
    if (! sameBytes(file, bytes)) return false;
    props.removeValue("scenes");
    props.setValue("scenes", "not XML");
    if (! props.saveIfNeeded() || SettingsSafety::restore(props, manager) != SettingsSafety::RestoreResult::invalid)
        return false;
    props.removeValue("scenes");
    props.setValue("controlMap", "<CONTROLMAP><BINDING/></CONTROLMAP>");
    if (! props.saveIfNeeded() || SettingsSafety::restore(props, manager) != SettingsSafety::RestoreResult::invalid)
        return false;

    auto broken = dir.getChildFile("broken.xml");
    broken.replaceWithText("not a properties document");
    juce::PropertiesFile brokenProps(broken, options());
    return SettingsSafety::restore(brokenProps, manager) == SettingsSafety::RestoreResult::invalid;
}

bool testProtectedShutdownLifecycle()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    for (const auto& original : { juce::String("<PROPERTIES><VALUE name=\"scenes\">broken</VALUE></PROPERTIES>"),
                                  juce::String("not a properties document") })
    {
        auto file = dir.getChildFile(juce::Uuid().toString() + ".xml");
        if (! file.replaceWithText(original)) return false;
        const auto bytes = file.loadFileAsString();
        {
            juce::PropertiesFile settings(file, options());
            TemplateManager templates;
            SettingsSafety::Warning warning = SettingsSafety::Warning::none;
            const auto result = SettingsSafety::restore(settings, templates, nullptr,
                [&](SettingsSafety::Warning observed) { warning = observed; });
            const bool protectedSettings = SettingsSafety::protectIfInvalid(settings, result);
            if (! protectedSettings || warning != SettingsSafety::Warning::unreadableSettings
                || SettingsSafety::setValue(settings, protectedSettings, "windowState", "attempt")
                || SettingsSafety::setValue(settings, protectedSettings, "audioDeviceState", "attempt")
                || SettingsSafety::setValue(settings, protectedSettings, "controlMap", "attempt")
                || SettingsSafety::setValue(settings, protectedSettings, "scenes", "attempt")
                || SettingsSafety::saveIfAllowed(settings, protectedSettings)
                || ! sameBytes(file, bytes)) return false;
        }
        if (! sameBytes(file, bytes)) return false; // PropertiesFile destructor is the shutdown flush
    }
    return true;
}

bool testLastPresetPathsRemainUntouched()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    auto settingsFile = dir.getChildFile("settings.xml");
    juce::PropertiesFile settings(settingsFile, options());
    auto missing = dir.getChildFile("missing.tfpreset");
    settings.setValue("lastPresetPath", missing.getFullPathName());
    if (! settings.saveIfNeeded()) return false;
    const auto missingBytes = settingsFile.loadFileAsString();
    juce::File resolved;
    SettingsSafety::Warning missingWarning = SettingsSafety::Warning::none;
    const auto missingResult = SettingsSafety::resolveLastPreset(settings, "lastPresetPath", resolved,
        [&](SettingsSafety::Warning warning) { missingWarning = warning; });
    if (missingResult != SettingsSafety::PresetPathResult::missing
        || missingWarning != SettingsSafety::Warning::missingPreset
        || settings.getValue("lastPresetPath") != missing.getFullPathName()
        || ! sameBytes(settingsFile, missingBytes)) return false;

    auto damaged = dir.getChildFile("damaged.tfpreset");
    damaged.replaceWithText("not a preset");
    settings.setValue("lastPresetPath", damaged.getFullPathName());
    if (! settings.saveIfNeeded()) return false;
    const auto damagedSettingsBytes = settingsFile.loadFileAsString();
    const auto damagedPresetBytes = damaged.loadFileAsString();
    juce::Array<PluginChain::SlotSpec> specs;
    juce::Array<PluginChain::SectionDef> sections;
    juce::String error;
    SettingsSafety::Warning damagedPathWarning = SettingsSafety::Warning::none;
    const auto damagedResult = SettingsSafety::resolveLastPreset(settings, "lastPresetPath", resolved,
        [&](SettingsSafety::Warning warning) { damagedPathWarning = warning; });
    SettingsSafety::Warning damagedLoadWarning = SettingsSafety::Warning::none;
    const bool damagedLoaded = SettingsSafety::loadPreset(resolved, specs, sections,
        [&](SettingsSafety::Warning warning) { damagedLoadWarning = warning; }, &error);
    return damagedResult == SettingsSafety::PresetPathResult::present
        && ! damagedLoaded
        && damagedPathWarning == SettingsSafety::Warning::none
        && damagedLoadWarning == SettingsSafety::Warning::damagedPreset
        && ! error.isEmpty() && settings.getValue("lastPresetPath") == damaged.getFullPathName()
        && sameBytes(settingsFile, damagedSettingsBytes) && sameBytes(damaged, damagedPresetBytes);
}

bool testInvalidControlMapAndReset()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    auto file = dir.getChildFile("settings.xml");
    juce::PropertiesFile props(file, options());
    ControlMap runtimeMap;
    runtimeMap.addBinding({ { ControlTrigger::Type::midiCC, 1, 64 }, { ControlAction::Type::nextTemplate, 0 } });
    runtimeMap.addExpression({ 1, 11, 0, 2 });
    TemplateManager malformed;
    malformed.addScene("Bad", {}, {});
    auto root = malformed.toValueTree();
    auto badMap = juce::ValueTree("CONTROLMAP");
    badMap.addChild(juce::ValueTree("BINDING"), -1, nullptr);
    root.getChild(0).addChild(badMap, -1, nullptr);
    auto xml = root.createXml();
    props.setValue("scenes", xml.get());
    if (! props.saveIfNeeded()) return false;
    TemplateManager loaded;
    if (SettingsSafety::restore(props, loaded) != SettingsSafety::RestoreResult::invalid) return false;
    const auto bytes = file.loadFileAsString();
    // Cancel means no reset call and no disk mutation.
    if (! sameBytes(file, bytes)) return false;
    HANDLE lock = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return false;
    const bool resetFailed = ! SettingsSafety::reset(props);
    CloseHandle(lock);
    if (! resetFailed || ! sameBytes(file, bytes)
        || SettingsSafety::restore(props, loaded) != SettingsSafety::RestoreResult::invalid
        || ! SettingsSafety::reset(props)) return false;
    TemplateManager reset;
    if (SettingsSafety::restore(props, reset) != SettingsSafety::RestoreResult::restored
        || reset.getNumScenes() != 0) return false;
    props.removeValue("scenes");
    props.setValue("controlMap", "<CONTROLMAP><BINDING/></CONTROLMAP>");
    if (! props.saveIfNeeded() || SettingsSafety::restore(props, reset) != SettingsSafety::RestoreResult::invalid)
        return false;
    const auto badLegacyBytes = file.loadFileAsString();
    HANDLE legacyLock = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (legacyLock == INVALID_HANDLE_VALUE) return false;
    const bool legacyResetFailed = ! SettingsSafety::reset(props);
    CloseHandle(legacyLock);
    if (! legacyResetFailed || ! sameBytes(file, badLegacyBytes) || ! SettingsSafety::reset(props)) return false;
    runtimeMap = ControlMap{}; // same replacement used by confirmed application reset
    auto emptyMapXml = runtimeMap.toValueTree().createXml();
    if (emptyMapXml == nullptr) return false;
    props.setValue("controlMap", emptyMapXml.get());
    if (! props.saveIfNeeded()) return false; // simulated shutdown Control Map write
    ControlMap reopenedMap;
    auto mapXml = props.getXmlValue("controlMap");
    if (mapXml == nullptr || ! reopenedMap.fromValueTree(juce::ValueTree::fromXml(*mapXml))) return false;
    return SettingsSafety::restore(props, reset) == SettingsSafety::RestoreResult::restored
        && reset.getNumScenes() == 0 && reopenedMap.getNumBindings() == 0
        && reopenedMap.getNumExpressions() == 0;
}

struct TemporaryApplicationProperties
{
    juce::File file;
    juce::PropertiesFile::Options config = options();
    std::unique_ptr<juce::PropertiesFile> cached;
    juce::PropertiesFile* getUserSettings()
    {
        if (! cached) cached = std::make_unique<juce::PropertiesFile>(file, config);
        return cached.get();
    }
    void closeFiles() { cached.reset(); }
};

bool testRepairedRefreshUsesNewCache()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    TemporaryApplicationProperties app { dir.getChildFile("settings.xml") };
    {
        juce::PropertiesFile initial(app.file, app.config);
        initial.setValue("scenes", "broken");
        if (! initial.saveIfNeeded()) return false;
    }
    TemplateManager manager;
    if (SettingsSafety::refresh(app, manager) != SettingsSafety::RestoreResult::invalid) return false;

    TemplateManager repaired;
    repaired.addScene("Repaired", {}, {});
    repaired.setCurrentIndex(0);
    ControlMap legacy;
    legacy.addBinding({ { ControlTrigger::Type::midiCC, 1, 64 }, { ControlAction::Type::nextTemplate, 0 } });
    {
        juce::PropertiesFile disk(app.file, app.config);
        auto mapXml = legacy.toValueTree().createXml();
        if (! SettingsSafety::writeTemplates(disk, repaired) || mapXml == nullptr) return false;
        disk.setValue("controlMap", mapXml.get());
        if (! disk.saveIfNeeded()) return false;
    }
    const auto repairedBytes = app.file.loadFileAsString();
    ControlMap restoredLegacy;
    if (SettingsSafety::refresh(app, manager, &restoredLegacy) != SettingsSafety::RestoreResult::restored
        || manager.getNumScenes() != 1 || manager.getScene(0).name != "Repaired"
        || manager.getCurrentIndex() != 0 || restoredLegacy.getNumBindings() != 1) return false;
    manager.setCurrentIndex(-1); // retry stages; no selection is committed until activation
    auto* refreshed = app.getUserSettings();
    if (SettingsSafety::setValue(*refreshed, true, "windowState", "blocked")
        || SettingsSafety::saveIfAllowed(*refreshed, true)) return false;
    app.closeFiles(); // shutdown without explicit recall/save
    TemplateManager afterShutdown;
    juce::PropertiesFile check(app.file, app.config);
    return sameBytes(app.file, repairedBytes)
        && SettingsSafety::restore(check, afterShutdown) == SettingsSafety::RestoreResult::restored
        && afterShutdown.getScene(0).name == "Repaired" && afterShutdown.getCurrentIndex() == 0;
}

bool testRecoveredWriteGateTransitions()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    auto file = dir.getChildFile("settings.xml");
    juce::PropertiesFile settings(file, options());
    TemplateManager saved;
    saved.addScene("Recovered", {}, {});
    saved.setCurrentIndex(0);
    if (! SettingsSafety::writeTemplates(settings, saved)) return false;
    const auto original = file.loadFileAsString();

    SettingsSafety::RecoveredSettings state;
    state.begin();
    state.finishRetry(true, false); // staged Template could not activate
    if (! state.blocksWrites() || SettingsSafety::setValue(settings, state.blocksWrites(), "windowState", "blocked")
        || SettingsSafety::saveIfAllowed(settings, state.blocksWrites()) || ! sameBytes(file, original)) return false;

    state.finishRetry(true, true); // production recovery completion after activation
    if (state.blocksWrites() || ! SettingsSafety::setValue(settings, state.blocksWrites(), "windowState", "saved")
        || ! SettingsSafety::saveIfAllowed(settings, state.blocksWrites())) return false;
    TemplateManager reopened;
    juce::PropertiesFile check(file, options());
    if (SettingsSafety::restore(check, reopened) != SettingsSafety::RestoreResult::restored
        || reopened.getScene(0).name != "Recovered" || check.getValue("windowState") != "saved") return false;

    state.begin();
    state.finishRetry(false, false); // repaired settings without saved selection finish synchronously
    return ! state.blocksWrites();
}

bool testFailedSaveRequiresExplicitRetry()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    auto file = dir.getChildFile("settings.xml");
    juce::String oldBytes;
    SettingsSafety::TemplateSave save;
    TemplateManager candidate;
    candidate.addScene("Rejected", {}, {});
    {
        juce::PropertiesFile props(file, options(80));
        TemplateManager previous;
        previous.addScene("Old", {}, {});
        if (! save.save(props, previous, true)) return false;
        oldBytes = file.loadFileAsString();
        HANDLE lock = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) return false;
        SettingsSafety::Warning saveWarning = SettingsSafety::Warning::none;
        const bool failed = ! save.save(props, candidate, true,
            [&](SettingsSafety::Warning warning) { saveWarning = warning; }) && save.hasFailed();
        const bool unsavedRemainsVisible = SettingsSafety::showTemplateUnsaved(false, save.hasFailed());
        CloseHandle(lock);
        if (! failed || ! unsavedRemainsVisible
            || saveWarning != SettingsSafety::Warning::templateSaveFailed
            || save.save(props, candidate, false)
            || SettingsSafety::setValue(props, save.hasFailed(), "windowState", "rejected")
            || SettingsSafety::saveIfAllowed(props, save.hasFailed()) || ! sameBytes(file, oldBytes)) return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(160);
    }
    if (! sameBytes(file, oldBytes)) return false;
    {
        juce::PropertiesFile retry(file, options());
        if (! save.save(retry, candidate, true)) return false;
    }
    TemplateManager loaded;
    juce::PropertiesFile check(file, options());
    return SettingsSafety::restore(check, loaded) == SettingsSafety::RestoreResult::restored
        && loaded.getScene(0).name == "Rejected";
}

bool testLockedSaveKeepsOldFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    auto file = dir.getChildFile("settings.xml");
    juce::String bytes;
    {
        juce::PropertiesFile props(file, options(100));
        TemplateManager old;
        old.addScene("Usable", {}, {});
        if (! SettingsSafety::writeTemplates(props, old)) return false;
        bytes = file.loadFileAsString();
        TemplateManager changed;
        changed.addScene("Candidate", {}, {});
        HANDLE lock = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) return false;
        const bool failed = ! SettingsSafety::writeTemplates(props, changed);
        CloseHandle(lock);
        if (! failed || ! sameBytes(file, bytes)) return false;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
    }
    if (! sameBytes(file, bytes)) return false; // timer and destructor must not retry the rejected candidate
    TemplateManager reopened;
    juce::PropertiesFile check(file, options());
    return SettingsSafety::restore(check, reopened) == SettingsSafety::RestoreResult::restored
        && reopened.getScene(0).name == "Usable" && sameBytes(file, bytes);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const bool fresh = testFreshAndRoundTrip();
    const bool rejected = testRejectedDataIsUntouched();
    const bool lifecycle = testProtectedShutdownLifecycle();
    const bool lastPreset = testLastPresetPathsRemainUntouched();
    const bool mapReset = testInvalidControlMapAndReset();
    const bool refreshed = testRepairedRefreshUsesNewCache();
    const bool locked = testLockedSaveKeepsOldFile();
    const bool explicitRetry = testFailedSaveRequiresExplicitRetry();
    const bool recoveredGate = testRecoveredWriteGateTransitions();
    std::cout << "fresh=" << fresh << " rejected=" << rejected << " lifecycle=" << lifecycle
              << " last-preset=" << lastPreset
              << " map/reset=" << mapReset << " refreshed=" << refreshed << " locked=" << locked
              << " explicit-retry=" << explicitRetry << " recovered-gate=" << recoveredGate << "\n";
    return fresh && rejected && lifecycle && lastPreset && mapReset && refreshed && locked
        && explicitRetry && recoveredGate ? 0 : 1;
}
