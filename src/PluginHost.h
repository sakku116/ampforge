#pragma once

#include <JuceHeader.h>

class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    bool loadPlugin(const juce::PluginDescription& description, double sampleRate, int blockSize);
    void unloadPlugin();

    bool hasLoadedPlugin() const;

    void prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels);
    void processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

    void openEditorWindow();

private:
    class PluginEditorWindow;

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
    std::unique_ptr<PluginEditorWindow> editorWindow;
    mutable juce::CriticalSection pluginLock;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    int currentInputChannels = 2;
    int currentOutputChannels = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
