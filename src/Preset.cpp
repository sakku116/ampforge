#include "Preset.h"
#include "AppDataDir.h"
#include "HostDebug.h"

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
}

juce::ValueTree toValueTree(const juce::Array<PluginChain::SlotSpec>& specs,
                             const juce::Array<PluginChain::SectionDef>& sections,
                             const juce::String& name)
{
    juce::ValueTree root(presetType);
    root.setProperty(propName, name, nullptr);
    root.setProperty(propVersion, 2, nullptr);

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

    outSpecs.clear();
    outSections.clear();

    // Read SECTION nodes.
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild(i);
        if (! child.hasType(sectionType))
            continue;

        PluginChain::SectionDef def;
        def.id   = (int) child.getProperty(propSectionId, 1);
        def.name = child.getProperty(propSectionName, "Stomp 1").toString();
        def.type = child.getProperty(propSectionType, "stomp").toString() == "preset"
                   ? PluginChain::SectionDef::Type::preset
                   : PluginChain::SectionDef::Type::stomp;
        def.gain     = (float) (double) child.getProperty(propSectionGain, 1.0);
        def.bypassed = (bool) child.getProperty(propSectionBypassed, false);
        outSections.add(def);
    }

    // v1 migration: no SECTION nodes → synthesise a single "Stomp 1" section.
    if (outSections.isEmpty())
        outSections.add({ 1, "Stomp 1", PluginChain::SectionDef::Type::stomp });

    // Read SLOT nodes.
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto slot = tree.getChild(i);
        if (! slot.hasType(slotType))
            continue;

        PluginChain::SlotSpec spec;
        spec.bypassed   = (bool) slot.getProperty(propBypassed, false);
        spec.customName = slot.getProperty(propCustomName, juce::String()).toString();
        spec.sectionId  = (int) slot.getProperty(propSectionId, outSections[0].id);
        spec.slotId     = (int) slot.getProperty(propSlotId, 0);
        spec.postGain   = (float) (double) slot.getProperty(propPostGain, 1.0);

        const auto stateStr = slot.getProperty(propState, juce::String()).toString();
        if (stateStr.isNotEmpty())
            spec.state.fromBase64Encoding(stateStr);

        auto descTree = slot.getChildWithName(pluginTag);
        if (descTree.isValid())
            if (auto descXml = descTree.createXml())
                spec.description.loadFromXml(*descXml);

        outSpecs.add(spec);
    }

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
                const juce::File& file)
{
    auto tree = toValueTree(specs, sections, name);

    if (auto xml = tree.createXml())
    {
        const bool ok = xml->writeTo(file);
        HostDebug::log("Preset save " + juce::String(ok ? "OK" : "FAILED") + ": " + file.getFullPathName());
        return ok;
    }

    return false;
}

bool loadFromFile(const juce::File& file,
                  juce::Array<PluginChain::SlotSpec>& outSpecs,
                  juce::Array<PluginChain::SectionDef>& outSections)
{
    if (! file.existsAsFile())
    {
        HostDebug::log("Preset load FAILED (missing): " + file.getFullPathName());
        return false;
    }

    auto xml = juce::XmlDocument::parse(file);

    if (xml == nullptr)
    {
        HostDebug::log("Preset load FAILED (parse): " + file.getFullPathName());
        return false;
    }

    auto tree = juce::ValueTree::fromXml(*xml);
    const bool ok = fromValueTree(tree, outSpecs, outSections);
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
