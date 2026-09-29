#include "PluginHost.h"
#include "HostDebug.h"

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

PluginHost::PluginHost(PluginChain::InstanceFactory factory) : chain(formatManager, std::move(factory))
{
    formatManager.addDefaultFormats();
}

PluginHost::~PluginHost()
{
    closeAllEditors();
    chain.clear();
}

// ── Chain editing ────────────────────────────────────────────────────────────
bool PluginHost::addPlugin(const juce::PluginDescription& description)
{
    return chain.addPlugin(description);
}

bool PluginHost::addPlugin(const juce::PluginDescription& description, int sectionId)
{
    return chain.addPlugin(description, sectionId);
}

void PluginHost::removePlugin(int index)
{
    closeAllEditors();   // editors reference instances about to be deleted
    chain.removePlugin(index);
}

void PluginHost::movePlugin(int fromIndex, int toIndex, int sectionIdOverride)
{
    chain.movePlugin(fromIndex, toIndex, sectionIdOverride);
}

void PluginHost::setBypass(int index, bool shouldBypass)
{
    chain.setBypass(index, shouldBypass);
}

void PluginHost::clearChain()
{
    closeAllEditors();
    chain.clear();
}

bool PluginHost::rebuildChain(const juce::Array<PluginChain::SlotSpec>& specs)
{
    cancelPendingSwitch();
    const int handle = chain.preloadChain(specs);
    if (handle <= 0) return false;
    closeAllEditors();
    return chain.activateChain(handle, 0);
}

bool PluginHost::rebuildChain(const juce::Array<PluginChain::SlotSpec>& specs,
                              const juce::Array<PluginChain::SectionDef>& sections)
{
    cancelPendingSwitch();
    const int handle = chain.preloadChain(specs, sections);
    if (handle <= 0) return false;
    closeAllEditors();
    return chain.activateChain(handle, 0);
}

bool PluginHost::switchChainWithCrossfade(const juce::Array<PluginChain::SlotSpec>& specs, int crossfadeMs)
{
    const int handle = chain.preloadChain(specs);
    if (handle <= 0) return false;
    closeAllEditors();
    return chain.activateChain(handle, crossfadeMs);
}

bool PluginHost::switchChainWithCrossfade(const juce::Array<PluginChain::SlotSpec>& specs,
                                          const juce::Array<PluginChain::SectionDef>& sections,
                                          int crossfadeMs)
{
    const int handle = chain.preloadChain(specs, sections);
    if (handle <= 0) return false;
    closeAllEditors();
    return chain.activateChain(handle, crossfadeMs);
}

void PluginHost::switchChainAsync(const juce::Array<PluginChain::SlotSpec>& specs,
                                  const juce::Array<PluginChain::SectionDef>& sections,
                                  int crossfadeMs,
                                  std::function<void(bool)> onComplete)
{
    chain.buildChainAsync(
        specs, sections,
        nullptr,   // onProgress — MainComponent accesses chain directly via getChain()
        [this, crossfadeMs, onComplete](int handle, bool allOk)
        {
            bool activated = false;
            if (allOk && handle > 0)
            {
                closeAllEditors();
                activated = chain.activateChain(handle, crossfadeMs);
            }
            if (onComplete)
                onComplete(activated);
        });
}

void PluginHost::cancelPendingSwitch()
{
    chain.cancelAsyncBuild();
}

// ── Editor ─────────────────────────────────────────────────────────────────
void PluginHost::openEditorWindow(int index)
{
    auto* instance = chain.getInstance(index);

    if (instance == nullptr)
    {
        HostDebug::log("Open editor skipped: no plugin at slot " + juce::String(index));
        return;
    }

    // One editor window at a time; opening a new slot replaces the previous editor.
    editorWindow = std::make_unique<PluginEditorWindow>(*instance);
}

void PluginHost::closeAllEditors()
{
    editorWindow.reset();
}

// ── Backwards-compatible single-plugin API ───────────────────────────────────
bool PluginHost::loadPlugin(const juce::PluginDescription& description, double, int)
{
    // Legacy "load one plugin" == replace the chain with a single plugin.
    // Sample rate / block size already come from prepare(); params kept for call-site compat.
    clearChain();
    return chain.addPlugin(description);
}

void PluginHost::unloadPlugin()
{
    clearChain();
}

void PluginHost::setParameter(int slotIndex, int paramIndex, float value)
{
    if (auto* instance = chain.getInstance(slotIndex))
    {
        const auto& params = instance->getParameters();

        if (juce::isPositiveAndBelow(paramIndex, params.size()))
            params[paramIndex]->setValue(juce::jlimit(0.0f, 1.0f, value));
    }
}

int PluginHost::getParameterCount(int slotIndex) const
{
    if (auto* instance = chain.getInstance(slotIndex))
        return instance->getParameters().size();

    return 0;
}

// ── Audio ────────────────────────────────────────────────────────────────────
void PluginHost::prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels)
{
    chain.prepare(sampleRate, blockSize, inputChannels, outputChannels);
}

void PluginHost::processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    chain.processAudio(buffer, midiMessages);
}
