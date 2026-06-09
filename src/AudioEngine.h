#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>

class PluginHost;

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    explicit AudioEngine(PluginHost& host);
    ~AudioEngine() override;

    void start();
    void stop();

    /** Enables every available MIDI input device and routes it into the engine. */
    void enableAllMidiInputs();

    /** Hook invoked (on the MIDI thread) for each incoming message, for control mapping. */
    std::function<void(const juce::MidiMessage&)> onMidiForControl;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    /** Serialises current device manager state to XML for persistence. */
    std::unique_ptr<juce::XmlElement> saveDeviceState() const;

    /** Restores device manager state from previously saved XML. */
    void restoreDeviceState(const juce::XmlElement* savedState);

    // ── Master control ────────────────────────────────────────────────────────
    void  setMasterInputGain(float gain)  { masterInputGain.store(gain); }
    void  setMasterOutputGain(float gain) { masterOutputGain.store(gain); }
    void  setMasterMuted(bool muted)      { masterMuted.store(muted); }
    float getMasterInputGain()  const     { return masterInputGain.load(); }
    float getMasterOutputGain() const     { return masterOutputGain.load(); }
    bool  isMasterMuted()       const     { return masterMuted.load(); }

    // ── Master level metering ─────────────────────────────────────────────────
    /** Post-input-gain peak (what enters the plugin chain). Linear 0..2+. */
    float getMasterInputPeakLevel()  const { return masterInputPeakLevel.load(); }
    /** Post-output-gain peak (final signal to speakers). Linear 0..2+. */
    float getMasterOutputPeakLevel() const { return masterOutputPeakLevel.load(); }

    // ── Input channel routing ─────────────────────────────────────────────────
    /** 0-based index into the active input channel array.
        The selected channel is fed to all processing buffer channels (mono → stereo). */
    void setInputChannelIndex(int ch) { inputChannelIndex.store(ch); }
    int  getInputChannelIndex()  const { return inputChannelIndex.load(); }

    /** Names of currently active input channels (set on device start). Call from message thread. */
    juce::StringArray getInputChannelNames() const { return activeInputChannelNames; }

    /** Fired on the message thread after the device starts or stops. */
    std::function<void()> onDeviceChanged;

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

    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

private:
    PluginHost& pluginHost;
    juce::AudioDeviceManager deviceManager;
    juce::AudioBuffer<float> processingBuffer;
    juce::MidiBuffer midiBuffer;
    juce::MidiMessageCollector midiCollector;

    std::atomic<float> masterInputGain  { 1.0f };
    std::atomic<float> masterOutputGain { 1.0f };
    std::atomic<bool>  masterMuted      { false };
    std::atomic<int>   inputChannelIndex { 0 };
    juce::StringArray  activeInputChannelNames;   // updated in audioDeviceAboutToStart
    std::atomic<float> masterInputPeakLevel  { 0.0f };
    std::atomic<float> masterOutputPeakLevel { 0.0f };

    // metrics
    std::atomic<double> dspLoad { 0.0 };       // smoothed processAudio time / block time
    double currentSampleRateHz   = 0.0;
    int    currentBlockSizeSamples = 0;
    int    inputLatencySamples   = 0;
    int    outputLatencySamples  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
