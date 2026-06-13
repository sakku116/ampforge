#pragma once

#include <JuceHeader.h>
#include <vector>
#include "PluginChain.h"
#include "ControlMap.h"

/** A set of named templates. A template is a full pedalboard snapshot (the same SlotSpec list a
    preset stores). Switching templates is done by the owner via PluginHost crossfade/preload;
    this class only stores, orders and (de)serialises them. */
class TemplateManager
{
public:
    struct Scene
    {
        juce::String name;
        juce::Array<PluginChain::SlotSpec> specs;
        juce::Array<PluginChain::SectionDef> sections;
        ControlMap controlMap;
    };

    int getNumScenes() const { return (int) scenes.size(); }
    const Scene& getScene(int index) const { return scenes[(size_t) index]; }
    juce::StringArray getSceneNames() const;

    int  addScene(const juce::String& name,
                  juce::Array<PluginChain::SlotSpec> specs,
                  juce::Array<PluginChain::SectionDef> sections,
                  ControlMap controlMap = {});
    void replaceScene(int index,
                      juce::Array<PluginChain::SlotSpec> specs,
                      juce::Array<PluginChain::SectionDef> sections,
                      ControlMap controlMap = {});
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
