#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "AudioEngine.h"
#include "PluginHost.h"
#include "PluginScanner.h"
#include "ControlMap.h"
#include "TemplateManager.h"
#include "ToneForgeLookAndFeel.h"
#include "ChainListBox.h"

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Button::Listener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // ── Audio settings window ─────────────────────────────────────────────────
    class AudioSettingsWindow : public juce::DocumentWindow
    {
    public:
        AudioSettingsWindow(juce::AudioDeviceManager& manager,
                            std::function<void()> onClose)
            : juce::DocumentWindow("Audio Settings",
                                   tf::colour::surface,
                                   juce::DocumentWindow::closeButton),
              onCloseCallback(std::move(onClose))
        {
            setUsingNativeTitleBar(true);
            setResizable(true, false);

            auto* selector = new juce::AudioDeviceSelectorComponent(
                manager, 0, 2, 0, 2, false, false, true, false);

            selector->setSize(500, 400);
            setContentOwned(selector, true);
            centreWithSize(500, 420);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            onCloseCallback();
            setVisible(false);
        }

    private:
        std::function<void()> onCloseCallback;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
    };

    // ── Scan paths window ─────────────────────────────────────────────────────

    /** Content component: a list of custom scan directories with Add/Remove buttons. */
    class ScanPathsContent : public juce::Component,
                             private juce::ListBoxModel
    {
    public:
        ScanPathsContent(juce::StringArray initialPaths,
                         std::function<void(const juce::StringArray&)> onChange)
            : paths(std::move(initialPaths)), onPathsChanged(std::move(onChange))
        {
            addAndMakeVisible(pathListBox);
            pathListBox.setModel(this);
            pathListBox.setRowHeight(26);

            addButton   .setButtonText("+ Add Directory...");
            removeButton.setButtonText("Remove Selected");

            addButton.onClick    = [this] { addPath(); };
            removeButton.onClick = [this] { removePath(); };

            addAndMakeVisible(addButton);
            addAndMakeVisible(removeButton);

            hint.setText("These paths are searched in addition to the defaults.\n"
                         "Both VST3 and VST2 (if enabled) are scanned in every listed directory.",
                         juce::dontSendNotification);
            hint.setFont(juce::FontOptions(12.0f));
            hint.setColour(juce::Label::textColourId, juce::Colours::grey);
            hint.setJustificationType(juce::Justification::topLeft);
            addAndMakeVisible(hint);
        }

        ~ScanPathsContent() override = default;

        void resized() override
        {
            auto area = getLocalBounds().reduced(12);
            hint.setBounds(area.removeFromBottom(34));
            area.removeFromBottom(6);
            auto btnRow = area.removeFromBottom(30);
            area.removeFromBottom(6);
            addButton.setBounds(btnRow.removeFromLeft(150));
            btnRow.removeFromLeft(8);
            removeButton.setBounds(btnRow.removeFromLeft(130));
            pathListBox.setBounds(area);
        }

        void paint(juce::Graphics& g) override { g.fillAll(tf::colour::surface); }

    private:
        int  getNumRows() override { return paths.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override
        {
            if (sel)
            {
                g.setColour(tf::colour::accent.withAlpha(0.16f));
                g.fillRoundedRectangle(juce::Rectangle<int>(2, 1, w - 4, h - 2).toFloat(), 4.0f);
            }
            if (juce::isPositiveAndBelow(row, paths.size()))
            {
                g.setColour(sel ? tf::colour::text : tf::colour::text.withAlpha(0.85f));
                g.setFont(juce::FontOptions(13.0f));
                g.drawText(paths[row], 10, 0, w - 14, h,
                           juce::Justification::centredLeft, true);
            }
        }

        void addPath()
        {
            fileChooser = std::make_unique<juce::FileChooser>("Select Plugin Directory");
            fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                [this](const juce::FileChooser& fc)
                {
                    const auto dir = fc.getResult();
                    if (dir.isDirectory())
                    {
                        const auto p = dir.getFullPathName();
                        if (! paths.contains(p))
                        {
                            paths.add(p);
                            pathListBox.updateContent();
                            if (onPathsChanged) onPathsChanged(paths);
                        }
                    }
                });
        }

        void removePath()
        {
            const int row = pathListBox.getSelectedRow();
            if (juce::isPositiveAndBelow(row, paths.size()))
            {
                paths.remove(row);
                pathListBox.updateContent();
                if (onPathsChanged) onPathsChanged(paths);
            }
        }

        juce::StringArray paths;
        std::function<void(const juce::StringArray&)> onPathsChanged;

        juce::ListBox   pathListBox;
        juce::TextButton addButton, removeButton;
        juce::Label      hint;
        std::unique_ptr<juce::FileChooser> fileChooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScanPathsContent)
    };

    class ScanPathsWindow : public juce::DocumentWindow
    {
    public:
        ScanPathsWindow(juce::StringArray initialPaths,
                        std::function<void(const juce::StringArray&)> onChanged,
                        std::function<void()> onClose)
            : juce::DocumentWindow("Plugin Scan Paths",
                                   tf::colour::surface,
                                   juce::DocumentWindow::closeButton),
              onCloseCallback(std::move(onClose))
        {
            setUsingNativeTitleBar(true);
            setResizable(true, false);

            auto* content = new ScanPathsContent(std::move(initialPaths),
                                                  std::move(onChanged));
            content->setSize(540, 310);
            setContentOwned(content, true);
            centreWithSize(540, 330);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            onCloseCallback();
            setVisible(false);
        }

    private:
        std::function<void()> onCloseCallback;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScanPathsWindow)
    };

    // ── Plain text list model (palette / library) ─────────────────────────────
    class SimpleListModel : public juce::ListBoxModel
    {
    public:
        explicit SimpleListModel(juce::StringArray& source) : rows(source) {}

        int getNumRows() override { return rows.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (selected)
            {
                g.setColour(tf::colour::accent.withAlpha(0.16f));
                g.fillRoundedRectangle(juce::Rectangle<int>(2, 1, width - 4, height - 2).toFloat(), 5.0f);
            }

            if (juce::isPositiveAndBelow(row, rows.size()))
            {
                g.setColour(selected ? tf::colour::text : tf::colour::text.withAlpha(0.92f));
                g.setFont(juce::FontOptions(13.5f));
                g.drawText(rows[row], 12, 0, width - 18, height, juce::Justification::centredLeft);
            }
        }

        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
        {
            if (onDoubleClick)
                onDoubleClick(row);
        }

        std::function<void(int)> onDoubleClick;

    private:
        juce::StringArray& rows;
    };

    // ── Helpers ───────────────────────────────────────────────────────────────
    void initialiseSettings();
    void openAudioSettings();
    void saveAudioDeviceState();
    void tryRestoreAudioDeviceState();

    void buttonClicked(juce::Button* button) override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void openScanPaths();
    void saveScanPaths(const juce::StringArray& paths);
    void restoreScanPaths();

    void refreshPaletteList();
    void refreshChainList();
    int  getSelectedPaletteRow() const;
    int  getSelectedChainRow() const;
    void addSelectedToChain();

    // Per-slot chain actions (driven by the inline row buttons, receive slotIndex).
    void toggleBypassAt(int slotIndex);
    void activatePresetSlotAt(int slotIndex);
    void moveSlotAt(int fromSlotIndex, int toSlotIndex, int sectionIdOverride = -1);
    void removeSlotAt(int slotIndex);
    void openEditorAt(int slotIndex);

    // Section actions.
    void addSectionOfType(PluginChain::SectionDef::Type type);
    void removeSectionAt(int sectionId);
    void renameSectionAt(int sectionId);
    void moveSectionUpAt(int sectionId);
    void moveSectionDownAt(int sectionId);

    // Helpers for ChainRow ↔ slotIndex mapping.
    int  rowToSlotIndex(int row) const;
    int  getTargetSectionId() const;
    int  findSlotIndexById(int slotId) const;

    void savePreset();
    void loadPreset();
    void loadPresetFile(const juce::File& file);
    void saveLastPresetPath(const juce::File& file);
    void tryRestoreLastPreset();

    // Live control (Phase 4)
    void handleControlMidi(const juce::MidiMessage& message);   // called on the MIDI thread
    void executeAction(const ControlAction& action);           // called on the message thread

    // Templates (Phase 4.5)
    void captureTemplate();
    void updateTemplate();
    void renameCurrentTemplate();
    void deleteTemplate();
    void recallTemplate(int index);
    void stepTemplate(int delta);
    void refreshTemplateSelector();
    void saveTemplates();
    void restoreTemplates();

    // Control mapping UI (Phase 4.7)
    void armActionLearn(const ControlAction& action);
    void armExpressionLearn(int slotIndex, int paramIndex);
    void bindingLearnComplete(const ControlTrigger& trigger, const ControlAction& action);
    void clearMappings();
    void updateControlLabel();
    void saveControlMap();
    void restoreControlMap();

    // ── Theme (declared first so it outlives every child component) ────────────
    ToneForgeLookAndFeel lookAndFeel;

    // ── Core modules ─────────────────────────────────────────────────────────
    PluginHost pluginHost;
    AudioEngine audioEngine;
    PluginScanner pluginScanner;

    // ── UI ───────────────────────────────────────────────────────────────────
    juce::Label titleLabel;
    juce::Label metricsLabel;
    juce::Label paletteLabel        { {}, "LIBRARY" };
    juce::Label chainLabel          { {}, "SIGNAL CHAIN" };
    juce::Label templatesLabel      { {}, "TEMPLATES" };
    juce::Label controlSectionLabel { {}, "CONTROL" };

    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton scanButton          { "Rescan Plugins" };
    juce::TextButton scanPathsButton     { "Scan Paths..." };
    juce::TextButton addButton           { "+ Add to Chain" };
    juce::TextButton savePresetButton    { "Save" };
    juce::TextButton loadPresetButton    { "Load" };
    juce::TextButton captureTemplateButton;
    juce::TextButton updateTemplateButton;
    juce::TextButton renameTemplateButton;
    juce::TextButton deleteTemplateButton;
    juce::TextButton prevTemplateButton;
    juce::TextButton nextTemplateButton;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ComboBox   templateSelector;
    juce::Label      templateDirtyLabel;   // "●" shown when current chain differs from active template

    juce::TextButton addSectionStompButton  { "+ Stomp" };
    juce::TextButton addSectionPresetButton { "+ Preset" };

    juce::Label      masterLabel          { {}, "MASTER" };
    juce::Slider     inputGainSlider;
    juce::Label      inputGainLabel       { {}, "In Gain" };
    juce::TextButton inputGainResetButton;
    juce::Slider     outputVolSlider;
    juce::Label      outputVolLabel       { {}, "Out Vol" };
    juce::TextButton outputVolResetButton;
    juce::TextButton muteButton           { "MUTE" };

    juce::Label      controlLabel;
    juce::TextButton learnExprButton         { "Learn Expression" };
    juce::TextButton clearMapsButton         { "Clear Maps" };

    juce::StringArray paletteNames;
    SimpleListModel paletteModel { paletteNames };
    ChainListModel  chainModel;
    juce::ListBox     paletteListBox;
    ChainListBoxView  chainListBox { chainModel };

    // Panel backgrounds, computed in resized() and drawn in paint().
    juce::Rectangle<int> headerPanel, libraryPanel, chainPanel, footerPanel;

    std::unique_ptr<AudioSettingsWindow> audioSettingsWindow;
    std::unique_ptr<ScanPathsWindow>     scanPathsWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ── Template dirty tracking ───────────────────────────────────────────────
    bool templateDirty = false;
    void setTemplateDirty(bool dirty);

    // ── Live control ───────────────────────────────────────────────────────────
    ControlMap controlMap;
    TemplateManager templateManager;
    std::atomic<bool> midiLearnArmed { false };
    ControlAction pendingLearnAction;   // action to bind when the next trigger arrives
    std::atomic<bool> expressionLearnArmed { false };
    int pendingExprSlot = 0;
    int pendingExprParam = 0;

    // ── Persistence ──────────────────────────────────────────────────────────
    juce::ApplicationProperties appProperties;
    static constexpr const char* audioDeviceStateKey  = "audioDeviceState";
    static constexpr const char* lastPresetPathKey    = "lastPresetPath";
    static constexpr const char* templatesStateKey    = "scenes";   // keep "scenes" for backward compat
    static constexpr const char* controlMapStateKey   = "controlMap";
    static constexpr const char* pluginScanPathsKey   = "pluginScanPaths";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
