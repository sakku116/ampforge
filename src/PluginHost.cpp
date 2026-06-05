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

PluginHost::PluginHost() : chain(formatManager)
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

void PluginHost::removePlugin(int index)
{
    closeAllEditors();   // editors reference instances about to be deleted
    chain.removePlugin(index);
}

void PluginHost::movePlugin(int fromIndex, int toIndex)
{
    chain.movePlugin(fromIndex, toIndex);   // instances persist, editors stay valid
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
    closeAllEditors();
    return chain.rebuildFrom(specs);
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

// ── Audio ────────────────────────────────────────────────────────────────────
void PluginHost::prepare(double sampleRate, int blockSize, int inputChannels, int outputChannels)
{
    chain.prepare(sampleRate, blockSize, inputChannels, outputChannels);
}

void PluginHost::processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    chain.processAudio(buffer, midiMessages);
}
