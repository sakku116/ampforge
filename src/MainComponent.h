#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "PluginHost.h"
#include "PluginScanner.h"

class MainComponent : public juce::Component,
                      private juce::Button::Listener,
                      private juce::ListBoxModel
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
                manager,
                /*minAudioInputChannels*/  0,
                /*maxAudioInputChannels*/  2,
                /*minAudioOutputChannels*/ 0,
                /*maxAudioOutputChannels*/ 2,
                /*showMidiInputOptions*/   false,
                /*showMidiOutputSelector*/ false,
                /*showChannelsAsStereoPairs*/ true,
                /*hideAdvancedOptionsWithButton*/ false);

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

    // ── Helpers ───────────────────────────────────────────────────────────────
    void initialiseSettings();
    void tryRestoreLastLoadedPlugin();
    void saveLastLoadedPlugin(const juce::PluginDescription& description);
    void openAudioSettings();
    void saveAudioDeviceState();
    void tryRestoreAudioDeviceState();

    int getNumRows() override;
    void paintListBoxItem(int rowNumber,
                          juce::Graphics& g,
                          int width,
                          int height,
                          bool rowIsSelected) override;

    void buttonClicked(juce::Button* button) override;

    void refreshPluginList();

    // ── Core modules ─────────────────────────────────────────────────────────
    PluginHost pluginHost;
    AudioEngine audioEngine;
    PluginScanner pluginScanner;

    // ── UI ───────────────────────────────────────────────────────────────────
    juce::Label titleLabel;
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton scanButton          { "Scan VST3 Folders" };
    juce::TextButton loadButton          { "Load Selected Plugin" };
    juce::TextButton openEditorButton    { "Open Plugin Editor" };

    juce::ListBox pluginListBox;
    juce::StringArray pluginNames;

    std::unique_ptr<AudioSettingsWindow> audioSettingsWindow;

    // ── Persistence ──────────────────────────────────────────────────────────
    juce::ApplicationProperties appProperties;

    static constexpr const char* lastPluginIdentifierKey = "lastPluginIdentifier";
    static constexpr const char* audioDeviceStateKey     = "audioDeviceState";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
