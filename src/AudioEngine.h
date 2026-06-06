#pragma once

#include <JuceHeader.h>
#include <atomic>

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

    // ── Performance metrics (Phase 3) ─────────────────────────────────────────
    double getCpuUsagePercent() const { return deviceManager.getCpuUsage() * 100.0; }
    double getDspLoadPercent() const  { return dspLoad.load() * 100.0; }
    double getInputLatencyMs() const;
    double getOutputLatencyMs() const;
    double getChainLatencyMs() const;
    double getRoundTripLatencyMs() const;
    double getCurrentSampleRate() const { return currentSampleRateHz; }
    int    getCurrentBlockSize() const { return currentBlockSizeSamples; }

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

    // metrics
    std::atomic<double> dspLoad { 0.0 };       // smoothed processAudio time / block time
    double currentSampleRateHz   = 0.0;
    int    currentBlockSizeSamples = 0;
    int    inputLatencySamples   = 0;
    int    outputLatencySamples  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
