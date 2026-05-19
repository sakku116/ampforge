#include "PluginHost.h"
#include "HostDebug.h"
#include "PluginScanGuard.h"

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
            HostDebug::log("Plugin editor opened: " + instance.getName());
        }
        else
        {
            HostDebug::log("Plugin editor missing for: " + instance.getName());
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

void PluginHost::updateProcessingChannelCount()
{
    if (pluginInstance == nullptr)
    {
        processingChannelCount.store(0);
        return;
    }

    processingChannelCount.store(juce::jmax(pluginInstance->getTotalNumInputChannels(),
                                            pluginInstance->getTotalNumOutputChannels()));
}

int PluginHost::getProcessingChannelCount() const
{
    return processingChannelCount.load();
}

bool PluginHost::loadPlugin(const juce::PluginDescription& description, double sampleRate, int blockSize)
{
    juce::ScopedLock lock(pluginLock);

    HostDebug::log("Loading plugin: " + description.name
                   + " | file: " + description.fileOrIdentifier
                   + " | " + juce::String(sampleRate, 1) + " Hz, block " + juce::String(blockSize));

    auto loadOutcome = PluginScanGuard::createPluginInstance(formatManager,
                                                             description,
                                                             sampleRate,
                                                             blockSize);

    if (loadOutcome.crashed)
    {
        HostDebug::log("Load CRASHED: " + description.name + " — " + loadOutcome.error);
        return false;
    }

    auto created = std::move(loadOutcome.instance);

    if (created == nullptr)
    {
        HostDebug::log("Load FAILED: " + description.name + " — " + loadOutcome.error);
        return false;
    }

    if (pluginInstance != nullptr)
        pluginInstance->releaseResources();

    pluginInstance = std::move(created);

    pluginInstance->setPlayConfigDetails(currentInputChannels, currentOutputChannels, sampleRate, blockSize);
    pluginInstance->prepareToPlay(sampleRate, blockSize);

    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    updateProcessingChannelCount();

    HostDebug::log("Load OK: " + pluginInstance->getName()
                   + " | in=" + juce::String(pluginInstance->getTotalNumInputChannels())
                   + " out=" + juce::String(pluginInstance->getTotalNumOutputChannels())
                   + " | process channels=" + juce::String(processingChannelCount.load()));

    return true;
}

void PluginHost::unloadPlugin()
{
    juce::ScopedLock lock(pluginLock);

    if (pluginInstance != nullptr)
        HostDebug::log("Unloading plugin: " + pluginInstance->getName());

    editorWindow.reset();

    if (pluginInstance != nullptr)
    {
        pluginInstance->releaseResources();
        pluginInstance.reset();
    }

    processingChannelCount.store(0);
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

    HostDebug::log("Plugin prepare: " + juce::String(sampleRate, 1) + " Hz, block " + juce::String(blockSize)
                   + ", in=" + juce::String(currentInputChannels) + ", out=" + juce::String(currentOutputChannels));

    if (pluginInstance != nullptr)
    {
        pluginInstance->releaseResources();
        pluginInstance->setPlayConfigDetails(currentInputChannels, currentOutputChannels, sampleRate, blockSize);
        pluginInstance->prepareToPlay(sampleRate, blockSize);
        updateProcessingChannelCount();
        HostDebug::log("  prepared: " + pluginInstance->getName());
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
    {
        HostDebug::log("Open editor skipped: no plugin loaded");
        return;
    }

    editorWindow = std::make_unique<PluginEditorWindow>(*pluginInstance);
}
