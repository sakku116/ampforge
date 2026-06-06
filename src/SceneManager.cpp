#include "SceneManager.h"
#include "Preset.h"

juce::StringArray SceneManager::getSceneNames() const
{
    juce::StringArray names;

    for (int i = 0; i < (int) scenes.size(); ++i)
        names.add(juce::String(i + 1) + ". " + scenes[(size_t) i].name);

    return names;
}

int SceneManager::addScene(const juce::String& name, juce::Array<PluginChain::SlotSpec> specs)
{
    scenes.push_back({ name, std::move(specs) });
    return (int) scenes.size() - 1;
}

void SceneManager::replaceScene(int index, juce::Array<PluginChain::SlotSpec> specs)
{
    if (juce::isPositiveAndBelow(index, (int) scenes.size()))
        scenes[(size_t) index].specs = std::move(specs);
}

void SceneManager::renameScene(int index, const juce::String& name)
{
    if (juce::isPositiveAndBelow(index, (int) scenes.size()))
        scenes[(size_t) index].name = name;
}

void SceneManager::removeScene(int index)
{
    if (! juce::isPositiveAndBelow(index, (int) scenes.size()))
        return;

    scenes.erase(scenes.begin() + index);

    if (currentIndex >= (int) scenes.size())
        currentIndex = (int) scenes.size() - 1;
}

void SceneManager::clear()
{
    scenes.clear();
    currentIndex = -1;
}

juce::ValueTree SceneManager::toValueTree() const
{
    juce::ValueTree root("SCENES");
    root.setProperty("current", currentIndex, nullptr);

    for (const auto& scene : scenes)
        root.addChild(Preset::toValueTree(scene.specs, scene.name), -1, nullptr);

    return root;
}

void SceneManager::fromValueTree(const juce::ValueTree& tree)
{
    clear();

    if (! tree.hasType("SCENES"))
        return;

    currentIndex = (int) tree.getProperty("current", -1);

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild(i);

        Scene scene;
        scene.name = child.getProperty("name", "Scene " + juce::String(i + 1)).toString();
        Preset::fromValueTree(child, scene.specs);
        scenes.push_back(std::move(scene));
    }

    if (currentIndex >= (int) scenes.size())
        currentIndex = (int) scenes.size() - 1;
}
