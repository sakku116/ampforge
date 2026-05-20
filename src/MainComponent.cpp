#include "MainComponent.h"
#include "HostDebug.h"

MainComponent::MainComponent()
    : audioEngine(pluginHost),
      pluginScanner(pluginHost.getFormatManager())
{
    initialiseSettings();

    setSize(960, 620);

    titleLabel.setText("Minimal JUCE VST3 Host", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    pluginListBox.setModel(this);
    addAndMakeVisible(pluginListBox);

    audioSettingsButton.addListener(this);
    scanButton.addListener(this);
    loadButton.addListener(this);
    openEditorButton.addListener(this);

    addAndMakeVisible(audioSettingsButton);
    addAndMakeVisible(scanButton);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(openEditorButton);

    audioEngine.start();
    tryRestoreAudioDeviceState();

    HostDebug::log("MainComponent ready — scheduling VST3 scan");

    juce::MessageManager::callAsync([this]
    {
        pluginScanner.scanDefaultWindowsVST3Folders();
        refreshPluginList();
        tryRestoreLastLoadedPlugin();
    });
}

MainComponent::~MainComponent()
{
    HostDebug::log("MainComponent destroying");

    audioSettingsButton.removeListener(this);
    scanButton.removeListener(this);
    loadButton.removeListener(this);
    openEditorButton.removeListener(this);

    audioSettingsWindow.reset();
    pluginHost.unloadPlugin();
    saveAudioDeviceState();
    audioEngine.stop();
    appProperties.saveIfNeeded();
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    titleLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(8);

    auto buttonRow = area.removeFromTop(36);
    audioSettingsButton.setBounds(buttonRow.removeFromLeft(160));
    buttonRow.removeFromLeft(8);
    scanButton.setBounds(buttonRow.removeFromLeft(190));
    buttonRow.removeFromLeft(8);
    loadButton.setBounds(buttonRow.removeFromLeft(200));
    buttonRow.removeFromLeft(8);
    openEditorButton.setBounds(buttonRow.removeFromLeft(200));

    area.removeFromTop(8);
    pluginListBox.setBounds(area);
}

int MainComponent::getNumRows()
{
    return pluginNames.size();
}

void MainComponent::paintListBoxItem(int rowNumber,
                                     juce::Graphics& g,
                                     int width,
                                     int height,
                                     bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue.withAlpha(0.5f));

    if (juce::isPositiveAndBelow(rowNumber, pluginNames.size()))
    {
        g.setColour(juce::Colours::white);
        g.drawText(pluginNames[rowNumber], 6, 0, width - 12, height, juce::Justification::centredLeft);
    }
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &audioSettingsButton)
    {
        openAudioSettings();
        return;
    }

    if (button == &scanButton)
    {
        HostDebug::log("UI: Scan VST3 clicked");
        pluginScanner.scanDefaultWindowsVST3Folders();
        refreshPluginList();
        return;
    }

    if (button == &loadButton)
    {
        auto selected = pluginListBox.getSelectedRow();

        if (!juce::isPositiveAndBelow(selected, pluginScanner.getKnownPluginList().getNumTypes()))
        {
            HostDebug::log("UI: Load clicked — no valid selection (row " + juce::String(selected) + ")");
            return;
        }

        auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();

        if (device == nullptr)
        {
            HostDebug::log("UI: Load clicked — no audio device");
            return;
        }

        auto description = pluginScanner.getKnownPluginList().getTypes()[selected];
        HostDebug::log("UI: Load clicked — " + description.name);

        if (pluginHost.loadPlugin(description,
                                  device->getCurrentSampleRate(),
                                  device->getCurrentBufferSizeSamples()))
        {
            saveLastLoadedPlugin(description);
        }

        return;
    }

    if (button == &openEditorButton)
    {
        HostDebug::log("UI: Open editor clicked");
        pluginHost.openEditorWindow();
    }
}

void MainComponent::refreshPluginList()
{
    pluginNames.clear();

    for (const auto& type : pluginScanner.getKnownPluginList().getTypes())
        pluginNames.add(type.name + " [" + type.pluginFormatName + "]");

    HostDebug::log("Plugin list refreshed: " + juce::String(pluginNames.size()) + " row(s)");

    pluginListBox.updateContent();
    pluginListBox.repaint();
}

void MainComponent::initialiseSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "GtrFxSim";
    options.filenameSuffix = "settings";
    options.folderName = "GtrFxSim";
    options.storageFormat = juce::PropertiesFile::storeAsXML;

    appProperties.setStorageParameters(options);
}

void MainComponent::tryRestoreLastLoadedPlugin()
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    auto lastPluginIdentifier = settings->getValue(lastPluginIdentifierKey);

    if (lastPluginIdentifier.isEmpty())
    {
        HostDebug::log("Restore plugin: none saved");
        return;
    }

    HostDebug::log("Restore plugin: looking for " + lastPluginIdentifier);

    auto* device = audioEngine.getDeviceManager().getCurrentAudioDevice();

    if (device == nullptr)
    {
        HostDebug::log("Restore plugin: aborted — no audio device");
        return;
    }

    const auto& types = pluginScanner.getKnownPluginList().getTypes();

    for (int i = 0; i < types.size(); ++i)
    {
        const auto& type = types.getReference(i);

        if (type.fileOrIdentifier != lastPluginIdentifier)
            continue;

        if (pluginHost.loadPlugin(type,
                                  device->getCurrentSampleRate(),
                                  device->getCurrentBufferSizeSamples()))
        {
            pluginListBox.selectRow(i);
            HostDebug::log("Restore plugin: OK — " + type.name);
        }
        else
        {
            HostDebug::log("Restore plugin: FAILED — " + type.name);
        }

        return;
    }

    HostDebug::log("Restore plugin: identifier not found in scanned list");
}

void MainComponent::saveLastLoadedPlugin(const juce::PluginDescription& description)
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    settings->setValue(lastPluginIdentifierKey, description.fileOrIdentifier);
    settings->saveIfNeeded();
}

void MainComponent::openAudioSettings()
{
    if (audioSettingsWindow != nullptr)
    {
        audioSettingsWindow->toFront(true);
        return;
    }

    audioSettingsWindow = std::make_unique<AudioSettingsWindow>(
        audioEngine.getDeviceManager(),
        [this]
        {
            saveAudioDeviceState();
            audioSettingsWindow.reset();
        });
}

void MainComponent::saveAudioDeviceState()
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    if (auto stateXml = audioEngine.saveDeviceState())
        settings->setValue(audioDeviceStateKey, stateXml.get());

    settings->saveIfNeeded();
}

void MainComponent::tryRestoreAudioDeviceState()
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    if (auto stateXml = settings->getXmlValue(audioDeviceStateKey))
        audioEngine.restoreDeviceState(stateXml.get());
}
