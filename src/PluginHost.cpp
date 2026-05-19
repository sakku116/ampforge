#include "PluginHost.h"

class PluginHost::PluginEditorWindow : public juce::DocumentWindow
{
public:
    explicit PluginEditorWindow(juce::AudioPluginInstance& instance)
        : juce::DocumentWindow(instance.getName() + " Editor",
                               juce::Colours::darkgrey,
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);

        auto* editor = instance.createEditorIfNeeded();

        if (editor != nullptr)
        {
            setContentOwned(editor, true);
            centreWithSize(editor->getWidth(), editor->getHeight());
        }

        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

PluginHost::PluginHost()
{
    formatManager.addDefaultFormats();
}

PluginHost::~PluginHost()
{
    unloadPlugin();
}

bool PluginHost::loadPlugin(const juce::PluginDescription& description, double sampleRate, int blockSize)
{
    juce::ScopedLock lock(pluginLock);

    juce::String error;

    auto created = formatManager.createPluginInstance(description, sampleRate, blockSize, error);

    if (created == nullptr)
    {
        juce::Logger::writeToLog("Failed to load plugin: " + error);
        return false;
    }

    if (pluginInstance != nullptr)
        pluginInstance->releaseResources();

    pluginInstance = std::move(created);

    pluginInstance->setPlayConfigDetails(currentInputChannels, currentOutputChannels, sampleRate, blockSize);
    pluginInstance->prepareToPlay(sampleRate, blockSize);

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;

    return true;
}

void PluginHost::unloadPlugin()
{
    juce::ScopedLock lock(pluginLock);

    editorWindow.reset();

    if (pluginInstance != nullptr)
    {
        pluginInstance->releaseResources();
        pluginInstance.reset();
    }
}

bool PluginHost::hasLoadedPlugin() const
{
    juce::ScopedLock lock(pluginLock);
    return pluginInstance != nullptr;
}

void PluginHost::prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels)
{
    juce::ScopedLock lock(pluginLock);

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    currentInputChannels = juce::jmax(1, inputChannels);
    currentOutputChannels = juce::jmax(1, outputChannels);

    if (pluginInstance != nullptr)
    {
        pluginInstance->releaseResources();
        pluginInstance->setPlayConfigDetails(currentInputChannels, currentOutputChannels, sampleRate, blockSize);
        pluginInstance->prepareToPlay(sampleRate, blockSize);
    }
}

void PluginHost::processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedLock lock(pluginLock);

    if (pluginInstance == nullptr)
        return;

    pluginInstance->processBlock(buffer, midiMessages);
}

void PluginHost::openEditorWindow()
{
    juce::ScopedLock lock(pluginLock);

    if (pluginInstance == nullptr)
        return;

    editorWindow = std::make_unique<PluginEditorWindow>(*pluginInstance);
}
