#include "Preset.h"
#include "HostDebug.h"

namespace Preset
{
namespace
{
    const juce::Identifier presetType  ("TONEFORGE_PRESET");
    const juce::Identifier slotType    ("SLOT");
    const juce::Identifier pluginTag   ("PLUGIN");   // tag produced by PluginDescription::createXml()
    const juce::Identifier propName    ("name");
    const juce::Identifier propVersion ("version");
    const juce::Identifier propBypassed("bypassed");
    const juce::Identifier propState   ("state");
}

juce::ValueTree toValueTree(const juce::Array<PluginChain::SlotSpec>& specs, const juce::String& name)
{
    juce::ValueTree root(presetType);
    root.setProperty(propName, name, nullptr);
    root.setProperty(propVersion, 1, nullptr);

    for (const auto& spec : specs)
    {
        juce::ValueTree slot(slotType);
        slot.setProperty(propBypassed, spec.bypassed, nullptr);

        if (spec.state.getSize() > 0)
            slot.setProperty(propState, spec.state.toBase64Encoding(), nullptr);

        if (auto descXml = spec.description.createXml())
            slot.addChild(juce::ValueTree::fromXml(*descXml), -1, nullptr);

        root.addChild(slot, -1, nullptr);
    }

    return root;
}

bool fromValueTree(const juce::ValueTree& tree, juce::Array<PluginChain::SlotSpec>& outSpecs)
{
    if (! tree.hasType(presetType))
        return false;

    outSpecs.clear();

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto slot = tree.getChild(i);

        if (! slot.hasType(slotType))
            continue;

        PluginChain::SlotSpec spec;
        spec.bypassed = (bool) slot.getProperty(propBypassed, false);

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

bool saveToFile(const juce::Array<PluginChain::SlotSpec>& specs,
                const juce::String& name,
                const juce::File& file)
{
    auto tree = toValueTree(specs, name);

    if (auto xml = tree.createXml())
    {
        const bool ok = xml->writeTo(file);
        HostDebug::log("Preset save " + juce::String(ok ? "OK" : "FAILED") + ": " + file.getFullPathName());
        return ok;
    }

    return false;
}

bool loadFromFile(const juce::File& file, juce::Array<PluginChain::SlotSpec>& outSpecs)
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
    const bool ok = fromValueTree(tree, outSpecs);
    HostDebug::log("Preset load " + juce::String(ok ? "OK" : "FAILED") + ": " + file.getFullPathName()
                   + " (" + juce::String(outSpecs.size()) + " slot(s))");
    return ok;
}

juce::File getPresetsDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("GtrFxSim")
                   .getChildFile("presets");
    dir.createDirectory();
    return dir;
}

} // namespace Preset
