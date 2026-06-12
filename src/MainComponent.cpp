#include "MainComponent.h"
#include "AppDataDir.h"
#include "HostDebug.h"
#include "Preset.h"
#include <algorithm>

MainComponent::MainComponent()
    : audioEngine(pluginHost),
      pluginScanner(pluginHost.getFormatManager())
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    setLookAndFeel(&lookAndFeel);

    initialiseSettings();

    setSize(1000, 660);
    setWantsKeyboardFocus(true);   // receive key presses for keyboard/footswitch mapping

    titleLabel.setText("Amp Forge", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, tf::colour::text);
    addAndMakeVisible(titleLabel);

    metricsLabel.setFont(juce::FontOptions(13.0f));
    metricsLabel.setColour(juce::Label::textColourId, tf::colour::accent);
    metricsLabel.setJustificationType(juce::Justification::centredRight);
    metricsLabel.setText("CPU --  DSP --  Latency --", juce::dontSendNotification);
    addAndMakeVisible(metricsLabel);

    // Section headers: small, bold, accent-coloured.
    for (auto* l : { &paletteLabel, &chainLabel, &templatesLabel, &controlSectionLabel, &masterLabel })
    {
        l->setFont(juce::FontOptions(12.5f, juce::Font::bold));
        l->setColour(juce::Label::textColourId, tf::colour::accent);
        addAndMakeVisible(*l);
    }

    paletteListBox.setModel(&paletteModel);
    paletteListBox.setRowHeight(26);
    paletteListBox.setColour(juce::ListBox::backgroundColourId, tf::colour::surface);
    chainListBox.setRowHeight(46);
    chainListBox.setColour(juce::ListBox::backgroundColourId, tf::colour::surface);
    addAndMakeVisible(paletteListBox);
    addAndMakeVisible(chainListBox);

    chainHorizontalView.setVisible(false);
    chainHorizontalView.onMovePlugin = [this](int f, int t, int s) { moveSlotAt(f, t, s); };
    addAndMakeVisible(chainHorizontalView);

    chainViewToggleButton.setTooltip("Toggle horizontal / vertical chain view");
    chainViewToggleButton.addListener(this);
    addAndMakeVisible(chainViewToggleButton);

    chainLoadingLabel.setJustificationType(juce::Justification::centred);
    chainLoadingLabel.setFont(juce::FontOptions(13.5f, juce::Font::bold));
    chainLoadingLabel.setColour(juce::Label::backgroundColourId, tf::colour::background.withAlpha(0.88f));
    chainLoadingLabel.setColour(juce::Label::textColourId, tf::colour::text);
    chainLoadingLabel.setVisible(false);
    addAndMakeVisible(chainLoadingLabel);

    formatFilterCombo.addItem("All",  1);
    formatFilterCombo.addItem("VST3", 2);
    formatFilterCombo.addItem("VST2", 3);
    formatFilterCombo.setSelectedId(1, juce::dontSendNotification);
    formatFilterCombo.setTooltip("Filter by plugin format");
    formatFilterCombo.onChange = [this] { refreshPaletteList(); };
    addAndMakeVisible(formatFilterCombo);

    searchBox.setTextToShowWhenEmpty("Search...", juce::Colours::grey);
    searchBox.setFont(juce::FontOptions(13.0f));
    searchBox.onTextChange = [this] { refreshPaletteList(); };
    addAndMakeVisible(searchBox);

    // Double-click a library item to add it; the chain rows carry their own buttons.
    paletteModel.onDoubleClick = [this](int) { addSelectedToChain(); };

    chainModel.onBypass    = [this](int si) { clearChainSelection(); toggleBypassAt(si); };
    chainModel.onRemove    = [this](int si) { clearChainSelection(); removeSlotAt(si); };
    chainModel.onDuplicate = [this](int si) { clearChainSelection(); duplicateSlotAt(si); };
    chainListBox.onMovePlugin = [this](int from, int to, int sid) { moveSlotAt(from, to, sid); };
    chainModel.onEditor    = [this](int si) { openEditorAt(si); };
    chainModel.onSelect    = [this](int row) {
        const int prevRow = chainModel.highlightedRow;
        chainModel.highlightedRow = row;
        auto repaintChainRow = [this](int r)
        {
            if (r < 0) return;
            juce::Component* comp = chainHorizontalMode
                ? chainHorizontalView.getComponentForRowNumber(r)
                : chainListBox.getComponentForRowNumber(r);
            if (comp) comp->repaint();
        };
        repaintChainRow(row);
        if (prevRow != row) repaintChainRow(prevRow);
    };

    chainModel.onRename = [this](int si)
    {
        const auto infos = pluginHost.getSlotInfos();
        if (! juce::isPositiveAndBelow(si, infos.size()))
            return;

        const auto& info = infos.getReference(si);
        const juce::String current = info.hasCustomName ? info.name : juce::String{};

        auto* dialog = new juce::AlertWindow("Rename Slot",
                                             "Enter a new name for \"" + info.originalName + "\":",
                                             juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", current, "Name:");
        dialog->addButton("OK",     1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        dialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, si, dialog](int result)
            {
                if (result == 1)
                {
                    const juce::String newName = dialog->getTextEditorContents("name").trim();
                    pluginHost.renameSlot(si, newName);
                    refreshChainList();
                    setTemplateDirty(templateManager.getCurrentIndex() >= 0);
                }
                delete dialog;
            }), true);
    };

    chainModel.onResetName = [this](int si)
    {
        pluginHost.renameSlot(si, {});
        refreshChainList();
        setTemplateDirty(templateManager.getCurrentIndex() >= 0);
    };

    chainModel.onLearnControl = [this](int si)
    {
        const auto infos = pluginHost.getSlotInfos();
        if (! juce::isPositiveAndBelow(si, infos.size()))
            return;
        const auto& info = infos.getReference(si);
        const auto actionType = info.isPreset ? ControlAction::Type::activatePresetSlot
                                              : ControlAction::Type::toggleBypass;
        armActionLearn({ actionType, info.slotId });
    };

    chainModel.onRemoveControl = [this](int si)
    {
        const auto infos = pluginHost.getSlotInfos();
        if (! juce::isPositiveAndBelow(si, infos.size()))
            return;
        const int slotId = infos.getReference(si).slotId;

        for (int b = controlMap.getNumBindings() - 1; b >= 0; --b)
        {
            const auto& binding = controlMap.getBinding(b);
            if (binding.action.index == slotId &&
                (binding.action.type == ControlAction::Type::toggleBypass ||
                 binding.action.type == ControlAction::Type::activatePresetSlot))
                controlMap.removeBinding(b);
        }
        saveControlMap();
        updateControlLabel();
        refreshChainList();
        setTemplateDirty(templateManager.getCurrentIndex() >= 0);
    };

    chainModel.onSectionMoveUp   = [this](int id) { clearChainSelection(); moveSectionUpAt(id); };
    chainModel.onSectionMoveDown = [this](int id) { clearChainSelection(); moveSectionDownAt(id); };
    chainModel.onSectionRemove   = [this](int id) { clearChainSelection(); removeSectionAt(id); };
    chainModel.onSectionRename   = [this](int id) { renameSectionAt(id); };
    chainModel.onSectionBypass   = [this](int id, bool bypassed)
    {
        clearChainSelection();
        pluginHost.setSectionBypassed(id, bypassed);
        refreshChainList();
        setTemplateDirty(templateManager.getCurrentIndex() >= 0);
    };

    chainModel.onSlotGainChanged = [this](int slotIndex, float linearGain)
    {
        pluginHost.setSlotGain(slotIndex, linearGain);
        setTemplateDirty(templateManager.getCurrentIndex() >= 0);
    };

    chainModel.onSectionGainChanged = [this](int sectionId, float linearGain)
    {
        pluginHost.setSectionGain(sectionId, linearGain);
        setTemplateDirty(templateManager.getCurrentIndex() >= 0);
    };

    chainModel.onGetSectionLevel = [this](int sectionId) -> float
    {
        return pluginHost.getSectionPeakLevel(sectionId);
    };

    // Scene action buttons: icon text + tooltip so their purpose is clear at a glance.
    captureTemplateButton.setButtonText(juce::String::fromUTF8("\xE2\x8A\x95"));   // ⊕
    updateTemplateButton .setButtonText(juce::String::fromUTF8("\xE2\x86\x91"));   // ↑
    renameTemplateButton .setButtonText(juce::String::fromUTF8("\xE2\x9C\x8E"));   // ✎
    deleteTemplateButton .setButtonText(juce::String::fromUTF8("\xE2\x9C\x95"));   // ✕
    prevTemplateButton   .setButtonText(juce::String::fromUTF8("\xE2\x97\x80"));   // ◀
    nextTemplateButton   .setButtonText(juce::String::fromUTF8("\xE2\x96\xB6"));   // ▶

    captureTemplateButton.setTooltip("Capture — save current chain as a new template");
    updateTemplateButton .setTooltip("Update — overwrite active template with current chain");
    renameTemplateButton .setTooltip("Rename — change the name of the active template");
    deleteTemplateButton .setTooltip("Delete — remove the active template");
    prevTemplateButton   .setTooltip("Previous template");
    nextTemplateButton   .setTooltip("Next template");

    for (auto* b : { &audioSettingsButton, &scanButton, &scanPathsButton, &addButton,
                     &savePresetButton, &loadPresetButton,
                     &captureTemplateButton, &updateTemplateButton, &renameTemplateButton,
                     &deleteTemplateButton, &prevTemplateButton, &nextTemplateButton,
                     &addSectionStompButton, &addSectionPresetButton,
                     &learnExprButton,
                     &clearMapsButton })
    {
        b->addListener(this);
        addAndMakeVisible(*b);
    }

    // Primary action stands out; destructive actions read as danger.
    addButton.setColour(juce::TextButton::buttonColourId, tf::colour::accent);
    addButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    captureTemplateButton.setColour(juce::TextButton::textColourOffId, tf::colour::accent);
    renameTemplateButton .setColour(juce::TextButton::textColourOffId, tf::colour::textDim);
    deleteTemplateButton .setColour(juce::TextButton::textColourOffId, tf::colour::danger);
    clearMapsButton.setColour(juce::TextButton::textColourOffId, tf::colour::danger);

    templateSelector.setTextWhenNothingSelected("(no templates)");
    templateSelector.onChange = [this]
    {
        const int idx = templateSelector.getSelectedId() - 1;   // ids are index+1
        if (idx >= 0 && idx != templateManager.getCurrentIndex())
            recallTemplate(idx);
    };
    addAndMakeVisible(templateSelector);

    controlLabel.setFont(juce::FontOptions(13.0f));
    controlLabel.setColour(juce::Label::textColourId, tf::colour::warn);
    controlLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(controlLabel);

    templateDirtyLabel.setText(juce::String::fromUTF8("\xe2\x97\x8f modified"), juce::dontSendNotification);
    templateDirtyLabel.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    templateDirtyLabel.setColour(juce::Label::textColourId, tf::colour::warn);
    templateDirtyLabel.setJustificationType(juce::Justification::centredLeft);
    templateDirtyLabel.setVisible(false);
    addAndMakeVisible(templateDirtyLabel);

    // Master control
    for (auto* l : { &inputGainLabel, &outputVolLabel })
    {
        l->setFont(juce::FontOptions(12.5f));
        l->setColour(juce::Label::textColourId, tf::colour::textDim);
        l->setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(*l);
    }

    auto setupSlider = [this](juce::Slider& s, float defaultVal, std::function<void()> onChange)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        s.setRange(0.0, 2.0, 0.0);
        s.setValue(defaultVal, juce::dontSendNotification);
        s.onValueChange = std::move(onChange);
        addAndMakeVisible(s);
    };

    setupSlider(inputGainSlider,  1.0f, [this] { audioEngine.setMasterInputGain((float) inputGainSlider.getValue()); });
    setupSlider(outputVolSlider,  1.0f, [this] { audioEngine.setMasterOutputGain((float) outputVolSlider.getValue()); });

    inputGainSlider.setDoubleClickReturnValue(true, 1.0);
    inputGainSlider.setTooltip("Master input gain (0–2) — double-click to reset to unity");
    outputVolSlider.setDoubleClickReturnValue(true, 1.0);
    outputVolSlider.setTooltip("Master output volume (0–2) — double-click to reset to unity");

    muteButton.setTooltip("Mute / unmute master output");
    muteButton.onClick = [this]
    {
        const bool nowMuted = ! audioEngine.isMasterMuted();
        audioEngine.setMasterMuted(nowMuted);
        muteButton.setColour(juce::TextButton::buttonColourId,
                             nowMuted ? tf::colour::danger : juce::Colour{});
        muteButton.setColour(juce::TextButton::textColourOffId,
                             nowMuted ? juce::Colours::white : tf::colour::text);
    };
    addAndMakeVisible(muteButton);

    masterInputMeter.getLevel  = [this] { return audioEngine.getMasterInputPeakLevel(); };
    masterOutputMeter.getLevel = [this] { return audioEngine.getMasterOutputPeakLevel(); };
    masterInputMeter.compact  = true;
    masterOutputMeter.compact = true;
    addAndMakeVisible(masterInputMeter);
    addAndMakeVisible(masterOutputMeter);

    inputChLabel.setFont(juce::FontOptions(12.0f));
    inputChLabel.setColour(juce::Label::textColourId, tf::colour::text.withAlpha(0.85f));
    inputChLabel.setTooltip("ASIO input channel routed to the plugin chain (mono)");
    addAndMakeVisible(inputChLabel);

    inputChannelCombo.setTooltip("Select which physical input channel is fed into the plugin chain");
    inputChannelCombo.onChange = [this]
    {
        const int idx = inputChannelCombo.getSelectedItemIndex();
        if (idx >= 0)
        {
            audioEngine.setInputChannelIndex(idx);
            saveInputChannel();
        }
    };
    addAndMakeVisible(inputChannelCombo);

    audioEngine.onDeviceChanged = [this]
    {
        juce::MessageManager::callAsync([this] { refreshInputChannelCombo(); });
    };

    audioEngine.start();
    tryRestoreAudioDeviceState();

    audioEngine.onMidiForControl = [this](const juce::MidiMessage& m) { handleControlMidi(m); };

    startTimerHz(10);   // performance metrics refresh

    restoreScanPaths();
    restoreChainViewMode();
    restoreInputChannel();

    HostDebug::log("MainComponent ready — scheduling plugin scan");

    juce::MessageManager::callAsync([this]
    {
        pluginScanner.scanAll();
        refreshPaletteList();

        // Restore scenes first so we know whether an active scene should own the chain.
        restoreTemplates();
        restoreControlMap();

        const int activeScene = templateManager.getCurrentIndex();

        if (juce::isPositiveAndBelow(activeScene, templateManager.getNumScenes()))
        {
            // Active scene takes priority: load async so the UI stays responsive.
            const auto& scene = templateManager.getScene(activeScene);
            setChainLoading(true, 0, scene.specs.size());
            pluginHost.getChain().buildChainAsync(
                scene.specs, scene.sections,
                [this](int loaded, int total) { setChainLoading(true, loaded, total); },
                [this, activeScene](int handle, bool allOk)
                {
                    if (handle > 0)
                        pluginHost.getChain().activateChain(handle, 0);
                    setChainLoading(false, 0, 0);
                    refreshChainList();
                    HostDebug::log("Startup: applied template " + juce::String(activeScene)
                                   + " (allOk=" + juce::String((int) allOk) + ")");
                });
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

    for (auto* b : { &audioSettingsButton, &scanButton, &scanPathsButton, &addButton,
                     &savePresetButton, &loadPresetButton,
                     &captureTemplateButton, &updateTemplateButton, &renameTemplateButton,
                     &deleteTemplateButton, &prevTemplateButton, &nextTemplateButton,
                     &addSectionStompButton, &addSectionPresetButton,
                     &learnExprButton, &clearMapsButton,
                     &chainViewToggleButton })
        b->removeListener(this);

    audioSettingsWindow.reset();
    saveTemplates();
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

    // Thin separator lines between footer sections (skip the label column on the left).
    if (footerSep1Y > 0 && footerSep2Y > 0)
    {
        const int sepX = footerPanel.getX() + 14 + 70;
        const int sepW = footerPanel.getWidth() - 28 - 70;
        g.setColour(tf::colour::outline.withAlpha(0.25f));
        g.fillRect(sepX, footerSep1Y, sepW, 1);
        g.fillRect(sepX, footerSep2Y, sepW, 1);
    }

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
    const int pad = 14, gap = 10, rowH = 28, footerH = 128;
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
    toolbar.removeFromLeft(6);
    scanPathsButton.setBounds(toolbar.removeFromLeft(100));
    area.removeFromTop(gap);

    // ── Footer panel (scenes + control), pinned to the bottom ─────────────────
    footerPanel = area.removeFromBottom(footerH);
    {
        auto f = footerPanel.reduced(14, 10);

        // ─ Templates row ──────────────────────────────────────────────────────
        auto sceneRow = f.removeFromTop(rowH);
        templatesLabel    .setBounds(sceneRow.removeFromLeft(70));
        templateSelector  .setBounds(sceneRow.removeFromLeft(200));
        sceneRow.removeFromLeft(6);
        prevTemplateButton.setBounds(sceneRow.removeFromLeft(rowH));
        sceneRow.removeFromLeft(4);
        nextTemplateButton.setBounds(sceneRow.removeFromLeft(rowH));
        sceneRow.removeFromLeft(6);
        for (auto* b : { &captureTemplateButton, &updateTemplateButton,
                         &renameTemplateButton,  &deleteTemplateButton })
        {
            b->setBounds(sceneRow.removeFromLeft(rowH));
            sceneRow.removeFromLeft(4);
        }
        templateDirtyLabel.setBounds(sceneRow.removeFromLeft(88));

        footerSep1Y = f.getY() + 3;   // midpoint of the 6px gap below templates
        f.removeFromTop(6);

        // ─ Master area: controls (rowH) + meter strip (8px) ──────────────────
        auto masterArea = f.removeFromTop(rowH + 4 + 8);
        auto masterRow  = masterArea.removeFromTop(rowH);
        masterLabel    .setBounds(masterRow.removeFromLeft(70));
        inputGainLabel .setBounds(masterRow.removeFromLeft(46));
        inputGainSlider.setBounds(masterRow.removeFromLeft(120));
        masterRow.removeFromLeft(8);
        outputVolLabel .setBounds(masterRow.removeFromLeft(46));
        outputVolSlider.setBounds(masterRow.removeFromLeft(120));
        masterRow.removeFromLeft(8);
        muteButton     .setBounds(masterRow.removeFromLeft(52));
        masterRow.removeFromLeft(10);
        inputChLabel   .setBounds(masterRow.removeFromLeft(34));
        inputChannelCombo.setBounds(masterRow.removeFromLeft(110));

        masterArea.removeFromTop(4);                          // gap before meter strip
        auto meterRow = masterArea;                           // remaining 8px
        meterRow.removeFromLeft(70 + 46);                    // skip MASTER label + In Gain label
        masterInputMeter .setBounds(meterRow.removeFromLeft(120));
        meterRow.removeFromLeft(8 + 46);                     // gap + Out Vol label
        masterOutputMeter.setBounds(meterRow.removeFromLeft(120));

        footerSep2Y = f.getY() + 3;   // midpoint of the 6px gap below master area
        f.removeFromTop(6);

        // ─ Control row ────────────────────────────────────────────────────────
        auto controlRow = f.removeFromTop(rowH);
        controlSectionLabel.setBounds(controlRow.removeFromLeft(70));
        learnExprButton    .setBounds(controlRow.removeFromLeft(110)); controlRow.removeFromLeft(5);
        clearMapsButton    .setBounds(controlRow.removeFromLeft(80));  controlRow.removeFromLeft(8);
        controlLabel       .setBounds(controlRow);
    }
    area.removeFromBottom(gap);

    // ── Content: LIBRARY panel (fixed width) | SIGNAL CHAIN panel ───────────────
    auto content = area;
    libraryPanel = content.removeFromLeft(260);
    content.removeFromLeft(gap);
    chainPanel = content;

    {
        auto in = libraryPanel.reduced(12);

        // Header row same height/gap as chain panel so labels and second rows align.
        auto libHeadRow = in.removeFromTop(28);
        paletteLabel.setBounds(libHeadRow.withTrimmedTop(4));
        in.removeFromTop(4);

        auto filterRow = in.removeFromTop(28);
        formatFilterCombo.setBounds(filterRow.removeFromLeft(72));
        filterRow.removeFromLeft(4);
        searchBox.setBounds(filterRow);
        in.removeFromTop(4);

        addButton.setBounds(in.removeFromBottom(rowH));
        in.removeFromBottom(8);
        paletteListBox.setBounds(in);
    }

    {
        auto in = chainPanel.reduced(12);

        // Header row: "SIGNAL CHAIN" label + Save/Load preset buttons + view toggle.
        auto headRow = in.removeFromTop(28);
        loadPresetButton.setBounds(headRow.removeFromRight(78));
        headRow.removeFromRight(6);
        savePresetButton.setBounds(headRow.removeFromRight(78));
        headRow.removeFromRight(6);
        chainViewToggleButton.setBounds(headRow.removeFromRight(28));
        headRow.removeFromRight(6);
        chainLabel.setBounds(headRow.withTrimmedTop(4));
        in.removeFromTop(4);

        // Section-add strip: "+ Stomp"  "+ Preset"
        auto sectionRow = in.removeFromTop(28);
        addSectionStompButton .setBounds(sectionRow.removeFromLeft(72));
        sectionRow.removeFromLeft(6);
        addSectionPresetButton.setBounds(sectionRow.removeFromLeft(72));
        in.removeFromTop(4);

        chainListBox.setBounds(in);
        chainHorizontalView.setBounds(in);
        chainLoadingLabel.setBounds(in);   // overlay on top of chain list
    }
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &chainViewToggleButton)
    {
        chainHorizontalMode = ! chainHorizontalMode;
        applyChainViewMode();
        saveChainViewMode();
        refreshChainList();
        return;
    }

    if (button == &audioSettingsButton) { openAudioSettings(); return; }

    if (button == &scanButton)
    {
        HostDebug::log("UI: Rescan Plugins clicked");
        pluginScanner.scanAll(true);   // force full rescan
        refreshPaletteList();
        return;
    }

    if (button == &scanPathsButton) { openScanPaths(); return; }

    if (button == &addButton)    { addSelectedToChain();      return; }
    if (button == &savePresetButton) { savePreset();          return; }
    if (button == &loadPresetButton) { loadPreset();          return; }
    if (button == &captureTemplateButton) { captureTemplate();        return; }
    if (button == &updateTemplateButton)  { updateTemplate();         return; }
    if (button == &renameTemplateButton)  { renameCurrentTemplate();  return; }
    if (button == &deleteTemplateButton)  { deleteTemplate();         return; }
    if (button == &prevTemplateButton)    { stepTemplate(-1);         return; }
    if (button == &nextTemplateButton)    { stepTemplate(+1);         return; }

    if (button == &addSectionStompButton)  { addSectionOfType(PluginChain::SectionDef::Type::stomp);  return; }
    if (button == &addSectionPresetButton) { addSectionOfType(PluginChain::SectionDef::Type::preset); return; }

    if (button == &learnExprButton)
    {
        const int si = rowToSlotIndex(getSelectedChainRow());
        if (si < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                "No Slot Selected", "Select a plugin slot in the signal chain first.");
            return;
        }
        armExpressionLearn(si, 0);
        return;
    }
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
            bindingLearnComplete(trigger, action);
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
        bindingLearnComplete(trigger, action);
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
            toggleBypassAt(findSlotIndexById(action.index));
            break;

        case ControlAction::Type::activatePresetSlot:
            activatePresetSlotAt(findSlotIndexById(action.index));
            break;

        case ControlAction::Type::nextTemplate:  stepTemplate(+1);             break;
        case ControlAction::Type::prevTemplate:  stepTemplate(-1);             break;
        case ControlAction::Type::loadTemplate:  recallTemplate(action.index); break;

        case ControlAction::Type::none:
        default:
            break;
    }
}

void MainComponent::captureTemplate()
{
    const auto name = "Template " + juce::String(templateManager.getNumScenes() + 1);
    const int idx = templateManager.addScene(name, pluginHost.captureChain(), pluginHost.captureSectionDefs(), controlMap);
    templateManager.setCurrentIndex(idx);
    refreshTemplateSelector();
    saveTemplates();
    setTemplateDirty(false);
    HostDebug::log("Template captured: " + name + " (" + juce::String(pluginHost.getNumSlots()) + " slots)");
}

void MainComponent::updateTemplate()
{
    const int idx = templateManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, templateManager.getNumScenes()))
        return;

    templateManager.replaceScene(idx, pluginHost.captureChain(), pluginHost.captureSectionDefs(), controlMap);
    saveTemplates();
    setTemplateDirty(false);
    HostDebug::log("Template updated: index " + juce::String(idx));
}

void MainComponent::renameCurrentTemplate()
{
    const int idx = templateManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, templateManager.getNumScenes()))
        return;

    const juce::String current = templateManager.getScene(idx).name;

    auto* dialog = new juce::AlertWindow("Rename Template",
                                         "Enter a new name for this template:",
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
                    templateManager.renameScene(idx, newName);
                    refreshTemplateSelector();
                    saveTemplates();
                    HostDebug::log("Template renamed: index " + juce::String(idx) + " -> \"" + newName + "\"");
                }
            }
            delete dialog;
        }), true);
}

void MainComponent::deleteTemplate()
{
    const int idx = templateManager.getCurrentIndex();

    if (! juce::isPositiveAndBelow(idx, templateManager.getNumScenes()))
        return;

    templateManager.removeScene(idx);
    refreshTemplateSelector();
    saveTemplates();
    setTemplateDirty(false);
    HostDebug::log("Template deleted: index " + juce::String(idx));
}

void MainComponent::recallTemplate(int index)
{
    if (! juce::isPositiveAndBelow(index, templateManager.getNumScenes()))
        return;

    templateManager.setCurrentIndex(index);
    const auto& scene = templateManager.getScene(index);

    // Apply the template's own control map immediately.
    controlMap = scene.controlMap;
    saveControlMap();
    updateControlLabel();

    pluginHost.cancelPendingSwitch();
    setChainLoading(true, 0, scene.specs.size());

    pluginHost.switchChainAsync(
        scene.specs, scene.sections, 25,
        [this, index](bool allOk)
        {
            setChainLoading(false, 0, 0);
            refreshChainList();
            refreshTemplateSelector();
            setTemplateDirty(false);
            HostDebug::log("Template recalled: index " + juce::String(index)
                           + " (allOk=" + juce::String((int) allOk) + ")");
        });
}

void MainComponent::stepTemplate(int delta)
{
    const int n = templateManager.getNumScenes();

    if (n == 0)
        return;

    int idx = templateManager.getCurrentIndex();
    idx = idx < 0 ? 0 : (idx + delta + n) % n;   // wrap around
    recallTemplate(idx);
}

void MainComponent::refreshTemplateSelector()
{
    templateSelector.clear(juce::dontSendNotification);

    const auto names = templateManager.getSceneNames();

    for (int i = 0; i < names.size(); ++i)
        templateSelector.addItem(names[i], i + 1);   // ids are index+1

    const int cur = templateManager.getCurrentIndex();

    if (juce::isPositiveAndBelow(cur, names.size()))
        templateSelector.setSelectedId(cur + 1, juce::dontSendNotification);
}

void MainComponent::saveTemplates()
{
    if (auto* settings = appProperties.getUserSettings())
    {
        if (auto xml = templateManager.toValueTree().createXml())
            settings->setValue(templatesStateKey, xml.get());

        settings->saveIfNeeded();
    }
}

void MainComponent::restoreTemplates()
{
    auto* settings = appProperties.getUserSettings();

    if (settings == nullptr)
        return;

    if (auto xml = settings->getXmlValue(templatesStateKey))
    {
        templateManager.fromValueTree(juce::ValueTree::fromXml(*xml));
        refreshTemplateSelector();
        HostDebug::log("Templates restored: " + juce::String(templateManager.getNumScenes()));
    }
}

void MainComponent::bindingLearnComplete(const ControlTrigger& trigger, const ControlAction& action)
{
    // Check for an existing binding on the same trigger and ask before overwriting.
    for (int b = 0; b < controlMap.getNumBindings(); ++b)
    {
        if (controlMap.getBinding(b).trigger.matches(trigger))
        {
            const auto existing = controlMap.getBinding(b).action;
            auto* dlg = new juce::AlertWindow("Duplicate Binding",
                trigger.toString() + " is already mapped to:\n[" + existing.toString()
                + "]\n\nOverwrite with [" + action.toString() + "]?",
                juce::MessageBoxIconType::WarningIcon);
            dlg->addButton("Overwrite", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dlg->addButton("Cancel",    0, juce::KeyPress(juce::KeyPress::escapeKey));
            dlg->enterModalState(true, juce::ModalCallbackFunction::create(
                [this, b, trigger, action, dlg](int result)
                {
                    if (result == 1)
                    {
                        controlMap.removeBinding(b);
                        controlMap.addBinding({ trigger, action });
                        saveControlMap();
                        updateControlLabel();
                        refreshChainList();
                        setTemplateDirty(templateManager.getCurrentIndex() >= 0);
                        HostDebug::log("Learn: overwrite " + trigger.toString() + " -> " + action.toString());
                    }
                    delete dlg;
                }), true);
            return;
        }
    }

    controlMap.addBinding({ trigger, action });
    saveControlMap();
    updateControlLabel();
    refreshChainList();
    setTemplateDirty(templateManager.getCurrentIndex() >= 0);
    HostDebug::log("Learn: bound " + trigger.toString() + " -> " + action.toString());
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
    refreshChainList();
    setTemplateDirty(templateManager.getCurrentIndex() >= 0);
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

void MainComponent::setTemplateDirty(bool dirty)
{
    templateDirty = dirty;
    templateDirtyLabel.setVisible(dirty);

    if (dirty)
    {
        updateTemplateButton.setColour(juce::TextButton::buttonColourId, tf::colour::warn.withAlpha(0.25f));
        updateTemplateButton.setColour(juce::TextButton::textColourOffId, tf::colour::warn);
    }
    else
    {
        updateTemplateButton.removeColour(juce::TextButton::buttonColourId);
        updateTemplateButton.removeColour(juce::TextButton::textColourOffId);
    }
}

void MainComponent::setChainLoading(bool loading, int loaded, int total)
{
    chainIsLoading = loading;
    chainLoadingLabel.setVisible(loading);

    if (loading)
        chainLoadingLabel.setText("Loading " + juce::String(loaded) + " / " + juce::String(total) + " plugins...",
                                  juce::dontSendNotification);

    // Disable template navigation while loading to prevent queued switches.
    prevTemplateButton.setEnabled(! loading);
    nextTemplateButton.setEnabled(! loading);
    templateSelector.setEnabled(! loading);
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

void MainComponent::openScanPaths()
{
    if (scanPathsWindow != nullptr && scanPathsWindow->isVisible())
    {
        scanPathsWindow->toFront(true);
        return;
    }

    scanPathsWindow = std::make_unique<ScanPathsWindow>(
        pluginScanner.getCustomPaths(),
        [this](const juce::StringArray& paths)
        {
            saveScanPaths(paths);
            pluginScanner.setCustomPaths(paths);
        },
        [this] { scanPathsWindow.reset(); });
}

void MainComponent::saveScanPaths(const juce::StringArray& paths)
{
    auto* settings = appProperties.getUserSettings();
    if (settings == nullptr) return;

    settings->setValue(pluginScanPathsKey, paths.joinIntoString("\n"));
    settings->saveIfNeeded();
    HostDebug::log("Scan paths saved: " + juce::String(paths.size()) + " path(s)");
}

void MainComponent::restoreScanPaths()
{
    auto* settings = appProperties.getUserSettings();
    if (settings == nullptr) return;

    const auto stored = settings->getValue(pluginScanPathsKey);
    if (stored.isEmpty()) return;

    const auto paths = juce::StringArray::fromLines(stored);
    pluginScanner.setCustomPaths(paths);
    HostDebug::log("Scan paths restored: " + juce::String(paths.size()) + " path(s)");
}

void MainComponent::applyChainViewMode()
{
    chainListBox       .setVisible(! chainHorizontalMode);
    chainHorizontalView.setVisible(  chainHorizontalMode);
    chainViewToggleButton.setButtonText(
        chainHorizontalMode
            ? juce::String::fromUTF8("\xE2\x86\x95")   // ↕  (click to go vertical)
            : juce::String::fromUTF8("\xE2\x86\x94")); // ↔  (click to go horizontal)
}

void MainComponent::restoreChainViewMode()
{
    if (auto* settings = appProperties.getUserSettings())
        chainHorizontalMode = settings->getBoolValue(chainViewModeKey, false);
    applyChainViewMode();
}

void MainComponent::saveChainViewMode()
{
    if (auto* settings = appProperties.getUserSettings())
    {
        settings->setValue(chainViewModeKey, chainHorizontalMode);
        settings->saveIfNeeded();
    }
}

void MainComponent::refreshInputChannelCombo()
{
    const auto names = audioEngine.getInputChannelNames();
    const int prevIdx = audioEngine.getInputChannelIndex();

    inputChannelCombo.clear(juce::dontSendNotification);

    if (names.isEmpty())
    {
        inputChannelCombo.addItem("(no inputs)", 1);
        inputChannelCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        return;
    }

    for (int i = 0; i < names.size(); ++i)
        inputChannelCombo.addItem(names[i], i + 1);

    const int clampedIdx = juce::jlimit(0, names.size() - 1, prevIdx);
    inputChannelCombo.setSelectedItemIndex(clampedIdx, juce::dontSendNotification);
    audioEngine.setInputChannelIndex(clampedIdx);
}

void MainComponent::restoreInputChannel()
{
    int idx = 0;
    if (auto* settings = appProperties.getUserSettings())
        idx = settings->getIntValue(inputChannelIndexKey, 0);

    audioEngine.setInputChannelIndex(idx);
    // combo will be populated when onDeviceChanged fires after audioEngine.start()
}

void MainComponent::saveInputChannel()
{
    if (auto* settings = appProperties.getUserSettings())
    {
        settings->setValue(inputChannelIndexKey, audioEngine.getInputChannelIndex());
        settings->saveIfNeeded();
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
    filteredPlugins.clear();

    const int  selectedFormat = formatFilterCombo.getSelectedId(); // 1=All 2=VST3 3=VST2
    const auto searchText     = searchBox.getText().trim().toLowerCase();

    juce::Array<juce::PluginDescription> vst3List, otherList;
    juce::StringArray seen; // dedup key: "Format|name" — guards against same DLL via two paths

    for (const auto& type : pluginScanner.getKnownPluginList().getTypes())
    {
        if (selectedFormat == 2 && type.pluginFormatName != "VST3") continue;
        if (selectedFormat == 3 && type.pluginFormatName != "VST")  continue;
        if (searchText.isNotEmpty() && ! type.name.toLowerCase().contains(searchText)) continue;

        const auto key = type.pluginFormatName + "|" + type.name;
        if (seen.contains(key)) continue;
        seen.add(key);

        if (type.pluginFormatName == "VST3")
            vst3List.add(type);
        else
            otherList.add(type);
    }

    auto byName = [](const juce::PluginDescription& a, const juce::PluginDescription& b)
    { return a.name.compareIgnoreCase(b.name) < 0; };
    std::sort(vst3List.begin(),  vst3List.end(),  byName);
    std::sort(otherList.begin(), otherList.end(), byName);

    for (auto& p : vst3List)  filteredPlugins.add(p);
    for (auto& p : otherList) filteredPlugins.add(p);

    for (const auto& p : filteredPlugins)
        paletteNames.add(p.name + " [" + p.pluginFormatName + "]");

    HostDebug::log("Palette refreshed: " + juce::String(filteredPlugins.size()) + " shown / "
                   + juce::String(pluginScanner.getKnownPluginList().getNumTypes()) + " total");

    paletteListBox.updateContent();
    paletteListBox.repaint();
}

void MainComponent::refreshChainList()
{
    const int hlRow = chainModel.highlightedRow;

    const auto sections  = pluginHost.getSectionDefs();
    const auto slotInfos = pluginHost.getSlotInfos();
    const int  lastSecIdx = sections.size() - 1;

    juce::Array<ChainRow> rows;

    for (int si = 0; si < sections.size(); ++si)
    {
        const auto& sec = sections.getReference(si);

        // Section header row.
        ChainRow header;
        header.kind          = ChainRow::Kind::sectionHeader;
        header.sectionId     = sec.id;
        header.sectionName   = sec.name;
        header.sectionType   = sec.type;
        header.isFirstSection  = (si == 0);
        header.isLastSection   = (si == lastSecIdx);
        header.sectionBypassed = sec.bypassed;
        header.sectionGain     = sec.gain;
        rows.add(header);

        // Collect slot indices belonging to this section (preserving flat-list order).
        juce::Array<int> slotIndices;
        for (int i = 0; i < slotInfos.size(); ++i)
            if (slotInfos.getReference(i).sectionId == sec.id)
                slotIndices.add(i);

        for (int j = 0; j < slotIndices.size(); ++j)
        {
            const int absIdx = slotIndices[j];
            ChainRow slot;
            slot.kind             = ChainRow::Kind::slot;
            slot.slotInfo         = slotInfos.getReference(absIdx);
            slot.slotIndex        = absIdx;
            slot.isFirstInSection = (j == 0);
            slot.isLastInSection  = (j == slotIndices.size() - 1);

            // Build control hint from any binding targeting this slot (matched by stable slotId).
            for (int b = 0; b < controlMap.getNumBindings(); ++b)
            {
                const auto& binding = controlMap.getBinding(b);
                if (binding.action.index == slot.slotInfo.slotId &&
                    (binding.action.type == ControlAction::Type::toggleBypass ||
                     binding.action.type == ControlAction::Type::activatePresetSlot))
                {
                    const juce::String prefix = (binding.action.type == ControlAction::Type::toggleBypass)
                                                ? "BYP" : "ACT";
                    slot.controlHint = prefix + ": " + binding.trigger.toString();
                    break;
                }
            }

            rows.add(slot);
        }
    }

    chainModel.setRows(rows);

    // Clamp highlight to the new row count.
    if (! juce::isPositiveAndBelow(hlRow, rows.size()))
        chainModel.highlightedRow = -1;

    chainListBox.updateContent();
    chainHorizontalView.setRows(rows);

    chainListBox.repaint();
}

void MainComponent::clearChainSelection()
{
    const int prevRow = chainModel.highlightedRow;
    chainModel.highlightedRow = -1;
    if (prevRow >= 0)
    {
        juce::Component* comp = chainHorizontalMode
            ? chainHorizontalView.getComponentForRowNumber(prevRow)
            : chainListBox.getComponentForRowNumber(prevRow);
        if (comp) comp->repaint();
    }
}

int MainComponent::getSelectedPaletteRow() const
{
    return paletteListBox.getSelectedRow();
}

int MainComponent::getSelectedChainRow() const
{
    return chainListBox.getSelectedRow();
}

int MainComponent::rowToSlotIndex(int row) const
{
    const auto& rows = chainModel.getRows();
    if (juce::isPositiveAndBelow(row, rows.size())
        && rows.getReference(row).kind == ChainRow::Kind::slot)
        return rows.getReference(row).slotIndex;
    return -1;
}

int MainComponent::findSlotIndexById(int slotId) const
{
    const auto infos = pluginHost.getSlotInfos();
    for (int i = 0; i < infos.size(); ++i)
        if (infos.getReference(i).slotId == slotId)
            return i;
    return -1;
}

int MainComponent::getTargetSectionId() const
{
    const int row = chainListBox.getSelectedRow();
    const auto& rows = chainModel.getRows();

    if (juce::isPositiveAndBelow(row, rows.size()))
    {
        const auto& r = rows.getReference(row);
        if (r.kind == ChainRow::Kind::sectionHeader) return r.sectionId;
        if (r.kind == ChainRow::Kind::slot)          return r.slotInfo.sectionId;
    }

    const auto secs = pluginHost.getSectionDefs();
    return secs.isEmpty() ? 1 : secs.getLast().id;
}

void MainComponent::addSelectedToChain()
{
    const auto row = getSelectedPaletteRow();

    if (! juce::isPositiveAndBelow(row, filteredPlugins.size()))
    {
        HostDebug::log("UI: Add clicked — no palette selection");
        return;
    }

    const auto description = filteredPlugins[row];
    const int  sectionId   = getTargetSectionId();
    HostDebug::log("UI: Add to chain — " + description.name + " (section=" + juce::String(sectionId) + ")");

    if (pluginHost.addPlugin(description, sectionId))
    {
        refreshChainList();

        if (templateManager.getCurrentIndex() >= 0)
            setTemplateDirty(true);
    }
}

void MainComponent::moveSlotAt(int fromSlotIndex, int toSlotIndex, int sectionIdOverride)
{
    const int n = pluginHost.getNumSlots();
    if (! juce::isPositiveAndBelow(fromSlotIndex, n))
        return;

    // toSlotIndex may equal n when dropping into an empty last section (append case).
    const int clampedTo = juce::jlimit(0, juce::jmax(0, n - 1), toSlotIndex);
    pluginHost.movePlugin(fromSlotIndex, clampedTo, sectionIdOverride);
    refreshChainList();

    if (templateManager.getCurrentIndex() >= 0)
        setTemplateDirty(true);
}

void MainComponent::toggleBypassAt(int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex, pluginHost.getNumSlots()))
        return;

    const auto infos = pluginHost.getSlotInfos();
    const auto& info = infos.getReference(slotIndex);

    if (info.isPreset)
    {
        if (! info.bypassed)
            return;   // already the active preset slot — no-op
        activatePresetSlotAt(slotIndex);
    }
    else
    {
        pluginHost.setBypass(slotIndex, ! info.bypassed);
        refreshChainList();

        if (templateManager.getCurrentIndex() >= 0)
            setTemplateDirty(true);
    }
}

void MainComponent::activatePresetSlotAt(int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex, pluginHost.getNumSlots()))
        return;

    const auto infos = pluginHost.getSlotInfos();
    const auto& info = infos.getReference(slotIndex);

    if (info.isPreset)
        pluginHost.activatePresetSlot(slotIndex);
    else
        pluginHost.setBypass(slotIndex, ! info.bypassed);   // slot moved to stomp section — toggle bypass

    refreshChainList();

    if (templateManager.getCurrentIndex() >= 0)
        setTemplateDirty(true);
}

void MainComponent::removeSlotAt(int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex, pluginHost.getNumSlots()))
        return;

    HostDebug::log("UI: Remove chain slot " + juce::String(slotIndex));
    pluginHost.removePlugin(slotIndex);
    refreshChainList();

    if (templateManager.getCurrentIndex() >= 0)
        setTemplateDirty(true);
}

void MainComponent::duplicateSlotAt(int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex, pluginHost.getNumSlots()))
        return;

    HostDebug::log("UI: Duplicate chain slot " + juce::String(slotIndex));
    if (pluginHost.duplicatePlugin(slotIndex))
    {
        refreshChainList();

        if (templateManager.getCurrentIndex() >= 0)
            setTemplateDirty(true);
    }
}

void MainComponent::openEditorAt(int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex, pluginHost.getNumSlots()))
    {
        HostDebug::log("UI: Open editor — invalid slot " + juce::String(slotIndex));
        return;
    }

    pluginHost.openEditorWindow(slotIndex);
}

void MainComponent::addSectionOfType(PluginChain::SectionDef::Type type)
{
    pluginHost.addSection(type);
    refreshChainList();

    if (templateManager.getCurrentIndex() >= 0)
        setTemplateDirty(true);
}

void MainComponent::removeSectionAt(int sectionId)
{
    juce::String sectionName;
    for (const auto& s : pluginHost.getSectionDefs())
        if (s.id == sectionId) { sectionName = s.name; break; }

    auto* dialog = new juce::AlertWindow("Remove Section",
                                         "Remove \"" + sectionName + "\"?\nAll plugins in this section will also be removed.",
                                         juce::MessageBoxIconType::WarningIcon);
    dialog->addButton("Remove", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, sectionId](int result)
        {
            if (result == 1)
            {
                pluginHost.removeSection(sectionId);
                refreshChainList();

                if (templateManager.getCurrentIndex() >= 0)
                    setTemplateDirty(true);
            }
        }), true);
}

void MainComponent::renameSectionAt(int sectionId)
{
    const auto secs = pluginHost.getSectionDefs();
    juce::String current;
    for (const auto& s : secs)
        if (s.id == sectionId) { current = s.name; break; }

    auto* dialog = new juce::AlertWindow("Rename Section",
                                         "Enter a new name for this section:",
                                         juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", current, "Name:");
    dialog->addButton("OK",     1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, sectionId, dialog](int result)
        {
            if (result == 1)
            {
                const juce::String newName = dialog->getTextEditorContents("name").trim();
                if (newName.isNotEmpty())
                {
                    pluginHost.renameSection(sectionId, newName);
                    refreshChainList();

                    if (templateManager.getCurrentIndex() >= 0)
                        setTemplateDirty(true);
                }
            }
            delete dialog;
        }), true);
}

void MainComponent::moveSectionUpAt(int sectionId)
{
    pluginHost.moveSectionUp(sectionId);
    refreshChainList();

    if (templateManager.getCurrentIndex() >= 0)
        setTemplateDirty(true);
}

void MainComponent::moveSectionDownAt(int sectionId)
{
    pluginHost.moveSectionDown(sectionId);
    refreshChainList();

    if (templateManager.getCurrentIndex() >= 0)
        setTemplateDirty(true);
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

        const auto specs    = pluginHost.captureChain();
        const auto sections = pluginHost.captureSectionDefs();

        if (Preset::saveToFile(specs, sections, file.getFileNameWithoutExtension(), file))
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
    juce::Array<PluginChain::SectionDef> sections;

    if (! Preset::loadFromFile(file, specs, sections))
        return;

    pluginHost.switchChainWithCrossfade(specs, sections, 25);   // smooth, click-free preset switch
    refreshChainList();
    saveLastPresetPath(file);
    HostDebug::log("Preset applied: " + file.getFileName());

    if (templateManager.getCurrentIndex() >= 0)
        setTemplateDirty(true);
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
    options.applicationName = "AmpForge";
    options.filenameSuffix = "settings";
    options.folderName = AppDataDir::get().getFullPathName();
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
