#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "AudioEngine.h"
#include "PluginHost.h"
#include "PluginScanner.h"
#include "ControlMap.h"
#include "SceneManager.h"

class MainComponent : public juce::Component,
                      private juce::Button::Listener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

private:
    // ── Audio settings window ─────────────────────────────────────────────────
    class AudioSettingsWindow : public juce::DocumentWindow
    {
    public:
        AudioSettingsWindow(juce::AudioDeviceManager& manager,
                            std::function<void()> onClose)
            : juce::DocumentWindow("Audio Settings",
                                   juce::Colours::darkgrey,
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

    // ── Generic text list model (used for both palette and chain) ─────────────
    class SimpleListModel : public juce::ListBoxModel
    {
    public:
        explicit SimpleListModel(juce::StringArray& source) : rows(source) {}

        int getNumRows() override { return rows.size(); }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (selected)
                g.fillAll(juce::Colours::lightblue.withAlpha(0.5f));

            if (juce::isPositiveAndBelow(row, rows.size()))
            {
                g.setColour(juce::Colours::white);
                g.drawText(rows[row], 6, 0, width - 12, height, juce::Justification::centredLeft);
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
    void moveSelectedChainSlot(int delta);
    void toggleSelectedBypass();
    void removeSelectedChainSlot();
    void openSelectedEditor();

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
    void deleteScene();
    void recallScene(int index);
    void stepScene(int delta);
    void refreshSceneSelector();
    void saveScenes();
    void restoreScenes();

    // ── Core modules ─────────────────────────────────────────────────────────
    PluginHost pluginHost;
    AudioEngine audioEngine;
    PluginScanner pluginScanner;

    // ── UI ───────────────────────────────────────────────────────────────────
    juce::Label titleLabel;
    juce::Label metricsLabel;
    juce::Label paletteLabel { {}, "Scanned Plugins" };
    juce::Label chainLabel   { {}, "Pedalboard Chain" };

    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton scanButton          { "Scan VST3 Folders" };
    juce::TextButton addButton           { "Add to Chain >" };
    juce::TextButton removeButton        { "Remove" };
    juce::TextButton upButton            { "Move Up" };
    juce::TextButton downButton          { "Move Down" };
    juce::TextButton bypassButton        { "Bypass" };
    juce::TextButton editorButton        { "Open Editor" };
    juce::TextButton savePresetButton    { "Save Preset" };
    juce::TextButton loadPresetButton    { "Load Preset" };
    juce::TextButton captureSceneButton  { "Capture Scene" };
    juce::TextButton updateSceneButton   { "Update Scene" };
    juce::TextButton deleteSceneButton   { "Delete Scene" };
    juce::TextButton prevSceneButton     { "< Prev" };
    juce::TextButton nextSceneButton     { "Next >" };
    juce::ComboBox   sceneSelector;

    juce::StringArray paletteNames;
    juce::StringArray chainNames;
    SimpleListModel paletteModel { paletteNames };
    SimpleListModel chainModel   { chainNames };
    juce::ListBox paletteListBox;
    juce::ListBox chainListBox;

    std::unique_ptr<AudioSettingsWindow> audioSettingsWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ── Live control ───────────────────────────────────────────────────────────
    ControlMap controlMap;
    SceneManager sceneManager;
    std::atomic<bool> midiLearnArmed { false };
    ControlAction pendingLearnAction;   // action to bind when the next trigger arrives

    // ── Persistence ──────────────────────────────────────────────────────────
    juce::ApplicationProperties appProperties;
    static constexpr const char* audioDeviceStateKey = "audioDeviceState";
    static constexpr const char* lastPresetPathKey   = "lastPresetPath";
    static constexpr const char* scenesStateKey      = "scenes";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
