#include "TemplateManager.h"
#include "Preset.h"

juce::StringArray TemplateManager::getSceneNames() const
{
    juce::StringArray names;

    for (int i = 0; i < (int) scenes.size(); ++i)
        names.add(juce::String(i + 1) + ". " + scenes[(size_t) i].name);

    return names;
}

int TemplateManager::addScene(const juce::String& name,
                               juce::Array<PluginChain::SlotSpec> specs,
                               juce::Array<PluginChain::SectionDef> sections,
                               ControlMap controlMap)
{
    scenes.push_back({ name, std::move(specs), std::move(sections), std::move(controlMap) });
    return (int) scenes.size() - 1;
}

void TemplateManager::replaceScene(int index,
                                    juce::Array<PluginChain::SlotSpec> specs,
                                    juce::Array<PluginChain::SectionDef> sections,
                                    ControlMap controlMap)
{
    if (juce::isPositiveAndBelow(index, (int) scenes.size()))
    {
        scenes[(size_t) index].specs      = std::move(specs);
        scenes[(size_t) index].sections   = std::move(sections);
        scenes[(size_t) index].controlMap = std::move(controlMap);
    }
}

void TemplateManager::renameScene(int index, const juce::String& name)
{
    if (juce::isPositiveAndBelow(index, (int) scenes.size()))
        scenes[(size_t) index].name = name;
}

void TemplateManager::removeScene(int index)
{
    if (! juce::isPositiveAndBelow(index, (int) scenes.size()))
        return;

    scenes.erase(scenes.begin() + index);

    if (currentIndex >= (int) scenes.size())
        currentIndex = (int) scenes.size() - 1;
}

void TemplateManager::clear()
{
    scenes.clear();
    currentIndex = -1;
}

ControlMap TemplateManager::getCurrentControlMapOr(const ControlMap& legacyMap) const
{
    if (juce::isPositiveAndBelow(currentIndex, (int) scenes.size()))
        return scenes[(size_t) currentIndex].controlMap;

    return legacyMap;
}

juce::ValueTree TemplateManager::toValueTree() const
{
    juce::ValueTree root("SCENES");
    root.setProperty("current", currentIndex, nullptr);

    for (const auto& scene : scenes)
    {
        auto sceneNode = Preset::toValueTree(scene.specs, scene.sections, scene.name);
        sceneNode.addChild(scene.controlMap.toValueTree(), -1, nullptr);
        root.addChild(sceneNode, -1, nullptr);
    }

    return root;
}

bool TemplateManager::fromValueTree(const juce::ValueTree& tree)
{
    if (! tree.hasType("SCENES")) return false;

    std::vector<Scene> restoredScenes;
    int restoredCurrentIndex = -1;
    if (tree.hasProperty("current"))
    {
        const auto current = tree.getProperty("current").toString();
        if (current.isEmpty() || current != current.trim() || ! current.containsOnly("-0123456789"))
            return false;
        restoredCurrentIndex = current.getIntValue();
        if (current != juce::String(restoredCurrentIndex))
            return false;
    }

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild(i);
        if (! child.hasType("TONEFORGE_PRESET"))
            return false;

        Scene scene;
        scene.name = child.getProperty("name", "Template " + juce::String(i + 1)).toString();
        auto chain = child.createCopy();
        for (int j = chain.getNumChildren(); --j >= 0;)
            if (chain.getChild(j).hasType("CONTROLMAP")) chain.removeChild(j, nullptr);
        if (! Preset::fromValueTree(chain, scene.specs, scene.sections))
            return false;

        bool foundMap = false;
        for (int j = 0; j < child.getNumChildren(); ++j)
        {
            const auto sub = child.getChild(j);
            if (sub.hasType("CONTROLMAP"))
            {
                if (foundMap || ! scene.controlMap.fromValueTree(sub)) return false;
                foundMap = true;
            }
            else if (! sub.hasType("SECTION") && ! sub.hasType("SLOT"))
                return false;
        }

        restoredScenes.push_back(std::move(scene));
    }

    if (restoredCurrentIndex < -1 || restoredCurrentIndex >= (int) restoredScenes.size())
        return false;

    scenes = std::move(restoredScenes);
    currentIndex = restoredCurrentIndex;
    return true;
}
