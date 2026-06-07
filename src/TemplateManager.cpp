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
                               juce::Array<PluginChain::SectionDef> sections)
{
    scenes.push_back({ name, std::move(specs), std::move(sections) });
    return (int) scenes.size() - 1;
}

void TemplateManager::replaceScene(int index,
                                    juce::Array<PluginChain::SlotSpec> specs,
                                    juce::Array<PluginChain::SectionDef> sections)
{
    if (juce::isPositiveAndBelow(index, (int) scenes.size()))
    {
        scenes[(size_t) index].specs    = std::move(specs);
        scenes[(size_t) index].sections = std::move(sections);
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

juce::ValueTree TemplateManager::toValueTree() const
{
    juce::ValueTree root("SCENES");
    root.setProperty("current", currentIndex, nullptr);

    for (const auto& scene : scenes)
        root.addChild(Preset::toValueTree(scene.specs, scene.sections, scene.name), -1, nullptr);

    return root;
}

void TemplateManager::fromValueTree(const juce::ValueTree& tree)
{
    clear();

    if (! tree.hasType("SCENES"))
        return;

    currentIndex = (int) tree.getProperty("current", -1);

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild(i);

        Scene scene;
        scene.name = child.getProperty("name", "Template " + juce::String(i + 1)).toString();
        Preset::fromValueTree(child, scene.specs, scene.sections);
        scenes.push_back(std::move(scene));
    }

    if (currentIndex >= (int) scenes.size())
        currentIndex = (int) scenes.size() - 1;
}
