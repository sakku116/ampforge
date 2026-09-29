#include "Preset.h"
#include "AppDataDir.h"
#include "HostDebug.h"
#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>

namespace Preset
{
namespace
{
    const juce::Identifier presetType    ("TONEFORGE_PRESET");
    const juce::Identifier slotType      ("SLOT");
    const juce::Identifier sectionType   ("SECTION");
    const juce::Identifier pluginTag     ("PLUGIN");   // tag produced by PluginDescription::createXml()
    const juce::Identifier propName      ("name");
    const juce::Identifier propVersion   ("version");
    const juce::Identifier propBypassed  ("bypassed");
    const juce::Identifier propState     ("state");
    const juce::Identifier propCustomName("customName");
    const juce::Identifier propSectionId ("sectionId");
    const juce::Identifier propSectionName("sectionName");
    const juce::Identifier propSectionType("sectionType");
    const juce::Identifier propSlotId    ("slotId");
    const juce::Identifier propPostGain  ("postGain");
    const juce::Identifier propSectionGain   ("sectionGain");
    const juce::Identifier propSectionBypassed("sectionBypassed");
    const juce::Identifier propUniqueId("uniqueId");
    const juce::Identifier propUid("uid");

    bool number(const juce::ValueTree& tree, const juce::Identifier& key, double fallback, double& result)
    {
        if (! tree.hasProperty(key)) { result = fallback; return true; }
        const auto text = tree.getProperty(key).toString();
        char* end = nullptr;
        result = std::strtod(text.toRawUTF8(), &end);
        return end != text.toRawUTF8() && *end == '\0' && std::isfinite(result);
    }

    bool validPluginMetadata(const juce::ValueTree& tree)
    {
        for (const auto* key : { "numInputs", "numOutputs" })
        {
            const juce::Identifier property(key);
            if (! tree.hasProperty(property)) continue;
            const auto text = tree.getProperty(property).toString();
            const auto* start = text.toRawUTF8();
            char* end = nullptr;
            errno = 0;
            const auto parsed = std::strtoll(start, &end, 10);
            if (errno == ERANGE || end == start || *end != '\0'
                || parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
        }
        for (const auto* key : { "fileTime", "infoUpdateTime" })
        {
            const auto property = juce::Identifier(key);
            if (! tree.hasProperty(property)) continue;
            const auto value = tree.getProperty(property).toString();
            if (value.isEmpty() || value.length() > 16) return false;
            for (auto c : value)
                if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
        for (const auto* key : { "isInstrument", "isShell", "hasARAExtension" })
        {
            const juce::Identifier property(key);
            if (! tree.hasProperty(property)) continue;
            const auto value = tree.getProperty(property).toString();
            if (value != "0" && value != "1" && value != "true" && value != "false") return false;
        }
        return true;
    }

    bool boolean(const juce::ValueTree& tree, const juce::Identifier& key, bool fallback, bool& result)
    {
        if (! tree.hasProperty(key)) { result = fallback; return true; }
        const auto text = tree.getProperty(key).toString();
        if (text == "1" || text == "true") { result = true; return true; }
        if (text == "0" || text == "false") { result = false; return true; }
        return false;
    }
}

juce::ValueTree toValueTree(const juce::Array<PluginChain::SlotSpec>& specs,
                             const juce::Array<PluginChain::SectionDef>& sections,
                             const juce::String& name)
{
    juce::ValueTree root(presetType);
    root.setProperty(propName, name, nullptr);
    root.setProperty(propVersion, 2, nullptr);

    if (sections.isEmpty())
    {
        juce::ValueTree secNode(sectionType);
        secNode.setProperty(propSectionId, 1, nullptr);
        secNode.setProperty(propSectionName, "Stomp 1", nullptr);
        secNode.setProperty(propSectionType, "stomp", nullptr);
        root.addChild(secNode, -1, nullptr);
    }
    for (const auto& sec : sections)
    {
        juce::ValueTree secNode(sectionType);
        secNode.setProperty(propSectionId,   sec.id,   nullptr);
        secNode.setProperty(propSectionName, sec.name, nullptr);
        secNode.setProperty(propSectionType,
                            sec.type == PluginChain::SectionDef::Type::preset
                                ? juce::String("preset") : juce::String("stomp"),
                            nullptr);
        if (sec.gain != 1.0f)
            secNode.setProperty(propSectionGain, sec.gain, nullptr);
        if (sec.bypassed)
            secNode.setProperty(propSectionBypassed, true, nullptr);
        root.addChild(secNode, -1, nullptr);
    }

    for (const auto& spec : specs)
    {
        juce::ValueTree slot(slotType);
        slot.setProperty(propBypassed,  spec.bypassed,  nullptr);
        slot.setProperty(propSectionId, spec.sectionId, nullptr);
        if (spec.slotId > 0)
            slot.setProperty(propSlotId, spec.slotId, nullptr);

        if (spec.customName.isNotEmpty())
            slot.setProperty(propCustomName, spec.customName, nullptr);

        if (spec.state.getSize() > 0)
            slot.setProperty(propState, spec.state.toBase64Encoding(), nullptr);

        if (spec.postGain != 1.0f)
            slot.setProperty(propPostGain, spec.postGain, nullptr);

        if (auto descXml = spec.description.createXml())
            slot.addChild(juce::ValueTree::fromXml(*descXml), -1, nullptr);

        root.addChild(slot, -1, nullptr);
    }

    return root;
}

juce::ValueTree toValueTree(const juce::Array<PluginChain::SlotSpec>& specs, const juce::String& name)
{
    // Delegate: all slots go into a synthetic "Stomp 1" section.
    juce::Array<PluginChain::SectionDef> secs;
    secs.add({ 1, "Stomp 1", PluginChain::SectionDef::Type::stomp });
    return toValueTree(specs, secs, name);
}

bool fromValueTree(const juce::ValueTree& tree,
                   juce::Array<PluginChain::SlotSpec>& outSpecs,
                   juce::Array<PluginChain::SectionDef>& outSections)
{
    if (! tree.hasType(presetType))
        return false;

    double versionValue = 1.0;
    const bool hasSections = tree.getChildWithName(sectionType).isValid();
    if (! tree.hasProperty(propVersion) && hasSections) return false;
    if (! number(tree, propVersion, 1.0, versionValue)
        || (versionValue != 1.0 && versionValue != 2.0)) return false;
    const int version = (int) versionValue;

    juce::Array<PluginChain::SlotSpec> specs;
    juce::Array<PluginChain::SectionDef> sections;

    if (version == 1 && hasSections) return false;

    // Read SECTION nodes.
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild(i);
        if (! child.hasType(sectionType))
        {
            if (! child.hasType(slotType)) return false;
            continue;
        }

        if (version == 2 && (! child.hasProperty(propSectionId) || ! child.hasProperty(propSectionName)
            || ! child.hasProperty(propSectionType))) return false;
        if (child.getNumChildren() != 0) return false;
        PluginChain::SectionDef def;
        double id = 0.0, gain = 1.0;
        bool bypassed = false;
        const auto type = child.getProperty(propSectionType, "stomp").toString();
        if (! number(child, propSectionId, 1.0, id) || id < 1 || id >= std::numeric_limits<int>::max() - 1.0 || std::floor(id) != id
            || ! number(child, propSectionGain, 1.0, gain) || gain < 0 || gain > std::numeric_limits<float>::max()
            || ! boolean(child, propSectionBypassed, false, bypassed)
            || (type != "stomp" && type != "preset")) return false;
        def.id = (int) id;
        def.name = child.getProperty(propSectionName, "Stomp 1").toString();
        def.type = type == "preset" ? PluginChain::SectionDef::Type::preset : PluginChain::SectionDef::Type::stomp;
        def.gain = (float) gain;
        def.bypassed = bypassed;
        if (def.name.isEmpty()) return false;
        for (const auto& existing : sections) if (existing.id == def.id) return false;
        sections.add(def);
    }

    // Older builds could write an empty v2 preset without a SECTION; slots still require one.
    if (sections.isEmpty())
    {
        if (version == 2 && tree.getNumChildren() != 0) return false;
        sections.add({ 1, "Stomp 1", PluginChain::SectionDef::Type::stomp });
    }

    // Read SLOT nodes.
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto slot = tree.getChild(i);
        if (! slot.hasType(slotType))
            continue;

        if (version == 2 && ! slot.hasProperty(propSectionId)) return false;
        PluginChain::SlotSpec spec;
        double sectionId = sections[0].id, slotId = 0.0, postGain = 1.0;
        if (! number(slot, propSectionId, sections[0].id, sectionId) || sectionId < 1 || sectionId > std::numeric_limits<int>::max() || std::floor(sectionId) != sectionId
            || ! number(slot, propSlotId, 0.0, slotId) || slotId < 0 || slotId >= std::numeric_limits<int>::max() - 1.0 || std::floor(slotId) != slotId
            || ! number(slot, propPostGain, 1.0, postGain) || postGain < 0 || postGain > std::numeric_limits<float>::max()
            || ! boolean(slot, propBypassed, false, spec.bypassed)
            || ! slot.getChildWithName(pluginTag).isValid()) return false;
        spec.sectionId = (int) sectionId;
        spec.slotId = (int) slotId;
        spec.postGain = (float) postGain;
        spec.customName = slot.getProperty(propCustomName, juce::String()).toString();
        for (const auto& existing : specs) if (spec.slotId > 0 && existing.slotId == spec.slotId) return false;
        bool knownSection = false;
        for (const auto& section : sections) knownSection |= section.id == spec.sectionId;
        if (! knownSection) return false;

        const auto stateStr = slot.getProperty(propState, juce::String()).toString();
        if (stateStr.isNotEmpty())
        {
            const auto dot = stateStr.indexOfChar('.');
            if (dot <= 0) return false;
            const auto lengthText = stateStr.substring(0, dot);
            if (lengthText.length() > 10 || (lengthText.length() > 1 && lengthText[0] == '0')) return false;
            size_t byteCount = 0;
            for (auto c : lengthText)
            {
                if (! juce::CharacterFunctions::isDigit(c)) return false;
                byteCount = byteCount * 10 + (size_t) (c - '0');
                if (byteCount > std::numeric_limits<int>::max()) return false;
            }
            const auto chars = stateStr.substring(dot + 1);
            const auto expected = (byteCount * 8 + 5) / 6;
            if ((size_t) chars.length() != expected) return false;
            for (auto c : chars)
                if (c != '.' && ! (c >= 'A' && c <= 'Z') && ! (c >= 'a' && c <= 'z')
                    && ! (c >= '0' && c <= '9') && c != '+' && c != '.') return false;
            if (! spec.state.fromBase64Encoding(stateStr) || spec.state.toBase64Encoding() != stateStr) return false;
        }

        if (slot.getNumChildren() != 1) return false;
        auto descTree = slot.getChildWithName(pluginTag);
        if (! validPluginMetadata(descTree)) return false;
        if (auto descXml = descTree.createXml())
        {
            if (! spec.description.loadFromXml(*descXml)) return false;
        }
        if (spec.description.name.isEmpty() || spec.description.pluginFormatName.isEmpty()
            || spec.description.fileOrIdentifier.isEmpty()) return false;
        bool hasIdentity = false;
        for (const auto& key : { propUniqueId, propUid })
        {
            if (! descTree.hasProperty(key)) continue;
            const auto identity = descTree.getProperty(key).toString();
            if (identity.isEmpty() || identity.length() > 8) return false;
            for (auto c : identity)
                if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
            hasIdentity = true;
        }
        if (! hasIdentity) return false;
        specs.add(spec);
    }

    if (! PluginChain::canAllocateSlotIds(specs, 1)) return false;

    outSpecs = specs;
    outSections = sections;
    return true;
}

bool fromValueTree(const juce::ValueTree& tree, juce::Array<PluginChain::SlotSpec>& outSpecs)
{
    juce::Array<PluginChain::SectionDef> ignoredSections;
    return fromValueTree(tree, outSpecs, ignoredSections);
}

bool saveToFile(const juce::Array<PluginChain::SlotSpec>& specs,
                const juce::Array<PluginChain::SectionDef>& sections,
                const juce::String& name,
                const juce::File& file,
                juce::String* error)
{
    auto tree = toValueTree(specs, sections, name);

    if (auto xml = tree.createXml())
    {
        const bool ok = xml->writeTo(file);
        if (! ok && error != nullptr) *error = "Could not write the preset file.";
        HostDebug::log("Preset save " + juce::String(ok ? "OK" : "FAILED") + ": " + file.getFullPathName());
        return ok;
    }

    if (error != nullptr) *error = "Could not create preset data.";
    return false;
}

bool loadFromFile(const juce::File& file,
                  juce::Array<PluginChain::SlotSpec>& outSpecs,
                  juce::Array<PluginChain::SectionDef>& outSections,
                  juce::String* error)
{
    if (! file.existsAsFile())
    {
        if (error != nullptr) *error = "Preset file does not exist.";
        HostDebug::log("Preset load FAILED (missing): " + file.getFullPathName());
        return false;
    }

    auto xml = juce::XmlDocument::parse(file);

    if (xml == nullptr)
    {
        if (error != nullptr) *error = "Preset file contains invalid XML.";
        HostDebug::log("Preset load FAILED (parse): " + file.getFullPathName());
        return false;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    juce::Array<PluginChain::SlotSpec> specs;
    juce::Array<PluginChain::SectionDef> sections;
    const bool ok = fromValueTree(tree, specs, sections);
    if (ok) { outSpecs = specs; outSections = sections; }
    else if (error != nullptr) *error = "Preset structure is invalid or its version is unsupported.";
    HostDebug::log("Preset load " + juce::String(ok ? "OK" : "FAILED") + ": " + file.getFullPathName()
                   + " (" + juce::String(outSpecs.size()) + " slot(s), "
                   + juce::String(outSections.size()) + " section(s))");
    return ok;
}

juce::File getPresetsDirectory()
{
    auto dir = AppDataDir::get().getChildFile("presets");
    dir.createDirectory();
    return dir;
}

} // namespace Preset
