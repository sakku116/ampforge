#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "AudioEngine.h"
#include "PluginHost.h"
#include "PluginScanner.h"
#include "ControlMap.h"
#include "SceneManager.h"
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

    void savePreset();
    void loadPreset();
    void loadPresetFile(const juce::File& file);
    void saveLastPresetPath(const juce::File& file);
    void tryRestoreLastPreset();

    // Live control (Phase 4)
    void handleControlMidi(const juce::MidiMessage& message);   // called on the MIDI thread
    void executeAction(const ControlAction& action);           // called on the message thread

    // Scenes (Phase 4.5)
    void captureScene();
    void updateScene();
    void renameCurrentScene();
    void deleteScene();
    void recallScene(int index);
    void stepScene(int delta);
    void refreshSceneSelector();
    void saveScenes();
    void restoreScenes();

    // Control mapping UI (Phase 4.7)
    void armActionLearn(const ControlAction& action);
    void armExpressionLearn(int slotIndex, int paramIndex);
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
    juce::Label sceneLabel          { {}, "SCENES" };
    juce::Label controlSectionLabel { {}, "CONTROL" };

    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton scanButton          { "Rescan Plugins" };
    juce::TextButton addButton           { "+ Add to Chain" };
    juce::TextButton savePresetButton    { "Save" };
    juce::TextButton loadPresetButton    { "Load" };
    juce::TextButton captureSceneButton;
    juce::TextButton updateSceneButton;
    juce::TextButton renameSceneButton;
    juce::TextButton deleteSceneButton;
    juce::TextButton prevSceneButton;
    juce::TextButton nextSceneButton;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ComboBox   sceneSelector;
    juce::Label      sceneDirtyLabel;   // "●" shown when current chain differs from active scene

    juce::TextButton addSectionStompButton  { "+ Stomp" };
    juce::TextButton addSectionPresetButton { "+ Preset" };

    juce::Label      controlLabel;
    juce::TextButton learnBypassButton       { "Learn Bypass" };
    juce::TextButton learnPresetSelectButton { "Learn Preset Sel" };
    juce::TextButton learnExprButton         { "Learn Expression" };
    juce::TextButton learnSceneNextButton    { "Learn Scene+" };
    juce::TextButton learnScenePrevButton    { "Learn Scene-" };
    juce::TextButton clearMapsButton         { "Clear Maps" };

    juce::StringArray paletteNames;
    SimpleListModel paletteModel { paletteNames };
    ChainListModel  chainModel;
    juce::ListBox     paletteListBox;
    ChainListBoxView  chainListBox { chainModel };

    // Panel backgrounds, computed in resized() and drawn in paint().
    juce::Rectangle<int> headerPanel, libraryPanel, chainPanel, footerPanel;

    std::unique_ptr<AudioSettingsWindow> audioSettingsWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ── Scene dirty tracking ──────────────────────────────────────────────────
    bool sceneDirty = false;
    void setSceneDirty(bool dirty);

    // ── Live control ───────────────────────────────────────────────────────────
    ControlMap controlMap;
    SceneManager sceneManager;
    std::atomic<bool> midiLearnArmed { false };
    ControlAction pendingLearnAction;   // action to bind when the next trigger arrives
    std::atomic<bool> expressionLearnArmed { false };
    int pendingExprSlot = 0;
    int pendingExprParam = 0;

    // ── Persistence ──────────────────────────────────────────────────────────
    juce::ApplicationProperties appProperties;
    static constexpr const char* audioDeviceStateKey = "audioDeviceState";
    static constexpr const char* lastPresetPathKey   = "lastPresetPath";
    static constexpr const char* scenesStateKey      = "scenes";
    static constexpr const char* controlMapStateKey  = "controlMap";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
