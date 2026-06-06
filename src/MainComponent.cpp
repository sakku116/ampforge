#include "MainComponent.h"
#include "HostDebug.h"
#include "Preset.h"

MainComponent::MainComponent()
    : audioEngine(pluginHost),
      pluginScanner(pluginHost.getFormatManager())
{
    initialiseSettings();

    setSize(960, 620);
    setWantsKeyboardFocus(true);   // receive key presses for keyboard/footswitch mapping

    titleLabel.setText("ToneForge - VST3 Pedalboard", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    metricsLabel.setFont(juce::FontOptions(13.0f));
    metricsLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    metricsLabel.setJustificationType(juce::Justification::centredRight);
    metricsLabel.setText("CPU --  DSP --  Latency --", juce::dontSendNotification);
    addAndMakeVisible(metricsLabel);

    paletteLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    chainLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    addAndMakeVisible(paletteLabel);
    addAndMakeVisible(chainLabel);

    paletteListBox.setModel(&paletteModel);
    chainListBox.setModel(&chainModel);
    addAndMakeVisible(paletteListBox);
    addAndMakeVisible(chainListBox);

    chainModel.onDoubleClick = [this](int row) { pluginHost.openEditorWindow(row); };

    for (auto* b : { &audioSettingsButton, &scanButton, &addButton, &removeButton,
                     &upButton, &downButton, &bypassButton, &editorButton,
                     &savePresetButton, &loadPresetButton,
                     &captureSceneButton, &updateSceneButton, &deleteSceneButton,
                     &prevSceneButton, &nextSceneButton })
    {
        b->addListener(this);
        addAndMakeVisible(*b);
    }

    sceneSelector.setTextWhenNothingSelected("(no scenes)");
    sceneSelector.onChange = [this]
    {
        const int idx = sceneSelector.getSelectedId() - 1;   // ids are index+1
        if (idx >= 0 && idx != sceneManager.getCurrentIndex())
            recallScene(idx);
    };
    addAndMakeVisible(sceneSelector);

    audioEngine.start();
    tryRestoreAudioDeviceState();

    audioEngine.onMidiForControl = [this](const juce::MidiMessage& m) { handleControlMidi(m); };

    startTimerHz(10);   // performance metrics refresh

    HostDebug::log("MainComponent ready — scheduling VST3 scan");

    juce::MessageManager::callAsync([this]
    {
        pluginScanner.scanDefaultWindowsVST3Folders();
        refreshPaletteList();
        tryRestoreLastPreset();
        restoreScenes();
    });
}

MainComponent::~MainComponent()
{
    HostDebug::log("MainComponent destroying");

    stopTimer();

    for (auto* b : { &audioSettingsButton, &scanButton, &addButton, &removeButton,
                     &upButton, &downButton, &bypassButton, &editorButton,
                     &savePresetButton, &loadPresetButton,
                     &captureSceneButton, &updateSceneButton, &deleteSceneButton,
                     &prevSceneButton, &nextSceneButton })
        b->removeListener(this);

    audioSettingsWindow.reset();
    saveScenes();
    pluginHost.clearChain();
    saveAudioDeviceState();
    audioEngine.stop();
    appProperties.saveIfNeeded();
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto titleRow = area.removeFromTop(36);
    metricsLabel.setBounds(titleRow.removeFromRight(380));
    titleLabel.setBounds(titleRow);
    area.removeFromTop(8);

    auto topRow = area.removeFromTop(36);
    audioSettingsButton.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(8);
    scanButton.setBounds(topRow.removeFromLeft(160));
    topRow.removeFromLeft(8);
    addButton.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(8);
    savePresetButton.setBounds(topRow.removeFromLeft(120));
    topRow.removeFromLeft(8);
    loadPresetButton.setBounds(topRow.removeFromLeft(120));

    area.removeFromTop(8);

    auto sceneRow = area.removeFromTop(34);
    sceneSelector.setBounds(sceneRow.removeFromLeft(220));
    sceneRow.removeFromLeft(8);
    captureSceneButton.setBounds(sceneRow.removeFromLeft(120));
    sceneRow.removeFromLeft(6);
    updateSceneButton.setBounds(sceneRow.removeFromLeft(110));
    sceneRow.removeFromLeft(6);
    deleteSceneButton.setBounds(sceneRow.removeFromLeft(110));
    sceneRow.removeFromLeft(6);
    prevSceneButton.setBounds(sceneRow.removeFromLeft(80));
    sceneRow.removeFromLeft(6);
    nextSceneButton.setBounds(sceneRow.removeFromLeft(80));

    area.removeFromTop(10);

    // Chain action buttons live in a row at the bottom.
    auto bottomRow = area.removeFromBottom(36);
    for (auto* b : { &editorButton, &bypassButton, &removeButton, &downButton, &upButton })
    {
        b->setBounds(bottomRow.removeFromRight(120));
        bottomRow.removeFromRight(8);
    }

    area.removeFromBottom(8);

    // Two columns: palette (left) and chain (right).
    auto labelRow = area.removeFromTop(22);
    paletteLabel.setBounds(labelRow.removeFromLeft(area.getWidth() / 2));
    chainLabel.setBounds(labelRow);

    auto columns = area;
    auto left = columns.removeFromLeft(columns.getWidth() / 2);
    left.removeFromRight(6);
    columns.removeFromLeft(6);

    paletteListBox.setBounds(left);
    chainListBox.setBounds(columns);
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &audioSettingsButton) { openAudioSettings(); return; }

    if (button == &scanButton)
    {
        HostDebug::log("UI: Scan VST3 clicked");
        pluginScanner.scanDefaultWindowsVST3Folders();
        refreshPaletteList();
        return;
    }

    if (button == &addButton)    { addSelectedToChain();      return; }
    if (button == &removeButton) { removeSelectedChainSlot(); return; }
    if (button == &upButton)     { moveSelectedChainSlot(-1); return; }
    if (button == &downButton)   { moveSelectedChainSlot(+1); return; }
    if (button == &bypassButton) { toggleSelectedBypass();    return; }
    if (button == &editorButton) { openSelectedEditor();      return; }
    if (button == &savePresetButton) { savePreset();          return; }
    if (button == &loadPresetButton) { loadPreset();          return; }
    if (button == &captureSceneButton) { captureScene();      return; }
    if (button == &updateSceneButton)  { updateScene();       return; }
    if (button == &deleteSceneButton)  { deleteScene();       return; }
    if (button == &prevSceneButton)    { stepScene(-1);       return; }
    if (button == &nextSceneButton)    { stepScene(+1);       return; }
}

void MainComponent::handleControlMidi(const juce::MidiMessage& message)
{
    if (midiLearnArmed.load())
    {
        if (message.isController() && message.getControllerValue() < 64)
            return;   // wait for the footswitch/button "on", not the release

        const auto trigger = ControlMap::triggerFromMidi(message);

        if (! trigger.isValid())
            return;   // ignore non-mappable messages while learning

        const auto action = pendingLearnAction;
        midiLearnArmed.store(false);

        juce::MessageManager::callAsync([this, trigger, action]
        {
            controlMap.addBinding({ trigger, action });
            HostDebug::log("MIDI learn: bound " + trigger.toString() + " -> " + action.toString());
        });
        return;
    }

    const auto action = controlMap.matchMidi(message);

    if (! action.isValid())
        return;

    juce::MessageManager::callAsync([this, action] { executeAction(action); });
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();

    if (midiLearnArmed.load())
    {
        ControlTrigger trigger;
        trigger.type = ControlTrigger::Type::key;
        trigger.number = code;

        const auto action = pendingLearnAction;
        midiLearnArmed.store(false);
        controlMap.addBinding({ trigger, action });
        HostDebug::log("Key learn: bound " + trigger.toString() + " -> " + action.toString());
        return true;
    }

    const auto action = controlMap.matchKey(code);

    if (! action.isValid())
        return false;   // let unhandled keys propagate

    executeAction(action);   // already on the message thread
    return true;
}

void MainComponent::executeAction(const ControlAction& action)
{
    switch (action.type)
    {
        case ControlAction::Type::toggleBypass:
            if (juce::isPositiveAndBelow(action.index, pluginHost.getNumSlots()))
            {
                const auto infos = pluginHost.getSlotInfos();
                pluginHost.setBypass(action.index, ! infos.getReference(action.index).bypassed);
                refreshChainList();
            }
            break;

        case ControlAction::Type::nextScene:  stepScene(+1);            break;
        case ControlAction::Type::prevScene:  stepScene(-1);            break;
        case ControlAction::Type::loadScene:  recallScene(action.index); break;

        case ControlAction::Type::none:
        default:
            break;
    }
}

void MainComponent::captureScene()
{
    const auto name = "Scene " + juce::String(sceneManager.getNumScenes() + 1);
    const int idx = sceneManager.addScene(name, pluginHost.captureChain());
    sceneManager.setCurrentIndex(idx);
    refreshSceneSelector();
    saveScenes();
    HostDebug::log("Scene captured: " + name + " (" + juce::String(pluginHost.getNumSlots()) + " slots)");
}

void MainComponent::updateScene()
{
    const int idx = sceneManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, sceneManager.getNumScenes()))
        return;

    sceneManager.replaceScene(idx, pluginHost.captureChain());
    saveScenes();
    HostDebug::log("Scene updated: index " + juce::String(idx));
}

void MainComponent::deleteScene()
{
    const int idx = sceneManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, sceneManager.getNumScenes()))
        return;

    sceneManager.removeScene(idx);
    refreshSceneSelector();
    saveScenes();
    HostDebug::log("Scene deleted: index " + juce::String(idx));
}

void MainComponent::recallScene(int index)
{
    if (! juce::isPositiveAndBelow(index, sceneManager.getNumScenes()))
        return;

    sceneManager.setCurrentIndex(index);
    pluginHost.switchChainWithCrossfade(sceneManager.getScene(index).specs, 25);
    refreshChainList();
    refreshSceneSelector();
    HostDebug::log("Scene recalled: index " + juce::String(index));
}

void MainComponent::stepScene(int delta)
{
    const int n = sceneManager.getNumScenes();

    if (n == 0)
        return;

    int idx = sceneManager.getCurrentIndex();
    idx = idx < 0 ? 0 : (idx + delta + n) % n;   // wrap around
    recallScene(idx);
}

void MainComponent::refreshSceneSelector()
{
    sceneSelector.clear(juce::dontSendNotification);

    const auto names = sceneManager.getSceneNames();

    for (int i = 0; i < names.size(); ++i)
        sceneSelector.addItem(names[i], i + 1);   // ids are index+1

    const int cur = sceneManager.getCurrentIndex();

    if (juce::isPositiveAndBelow(cur, names.size()))
        sceneSelector.setSelectedId(cur + 1, juce::dontSendNotification);
}

void MainComponent::saveScenes()
{
    if (auto* settings = appProperties.getUserSettings())
    {
        if (auto xml = sceneManager.toValueTree().createXml())
            settings->setValue(scenesStateKey, xml.get());

        settings->saveIfNeeded();
    }
}

void MainComponent::restoreScenes()
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    if (auto xml = settings->getXmlValue(scenesStateKey))
    {
        sceneManager.fromValueTree(juce::ValueTree::fromXml(*xml));
        refreshSceneSelector();
        HostDebug::log("Scenes restored: " + juce::String(sceneManager.getNumScenes()));
    }
}

void MainComponent::timerCallback()
{
    juce::String text;
    text << "CPU " << juce::String(audioEngine.getCpuUsagePercent(), 0) << "%"
         << "   DSP " << juce::String(audioEngine.getDspLoadPercent(), 0) << "%"
         << "   Lat " << juce::String(audioEngine.getRoundTripLatencyMs(), 1) << " ms"
         << "  (" << juce::String((int) audioEngine.getCurrentSampleRate()) << " Hz / "
         << juce::String(audioEngine.getCurrentBlockSize()) << ")";

    metricsLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::refreshPaletteList()
{
    paletteNames.clear();

    for (const auto& type : pluginScanner.getKnownPluginList().getTypes())
        paletteNames.add(type.name + " [" + type.pluginFormatName + "]");

    HostDebug::log("Palette refreshed: " + juce::String(paletteNames.size()) + " plugin(s)");

    paletteListBox.updateContent();
    paletteListBox.repaint();
}

void MainComponent::refreshChainList()
{
    chainNames.clear();

    const auto infos = pluginHost.getSlotInfos();

    for (int i = 0; i < infos.size(); ++i)
    {
        const auto& info = infos.getReference(i);
        juce::String label;
        label << (i + 1) << ". " << info.name << " [" << info.format << "]";

        if (info.bypassed)
            label << "   (BYPASSED)";

        chainNames.add(label);
    }

    chainListBox.updateContent();
    chainListBox.repaint();
}

int MainComponent::getSelectedPaletteRow() const
{
    return paletteListBox.getSelectedRow();
}

int MainComponent::getSelectedChainRow() const
{
    return chainListBox.getSelectedRow();
}

void MainComponent::addSelectedToChain()
{
    const auto row = getSelectedPaletteRow();
    const auto& types = pluginScanner.getKnownPluginList().getTypes();

    if (! juce::isPositiveAndBelow(row, types.size()))
    {
        HostDebug::log("UI: Add clicked — no palette selection");
        return;
    }

    const auto description = types[row];
    HostDebug::log("UI: Add to chain — " + description.name);

    if (pluginHost.addPlugin(description))
    {
        refreshChainList();
        chainListBox.selectRow(pluginHost.getNumSlots() - 1);
    }
}

void MainComponent::moveSelectedChainSlot(int delta)
{
    const auto row = getSelectedChainRow();
    const auto target = row + delta;

    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots())
        || ! juce::isPositiveAndBelow(target, pluginHost.getNumSlots()))
        return;

    pluginHost.movePlugin(row, target);
    refreshChainList();
    chainListBox.selectRow(target);
}

void MainComponent::toggleSelectedBypass()
{
    const auto row = getSelectedChainRow();

    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots()))
        return;

    const auto infos = pluginHost.getSlotInfos();
    pluginHost.setBypass(row, ! infos.getReference(row).bypassed);
    refreshChainList();
    chainListBox.selectRow(row);
}

void MainComponent::removeSelectedChainSlot()
{
    const auto row = getSelectedChainRow();

    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots()))
        return;

    HostDebug::log("UI: Remove chain slot " + juce::String(row));
    pluginHost.removePlugin(row);
    refreshChainList();
}

void MainComponent::openSelectedEditor()
{
    const auto row = getSelectedChainRow();

    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots()))
    {
        HostDebug::log("UI: Open editor — no chain selection");
        return;
    }

    pluginHost.openEditorWindow(row);
}

void MainComponent::savePreset()
{
    if (pluginHost.getNumSlots() == 0)
    {
        HostDebug::log("UI: Save preset — chain is empty");
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>(
        "Save Preset",
        Preset::getPresetsDirectory(),
        juce::String("*") + Preset::fileExtension);

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        file = file.withFileExtension(Preset::fileExtension);

        const auto specs = pluginHost.captureChain();

        if (Preset::saveToFile(specs, file.getFileNameWithoutExtension(), file))
            saveLastPresetPath(file);
    });
}

void MainComponent::loadPreset()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Preset",
        Preset::getPresetsDirectory(),
        juce::String("*") + Preset::fileExtension);

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        loadPresetFile(file);
    });
}

void MainComponent::loadPresetFile(const juce::File& file)
{
    juce::Array<PluginChain::SlotSpec> specs;

    if (! Preset::loadFromFile(file, specs))
        return;

    pluginHost.switchChainWithCrossfade(specs, 25);   // smooth, click-free preset switch
    refreshChainList();
    saveLastPresetPath(file);
    HostDebug::log("Preset applied: " + file.getFileName());
}

void MainComponent::saveLastPresetPath(const juce::File& file)
{
    if (auto* settings = appProperties.getUserSettings())
    {
        settings->setValue(lastPresetPathKey, file.getFullPathName());
        settings->saveIfNeeded();
    }
}

void MainComponent::tryRestoreLastPreset()
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    const auto path = settings->getValue(lastPresetPathKey);

    if (path.isEmpty())
    {
        HostDebug::log("Restore preset: none saved");
        return;
    }

    const juce::File file(path);

    if (! file.existsAsFile())
    {
        HostDebug::log("Restore preset: file missing — " + path);
        return;
    }

    HostDebug::log("Restore preset: loading " + path);
    loadPresetFile(file);
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
