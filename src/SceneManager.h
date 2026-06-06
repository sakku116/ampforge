#pragma once

#include <JuceHeader.h>
#include <vector>
#include "PluginChain.h"

/** A set of named scenes. A scene is a full pedalboard snapshot (the same SlotSpec list a
    preset stores). Switching scenes is done by the owner via PluginHost crossfade/preload;
    this class only stores, orders and (de)serialises them. */
class SceneManager
{
public:
    struct Scene
    {
        juce::String name;
        juce::Array<PluginChain::SlotSpec> specs;
        juce::Array<PluginChain::SectionDef> sections;
    };

    int getNumScenes() const { return (int) scenes.size(); }
    const Scene& getScene(int index) const { return scenes[(size_t) index]; }
    juce::StringArray getSceneNames() const;

    int  addScene(const juce::String& name,
                  juce::Array<PluginChain::SlotSpec> specs,
                  juce::Array<PluginChain::SectionDef> sections);
    void replaceScene(int index,
                      juce::Array<PluginChain::SlotSpec> specs,
                      juce::Array<PluginChain::SectionDef> sections);
    void renameScene(int index, const juce::String& name);
    void removeScene(int index);
    void clear();

    int  getCurrentIndex() const { return currentIndex; }
    void setCurrentIndex(int index) { currentIndex = index; }

    juce::ValueTree toValueTree() const;
    void fromValueTree(const juce::ValueTree& tree);

private:
    std::vector<Scene> scenes;
    int currentIndex = -1;
};
