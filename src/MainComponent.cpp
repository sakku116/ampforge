#include "MainComponent.h"
#include "HostDebug.h"
#include "Preset.h"

MainComponent::MainComponent()
    : audioEngine(pluginHost),
      pluginScanner(pluginHost.getFormatManager())
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    setLookAndFeel(&lookAndFeel);

    initialiseSettings();

    setSize(1000, 660);
    setWantsKeyboardFocus(true);   // receive key presses for keyboard/footswitch mapping

    titleLabel.setText("ToneForge", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, tf::colour::text);
    addAndMakeVisible(titleLabel);

    metricsLabel.setFont(juce::FontOptions(13.0f));
    metricsLabel.setColour(juce::Label::textColourId, tf::colour::accent);
    metricsLabel.setJustificationType(juce::Justification::centredRight);
    metricsLabel.setText("CPU --  DSP --  Latency --", juce::dontSendNotification);
    addAndMakeVisible(metricsLabel);

    // Section headers: small, bold, accent-coloured.
    for (auto* l : { &paletteLabel, &chainLabel, &sceneLabel, &controlSectionLabel })
    {
        l->setFont(juce::FontOptions(12.5f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, tf::colour::accent);
        addAndMakeVisible(*l);
    }

    paletteListBox.setModel(&paletteModel);
    paletteListBox.setRowHeight(26);
    paletteListBox.setColour(juce::ListBox::backgroundColourId, tf::colour::surface);
    chainListBox.setModel(&chainModel);
    chainListBox.setRowHeight(52);
    chainListBox.setColour(juce::ListBox::backgroundColourId, tf::colour::surface);
    addAndMakeVisible(paletteListBox);
    addAndMakeVisible(chainListBox);

    // Double-click a library item to add it; the chain rows carry their own buttons.
    paletteModel.onDoubleClick = [this](int) { addSelectedToChain(); };

    chainModel.onBypass    = [this](int r) { toggleBypassAt(r); };
    chainModel.onMoveUp   = [this](int r) { moveSlotAt(r, -1); };
    chainModel.onMoveDown = [this](int r) { moveSlotAt(r, +1); };
    chainModel.onRemove   = [this](int r) { removeSlotAt(r); };
    chainModel.onEditor   = [this](int r) { openEditorAt(r); };
    chainModel.onSelect   = [this](int r) { chainListBox.selectRow(r); };

    chainModel.onRename = [this](int r)
    {
        const auto infos = pluginHost.getSlotInfos();
        if (! juce::isPositiveAndBelow(r, infos.size()))
            return;

        const auto& info = infos.getReference(r);
        const juce::String current = info.hasCustomName ? info.name : juce::String{};

        auto* dialog = new juce::AlertWindow("Rename Slot",
                                             "Enter a new name for \"" + info.originalName + "\":",
                                             juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", current, "Name:");
        dialog->addButton("OK",     1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, r, dialog](int result)
            {
                if (result == 1)
                {
                    const juce::String newName = dialog->getTextEditorContents("name").trim();
                    pluginHost.renameSlot(r, newName);
                    refreshChainList();
                    setSceneDirty(sceneManager.getCurrentIndex() >= 0);
                }
                delete dialog;
            }), true);
    };

    chainModel.onResetName = [this](int r)
    {
        pluginHost.renameSlot(r, {});
        refreshChainList();
        setSceneDirty(sceneManager.getCurrentIndex() >= 0);
    };

    // Scene action buttons: icon text + tooltip so their purpose is clear at a glance.
    captureSceneButton.setButtonText(juce::String::fromUTF8("\xE2\x8A\x95"));   // ⊕
    updateSceneButton .setButtonText(juce::String::fromUTF8("\xE2\x86\x91"));   // ↑
    renameSceneButton .setButtonText(juce::String::fromUTF8("\xE2\x9C\x8E"));   // ✎
    deleteSceneButton .setButtonText(juce::String::fromUTF8("\xE2\x9C\x95"));   // ✕
    prevSceneButton   .setButtonText(juce::String::fromUTF8("\xE2\x97\x80"));   // ◀
    nextSceneButton   .setButtonText(juce::String::fromUTF8("\xE2\x96\xB6"));   // ▶

    captureSceneButton.setTooltip("Capture — save current chain as a new scene");
    updateSceneButton .setTooltip("Update — overwrite active scene with current chain");
    renameSceneButton .setTooltip("Rename — change the name of the active scene");
    deleteSceneButton .setTooltip("Delete — remove the active scene");
    prevSceneButton   .setTooltip("Previous scene");
    nextSceneButton   .setTooltip("Next scene");

    for (auto* b : { &audioSettingsButton, &scanButton, &addButton,
                     &savePresetButton, &loadPresetButton,
                     &captureSceneButton, &updateSceneButton, &renameSceneButton,
                     &deleteSceneButton, &prevSceneButton, &nextSceneButton,
                     &learnBypassButton, &learnExprButton, &learnSceneNextButton,
                     &learnScenePrevButton, &clearMapsButton })
    {
        b->addListener(this);
        addAndMakeVisible(*b);
    }

    // Primary action stands out; destructive actions read as danger.
    addButton.setColour(juce::TextButton::buttonColourId, tf::colour::accent);
    addButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    captureSceneButton.setColour(juce::TextButton::textColourOffId, tf::colour::accent);
    renameSceneButton .setColour(juce::TextButton::textColourOffId, tf::colour::textDim);
    deleteSceneButton .setColour(juce::TextButton::textColourOffId, tf::colour::danger);
    clearMapsButton.setColour(juce::TextButton::textColourOffId, tf::colour::danger);

    sceneSelector.setTextWhenNothingSelected("(no scenes)");
    sceneSelector.onChange = [this]
    {
        const int idx = sceneSelector.getSelectedId() - 1;   // ids are index+1
        if (idx >= 0 && idx != sceneManager.getCurrentIndex())
            recallScene(idx);
    };
    addAndMakeVisible(sceneSelector);

    controlLabel.setFont(juce::FontOptions(13.0f));
    controlLabel.setColour(juce::Label::textColourId, tf::colour::warn);
    controlLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(controlLabel);

    sceneDirtyLabel.setText(juce::String::fromUTF8("\xe2\x97\x8f modified"), juce::dontSendNotification);
    sceneDirtyLabel.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    sceneDirtyLabel.setColour(juce::Label::textColourId, tf::colour::warn);
    sceneDirtyLabel.setJustificationType(juce::Justification::centredLeft);
    sceneDirtyLabel.setVisible(false);
    addAndMakeVisible(sceneDirtyLabel);

    audioEngine.start();
    tryRestoreAudioDeviceState();

    audioEngine.onMidiForControl = [this](const juce::MidiMessage& m) { handleControlMidi(m); };

    startTimerHz(10);   // performance metrics refresh

    HostDebug::log("MainComponent ready — scheduling VST3 scan");

    juce::MessageManager::callAsync([this]
    {
        pluginScanner.scanDefaultWindowsVST3Folders();
        refreshPaletteList();

        // Restore scenes first so we know whether an active scene should own the chain.
        restoreScenes();
        restoreControlMap();

        const int activeScene = sceneManager.getCurrentIndex();

        if (juce::isPositiveAndBelow(activeScene, sceneManager.getNumScenes()))
        {
            // Active scene takes priority: rebuild immediately (no crossfade from empty chain).
            pluginHost.rebuildChain(sceneManager.getScene(activeScene).specs);
            refreshChainList();
            HostDebug::log("Startup: applied scene " + juce::String(activeScene));
        }
        else
        {
            // No active scene — fall back to last saved preset.
            tryRestoreLastPreset();
        }
    });
}

MainComponent::~MainComponent()
{
    HostDebug::log("MainComponent destroying");

    stopTimer();

    for (auto* b : { &audioSettingsButton, &scanButton, &addButton,
                     &savePresetButton, &loadPresetButton,
                     &captureSceneButton, &updateSceneButton, &renameSceneButton,
                     &deleteSceneButton, &prevSceneButton, &nextSceneButton,
                     &learnBypassButton, &learnExprButton, &learnSceneNextButton,
                     &learnScenePrevButton, &clearMapsButton })
        b->removeListener(this);

    audioSettingsWindow.reset();
    saveScenes();
    saveControlMap();
    pluginHost.clearChain();
    saveAudioDeviceState();
    audioEngine.stop();
    appProperties.saveIfNeeded();

    setLookAndFeel(nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(tf::colour::background);

    auto drawPanel = [&g](juce::Rectangle<int> r)
    {
        if (r.isEmpty())
            return;

        g.setColour(tf::colour::surface);
        g.fillRoundedRectangle(r.toFloat(), 9.0f);
        g.setColour(tf::colour::outline);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 9.0f, 1.0f);
    };

    drawPanel(headerPanel);
    drawPanel(libraryPanel);
    drawPanel(chainPanel);
    drawPanel(footerPanel);

    // Accent flourish under the title.
    if (! headerPanel.isEmpty())
    {
        const auto s = headerPanel.toFloat();
        g.setColour(tf::colour::accent);
        g.fillRect(s.getX() + 16.0f, s.getBottom() - 8.0f, 44.0f, 3.0f);
    }
}

void MainComponent::resized()
{
    const int pad = 14, gap = 10, rowH = 34, footerH = 100;
    auto area = getLocalBounds().reduced(pad);

    // ── Header bar (title + live metrics) ─────────────────────────────────────
    headerPanel = area.removeFromTop(52);
    {
        auto h = headerPanel.reduced(16, 0);
        metricsLabel.setBounds(h.removeFromRight(340));
        titleLabel.setBounds(h);
    }
    area.removeFromTop(gap);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    auto toolbar = area.removeFromTop(rowH);
    audioSettingsButton.setBounds(toolbar.removeFromLeft(140));
    toolbar.removeFromLeft(8);
    scanButton.setBounds(toolbar.removeFromLeft(130));
    area.removeFromTop(gap);

    // ── Footer panel (scenes + control), pinned to the bottom ─────────────────
    footerPanel = area.removeFromBottom(footerH);
    {
        auto f = footerPanel.reduced(14, 12);

        auto sceneRow = f.removeFromTop(rowH);
        sceneLabel.setBounds(sceneRow.removeFromLeft(70));
        sceneSelector.setBounds(sceneRow.removeFromLeft(200));
        sceneRow.removeFromLeft(8);
        sceneDirtyLabel.setBounds(sceneRow.removeFromLeft(88));
        sceneRow.removeFromLeft(6);

        // Icon buttons: ⊕ capture  ↑ update  ✎ rename  ✕ delete  |  ◀ prev  ▶ next
        for (auto* b : { &captureSceneButton, &updateSceneButton,
                         &renameSceneButton,  &deleteSceneButton })
        {
            b->setBounds(sceneRow.removeFromLeft(34));
            sceneRow.removeFromLeft(4);
        }

        nextSceneButton.setBounds(sceneRow.removeFromRight(34));
        sceneRow.removeFromRight(4);
        prevSceneButton.setBounds(sceneRow.removeFromRight(34));

        f.removeFromTop(8);

        auto controlRow = f.removeFromTop(rowH);
        controlSectionLabel.setBounds(controlRow.removeFromLeft(70));
        learnBypassButton.setBounds(controlRow.removeFromLeft(108));
        controlRow.removeFromLeft(6);
        learnExprButton.setBounds(controlRow.removeFromLeft(126));
        controlRow.removeFromLeft(6);
        learnSceneNextButton.setBounds(controlRow.removeFromLeft(106));
        controlRow.removeFromLeft(6);
        learnScenePrevButton.setBounds(controlRow.removeFromLeft(106));
        controlRow.removeFromLeft(6);
        clearMapsButton.setBounds(controlRow.removeFromLeft(92));
        controlRow.removeFromLeft(12);
        controlLabel.setBounds(controlRow);
    }
    area.removeFromBottom(gap);

    // ── Content: LIBRARY panel | SIGNAL CHAIN panel ───────────────────────────
    auto content = area;
    libraryPanel = content.removeFromLeft((content.getWidth() - gap) / 2);
    content.removeFromLeft(gap);
    chainPanel = content;

    {
        auto in = libraryPanel.reduced(12);
        paletteLabel.setBounds(in.removeFromTop(20));
        in.removeFromTop(6);
        addButton.setBounds(in.removeFromBottom(rowH));
        in.removeFromBottom(8);
        paletteListBox.setBounds(in);
    }

    {
        auto in = chainPanel.reduced(12);
        auto headRow = in.removeFromTop(28);
        loadPresetButton.setBounds(headRow.removeFromRight(78));
        headRow.removeFromRight(6);
        savePresetButton.setBounds(headRow.removeFromRight(78));
        chainLabel.setBounds(headRow.withTrimmedTop(4));
        in.removeFromTop(6);
        chainListBox.setBounds(in);
    }
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
    if (button == &savePresetButton) { savePreset();          return; }
    if (button == &loadPresetButton) { loadPreset();          return; }
    if (button == &captureSceneButton) { captureScene();        return; }
    if (button == &updateSceneButton)  { updateScene();         return; }
    if (button == &renameSceneButton)  { renameCurrentScene();  return; }
    if (button == &deleteSceneButton)  { deleteScene();         return; }
    if (button == &prevSceneButton)    { stepScene(-1);         return; }
    if (button == &nextSceneButton)    { stepScene(+1);         return; }

    if (button == &learnBypassButton)
    {
        armActionLearn({ ControlAction::Type::toggleBypass, juce::jmax(0, getSelectedChainRow()) });
        return;
    }
    if (button == &learnExprButton)    { armExpressionLearn(juce::jmax(0, getSelectedChainRow()), 0); return; }
    if (button == &learnSceneNextButton) { armActionLearn({ ControlAction::Type::nextScene, 0 }); return; }
    if (button == &learnScenePrevButton) { armActionLearn({ ControlAction::Type::prevScene, 0 }); return; }
    if (button == &clearMapsButton)    { clearMappings();     return; }
}

void MainComponent::handleControlMidi(const juce::MidiMessage& message)
{
    if (expressionLearnArmed.load())
    {
        if (! message.isController())
            return;   // wait for a CC (expression) message

        const int cc = message.getControllerNumber();
        const int chan = message.getChannel();
        const int slot = pendingExprSlot;
        const int param = pendingExprParam;
        expressionLearnArmed.store(false);

        juce::MessageManager::callAsync([this, chan, cc, slot, param]
        {
            controlMap.addExpression({ chan, cc, slot, param });
            saveControlMap();
            HostDebug::log("Expression learn: CC " + juce::String(cc)
                           + " -> slot " + juce::String(slot + 1) + " param " + juce::String(param));
        });
        return;
    }

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
            saveControlMap();
            HostDebug::log("MIDI learn: bound " + trigger.toString() + " -> " + action.toString());
        });
        return;
    }

    // Expression pedals: apply continuous CC -> parameter directly (high message rate).
    for (const auto& target : controlMap.matchExpressions(message))
        pluginHost.setParameter(target.slotIndex, target.paramIndex, target.value);

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
        saveControlMap();
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
            toggleBypassAt(action.index);
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
    setSceneDirty(false);
    HostDebug::log("Scene captured: " + name + " (" + juce::String(pluginHost.getNumSlots()) + " slots)");
}

void MainComponent::updateScene()
{
    const int idx = sceneManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, sceneManager.getNumScenes()))
        return;

    sceneManager.replaceScene(idx, pluginHost.captureChain());
    saveScenes();
    setSceneDirty(false);
    HostDebug::log("Scene updated: index " + juce::String(idx));
}

void MainComponent::renameCurrentScene()
{
    const int idx = sceneManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, sceneManager.getNumScenes()))
        return;

    const juce::String current = sceneManager.getScene(idx).name;

    auto* dialog = new juce::AlertWindow("Rename Scene",
                                         "Enter a new name for this scene:",
                                         juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", current, "Name:");
    dialog->addButton("OK",     1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, idx, dialog](int result)
        {
            if (result == 1)
            {
                const juce::String newName = dialog->getTextEditorContents("name").trim();
                if (newName.isNotEmpty())
                {
                    sceneManager.renameScene(idx, newName);
                    refreshSceneSelector();
                    saveScenes();
                    HostDebug::log("Scene renamed: index " + juce::String(idx) + " -> \"" + newName + "\"");
                }
            }
            delete dialog;
        }), true);
}

void MainComponent::deleteScene()
{
    const int idx = sceneManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, sceneManager.getNumScenes()))
        return;

    sceneManager.removeScene(idx);
    refreshSceneSelector();
    saveScenes();
    setSceneDirty(false);
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
    setSceneDirty(false);
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

void MainComponent::armActionLearn(const ControlAction& action)
{
    pendingLearnAction = action;
    expressionLearnArmed.store(false);
    midiLearnArmed.store(true);
    updateControlLabel();
    HostDebug::log("Learn armed: " + action.toString());
}

void MainComponent::armExpressionLearn(int slotIndex, int paramIndex)
{
    pendingExprSlot = slotIndex;
    pendingExprParam = paramIndex;
    midiLearnArmed.store(false);
    expressionLearnArmed.store(true);
    updateControlLabel();
    HostDebug::log("Expression learn armed: slot " + juce::String(slotIndex + 1) + " param " + juce::String(paramIndex));
}

void MainComponent::clearMappings()
{
    controlMap.clear();
    midiLearnArmed.store(false);
    expressionLearnArmed.store(false);
    saveControlMap();
    updateControlLabel();
    HostDebug::log("Control mappings cleared");
}

void MainComponent::updateControlLabel()
{
    juce::String text;

    if (midiLearnArmed.load())
        text = "LEARN: press a MIDI control or key for [" + pendingLearnAction.toString() + "]";
    else if (expressionLearnArmed.load())
        text = "LEARN EXPR: move a CC for slot " + juce::String(pendingExprSlot + 1)
             + " param " + juce::String(pendingExprParam);
    else
        text = "Maps: " + juce::String(controlMap.getNumBindings()) + " control, "
             + juce::String(controlMap.getNumExpressions()) + " expression";

    controlLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::setSceneDirty(bool dirty)
{
    sceneDirty = dirty;
    sceneDirtyLabel.setVisible(dirty);

    if (dirty)
    {
        updateSceneButton.setColour(juce::TextButton::buttonColourId, tf::colour::warn.withAlpha(0.25f));
        updateSceneButton.setColour(juce::TextButton::textColourOffId, tf::colour::warn);
    }
    else
    {
        updateSceneButton.removeColour(juce::TextButton::buttonColourId);
        updateSceneButton.removeColour(juce::TextButton::textColourOffId);
    }
}

void MainComponent::saveControlMap()
{
    if (auto* settings = appProperties.getUserSettings())
    {
        if (auto xml = controlMap.toValueTree().createXml())
            settings->setValue(controlMapStateKey, xml.get());

        settings->saveIfNeeded();
    }
}

void MainComponent::restoreControlMap()
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    if (auto xml = settings->getXmlValue(controlMapStateKey))
    {
        controlMap.fromValueTree(juce::ValueTree::fromXml(*xml));
        updateControlLabel();
        HostDebug::log("Control map restored: " + juce::String(controlMap.getNumBindings()) + " binding(s)");
    }
}

void MainComponent::timerCallback()
{
    updateControlLabel();

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
    const int selected = chainListBox.getSelectedRow();

    chainModel.setInfos(pluginHost.getSlotInfos());
    chainListBox.updateContent();

    if (juce::isPositiveAndBelow(selected, chainModel.getInfos().size()))
        chainListBox.selectRow(selected, juce::dontSendNotification);

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

        if (sceneManager.getCurrentIndex() >= 0)
            setSceneDirty(true);
    }
}

void MainComponent::moveSlotAt(int row, int delta)
{
    const auto target = row + delta;

    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots())
        || ! juce::isPositiveAndBelow(target, pluginHost.getNumSlots()))
        return;

    pluginHost.movePlugin(row, target);
    refreshChainList();
    chainListBox.selectRow(target);

    if (sceneManager.getCurrentIndex() >= 0)
        setSceneDirty(true);
}

void MainComponent::toggleBypassAt(int row)
{
    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots()))
        return;

    const auto infos = pluginHost.getSlotInfos();
    pluginHost.setBypass(row, ! infos.getReference(row).bypassed);
    refreshChainList();
    chainListBox.selectRow(row);

    if (sceneManager.getCurrentIndex() >= 0)
        setSceneDirty(true);
}

void MainComponent::removeSlotAt(int row)
{
    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots()))
        return;

    HostDebug::log("UI: Remove chain slot " + juce::String(row));
    pluginHost.removePlugin(row);
    refreshChainList();

    if (sceneManager.getCurrentIndex() >= 0)
        setSceneDirty(true);
}

void MainComponent::openEditorAt(int row)
{
    if (! juce::isPositiveAndBelow(row, pluginHost.getNumSlots()))
    {
        HostDebug::log("UI: Open editor — invalid slot " + juce::String(row));
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

    if (sceneManager.getCurrentIndex() >= 0)
        setSceneDirty(true);
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
