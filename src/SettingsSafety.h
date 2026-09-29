#pragma once

#include <JuceHeader.h>
#include "TemplateManager.h"
#include "Preset.h"

namespace SettingsSafety
{
enum class RestoreResult { fresh, restored, invalid };
enum class PresetPathResult { none, missing, present };
enum class Warning { none, unreadableSettings, templateSaveFailed, missingPreset, damagedPreset };
inline Warning warningFor(RestoreResult result)
{
    return result == RestoreResult::invalid ? Warning::unreadableSettings : Warning::none;
}
inline Warning warningForTemplateSave(bool saved)
{
    return saved ? Warning::none : Warning::templateSaveFailed;
}
inline Warning warningForPresetPath(PresetPathResult result)
{
    return result == PresetPathResult::missing ? Warning::missingPreset : Warning::none;
}
inline Warning warningForPresetLoad(bool loaded)
{
    return loaded ? Warning::none : Warning::damagedPreset;
}
inline bool showTemplateUnsaved(bool chainDirty, bool saveFailed)
{
    return chainDirty || saveFailed;
}

class RecoveredSettings
{
public:
    void begin() { pending = true; }
    void finishRetry(bool selectedTemplate, bool activated)
    {
        if (! selectedTemplate || activated) pending = false;
    }
    void commitExplicitly() { pending = false; }
    bool blocksWrites() const { return pending; }

private:
    bool pending = false;
};
inline PresetPathResult resolveLastPreset(const juce::PropertiesFile& settings,
                                           const juce::String& key, juce::File& file,
                                           std::function<void(Warning)> notify = {})
{
    const auto path = settings.getValue(key);
    if (path.isEmpty()) return PresetPathResult::none;
    file = juce::File(path);
    const auto result = file.existsAsFile() ? PresetPathResult::present : PresetPathResult::missing;
    if (notify && warningForPresetPath(result) != Warning::none) notify(warningForPresetPath(result));
    return result;
}
inline bool writesAllowed(RestoreResult result) { return result != RestoreResult::invalid; }
inline bool protectIfInvalid(juce::PropertiesFile& settings, RestoreResult result)
{
    if (result != RestoreResult::invalid) return false;
    settings.setNeedsToBeSaved(false);
    return true;
}
inline bool loadPreset(const juce::File& file, juce::Array<PluginChain::SlotSpec>& specs,
                       juce::Array<PluginChain::SectionDef>& sections,
                       std::function<void(Warning)> notify, juce::String* error = nullptr)
{
    juce::String reason;
    const bool loaded = Preset::loadFromFile(file, specs, sections, &reason);
    if (error != nullptr) *error = reason;
    if (! loaded && notify) notify(Warning::damagedPreset);
    return loaded;
}
template <typename Properties>
inline bool saveIfAllowed(Properties& properties, bool protectedSettings)
{
    return ! protectedSettings && properties.saveIfNeeded();
}
template <typename Value>
inline bool setValue(juce::PropertiesFile& settings, bool protectedSettings,
                     const juce::String& key, const Value& value)
{
    if (protectedSettings) return false;
    settings.setValue(key, value);
    return true;
}

inline RestoreResult restore(juce::PropertiesFile& settings, TemplateManager& manager,
                             ControlMap* restoredLegacyMap = nullptr,
                             std::function<void(Warning)> notify = {})
{
    const auto file = settings.getFile();
    if (file.existsAsFile() && ! settings.isValidFile())
    {
        if (notify) notify(Warning::unreadableSettings);
        return RestoreResult::invalid;
    }
    const bool hasScenes = settings.containsKey("scenes");
    TemplateManager candidate;
    if (hasScenes)
    {
        auto xml = juce::XmlDocument::parse(settings.getValue("scenes"));
        if (xml == nullptr || ! candidate.fromValueTree(juce::ValueTree::fromXml(*xml)))
        {
            if (notify) notify(Warning::unreadableSettings);
            return RestoreResult::invalid;
        }
    }
    ControlMap legacyMap;
    if (settings.containsKey("controlMap"))
    {
        auto xml = settings.getXmlValue("controlMap");
        if (xml == nullptr || ! legacyMap.fromValueTree(juce::ValueTree::fromXml(*xml)))
        {
            if (notify) notify(Warning::unreadableSettings);
            return RestoreResult::invalid;
        }
    }
    if (hasScenes) manager = std::move(candidate);
    if (restoredLegacyMap != nullptr) *restoredLegacyMap = std::move(legacyMap);
    const auto result = hasScenes ? RestoreResult::restored : RestoreResult::fresh;
    if (notify && warningFor(result) != Warning::none) notify(warningFor(result));
    return result;
}

inline bool writeTemplates(juce::PropertiesFile& settings, const TemplateManager& manager)
{
    auto xml = manager.toValueTree().createXml();
    if (xml == nullptr) return false;
    const bool hadValue = settings.containsKey("scenes");
    const auto previous = settings.getValue("scenes");
    const bool wasDirty = settings.needsToBeSaved();
    settings.setValue("scenes", xml.get());
    if (settings.saveIfNeeded()) return true;
    if (hadValue) settings.setValue("scenes", previous);
    else settings.removeValue("scenes");
    settings.setNeedsToBeSaved(wasDirty);
    return false;
}

class TemplateSave
{
public:
    bool save(juce::PropertiesFile& settings, const TemplateManager& manager, bool explicitAttempt,
              std::function<void(Warning)> notify = {})
    {
        if (failed && ! explicitAttempt) return false;
        if (settings.needsToBeSaved() && ! settings.saveIfNeeded())
        {
            failed = true;
            if (notify) notify(Warning::templateSaveFailed);
            return false;
        }
        failed = ! writeTemplates(settings, manager);
        if (failed && notify) notify(warningForTemplateSave(false));
        return ! failed;
    }
    bool hasFailed() const { return failed; }

private:
    bool failed = false;
};

template <typename ApplicationPropertiesLike>
inline RestoreResult refresh(ApplicationPropertiesLike& properties, TemplateManager& manager,
                             ControlMap* restoredLegacyMap = nullptr,
                             std::function<void(Warning)> notify = {})
{
    auto* cached = properties.getUserSettings();
    if (cached == nullptr || cached->needsToBeSaved()) return RestoreResult::invalid;
    properties.closeFiles();
    auto* fresh = properties.getUserSettings();
    return fresh != nullptr ? restore(*fresh, manager, restoredLegacyMap, std::move(notify)) : RestoreResult::invalid;
}

inline bool reset(juce::PropertiesFile& settings)
{
    TemplateManager emptyTemplates;
    ControlMap emptyMap;
    auto scenesXml = emptyTemplates.toValueTree().createXml();
    auto mapXml = emptyMap.toValueTree().createXml();
    if (scenesXml == nullptr || mapXml == nullptr) return false;

    const bool hadScenes = settings.containsKey("scenes");
    const auto oldScenes = settings.getValue("scenes");
    const bool hadMap = settings.containsKey("controlMap");
    const auto oldMap = settings.getValue("controlMap");
    const bool wasDirty = settings.needsToBeSaved();
    settings.setValue("scenes", scenesXml.get());
    settings.setValue("controlMap", mapXml.get());
    if (settings.saveIfNeeded()) return true;
    if (hadScenes) settings.setValue("scenes", oldScenes); else settings.removeValue("scenes");
    if (hadMap) settings.setValue("controlMap", oldMap); else settings.removeValue("controlMap");
    settings.setNeedsToBeSaved(wasDirty);
    return false;
}
}
