#pragma once

#include <JuceHeader.h>

class PluginHost;

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine(PluginHost& host);
    ~AudioEngine() override;

    void start();
    void stop();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    /** Serialises current device manager state to XML for persistence. */
    std::unique_ptr<juce::XmlElement> saveDeviceState() const;

    /** Restores device manager state from previously saved XML. */
    void restoreDeviceState(const juce::XmlElement* savedState);

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    PluginHost& pluginHost;
    juce::AudioDeviceManager deviceManager;
    juce::AudioBuffer<float> processingBuffer;
    juce::MidiBuffer midiBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
