#include "AudioEngine.h"
#include "HostDebug.h"
#include "PluginHost.h"

AudioEngine::AudioEngine(PluginHost& host) : pluginHost(host)
{
}

AudioEngine::~AudioEngine()
{
    stop();
}

void AudioEngine::start()
{
    HostDebug::log("AudioEngine::start");

    auto error = deviceManager.initialiseWithDefaultDevices(2, 2);

    if (error.isNotEmpty())
    {
        HostDebug::log("Audio init (2 in / 2 out) failed: " + error + " — trying output only");
        error = deviceManager.initialiseWithDefaultDevices(0, 2);
    }

    if (error.isNotEmpty())
        HostDebug::log("Audio init error: " + error);
    else if (auto* device = deviceManager.getCurrentAudioDevice())
        HostDebug::log("Audio device: " + device->getName() + " | " + device->getTypeName());

    if (deviceManager.getCurrentAudioDevice() != nullptr)
        deviceManager.addAudioCallback(this);
    else
        HostDebug::log("Audio callback not registered — no output device");

    enableAllMidiInputs();
}

void AudioEngine::enableAllMidiInputs()
{
    const auto devices = juce::MidiInput::getAvailableDevices();

    HostDebug::log("MIDI inputs available: " + juce::String(devices.size()));

    for (const auto& device : devices)
    {
        if (! deviceManager.isMidiInputDeviceEnabled(device.identifier))
            deviceManager.setMidiInputDeviceEnabled(device.identifier, true);

        deviceManager.addMidiInputDeviceCallback(device.identifier, this);
        HostDebug::log("  MIDI in enabled: " + device.name);
    }
}

void AudioEngine::stop()
{
    HostDebug::log("AudioEngine::stop");

    for (const auto& device : juce::MidiInput::getAvailableDevices())
        deviceManager.removeMidiInputDeviceCallback(device.identifier, this);

    deviceManager.removeAudioCallback(this);
}

std::unique_ptr<juce::XmlElement> AudioEngine::saveDeviceState() const
{
    return deviceManager.createStateXml();
}

void AudioEngine::restoreDeviceState(const juce::XmlElement* savedState)
{
    deviceManager.removeAudioCallback(this);

    auto error = deviceManager.initialise(2, 2, savedState, true);

    if (error.isNotEmpty())
    {
        HostDebug::log("Audio restore (2/2) failed: " + error + " — trying output only");
        error = deviceManager.initialise(0, 2, savedState, true);
    }

    if (error.isNotEmpty())
        HostDebug::log("Audio restore error: " + error);
    else
        HostDebug::log("Audio device state restored");

    if (deviceManager.getCurrentAudioDevice() != nullptr)
        deviceManager.addAudioCallback(this);
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                   int numInputChannels,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    const int pluginChannels = pluginHost.getProcessingChannelCount();
    const int channelsToUse = pluginChannels > 0 ? juce::jmax(numOutputChannels, pluginChannels)
                                                 : numOutputChannels;

    processingBuffer.setSize(channelsToUse, numSamples, false, false, true);
    processingBuffer.clear();

    const float inGain = masterInputGain.load();
    const int selCh = juce::jlimit(0, juce::jmax(numInputChannels - 1, 0),
                                   inputChannelIndex.load());
    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        if (selCh < numInputChannels && inputChannelData[selCh] != nullptr)
        {
            processingBuffer.copyFrom(ch, 0, inputChannelData[selCh], numSamples);
            if (inGain != 1.0f)
                processingBuffer.applyGain(ch, 0, numSamples, inGain);
        }
    }

    // Measure post-input-gain peak (enters the plugin chain). Every processing
    // channel holds a copy of the same selected input, so channel 0 is
    // representative — no need to scan all channels.
    masterInputPeakLevel.store(channelsToUse > 0 ? processingBuffer.getMagnitude(0, 0, numSamples)
                                                  : 0.0f);

    midiBuffer.clear();
    midiCollector.removeNextBlockOfMessages(midiBuffer, numSamples);   // live MIDI input

    const auto startTicks = juce::Time::getHighResolutionTicks();
    pluginHost.processAudio(processingBuffer, midiBuffer);
    const auto elapsedSeconds = juce::Time::highResolutionTicksToSeconds(
        juce::Time::getHighResolutionTicks() - startTicks);

    if (currentSampleRateHz > 0.0)
    {
        const double blockSeconds = (double) numSamples / currentSampleRateHz;
        const double load = blockSeconds > 0.0 ? elapsedSeconds / blockSeconds : 0.0;
        const double prev = dspLoad.load();
        dspLoad.store(prev + 0.1 * (load - prev));   // exponential smoothing
    }

    const float outGain = masterMuted.load() ? 0.0f : masterOutputGain.load();
    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
        {
            juce::FloatVectorOperations::copy(outputChannelData[ch], processingBuffer.getReadPointer(ch), numSamples);
            if (outGain != 1.0f)
                juce::FloatVectorOperations::multiply(outputChannelData[ch], outGain, numSamples);
        }
    }

    // Measure post-output-gain peak (after chain + output gain).
    {
        float pk = 0.0f;
        for (int ch = 0; ch < juce::jmin(numOutputChannels, channelsToUse); ++ch)
            pk = juce::jmax(pk, processingBuffer.getMagnitude(ch, 0, numSamples));
        masterOutputPeakLevel.store(pk * outGain);
    }
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
    {
        HostDebug::log("audioDeviceAboutToStart: device is null");
        return;
    }

    HostDebug::log("Audio started: " + device->getName()
                   + " | " + juce::String(device->getCurrentSampleRate(), 1) + " Hz"
                   + " | buffer " + juce::String(device->getCurrentBufferSizeSamples())
                   + " | in ch " + juce::String(device->getActiveInputChannels().countNumberOfSetBits())
                   + " | out ch " + juce::String(device->getActiveOutputChannels().countNumberOfSetBits()));

    currentSampleRateHz = device->getCurrentSampleRate();
    currentBlockSizeSamples = device->getCurrentBufferSizeSamples();
    inputLatencySamples = device->getInputLatencyInSamples();
    outputLatencySamples = device->getOutputLatencyInSamples();

    // Build names for active input channels so UI can populate a selector.
    activeInputChannelNames.clear();
    const auto activeBits = device->getActiveInputChannels();
    const auto allNames   = device->getInputChannelNames();
    for (int i = 0; i < allNames.size(); ++i)
        if (activeBits[i])
            activeInputChannelNames.add(allNames[i].isEmpty()
                                            ? "Input " + juce::String(i + 1)
                                            : allNames[i]);

    midiCollector.reset(device->getCurrentSampleRate());

    pluginHost.prepare(device->getCurrentSampleRate(),
                       device->getCurrentBufferSizeSamples(),
                       device->getActiveInputChannels().countNumberOfSetBits(),
                       device->getActiveOutputChannels().countNumberOfSetBits());

    if (onDeviceChanged)
        onDeviceChanged();
}

void AudioEngine::audioDeviceStopped()
{
    HostDebug::log("Audio device stopped");
    activeInputChannelNames.clear();
    if (onDeviceChanged)
        onDeviceChanged();
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    midiCollector.addMessageToQueue(message);   // forwarded to the plugin chain on the audio thread

    if (onMidiForControl)
        onMidiForControl(message);              // tapped for control mapping (Phase 4.2)
}

double AudioEngine::getInputLatencyMs() const
{
    return currentSampleRateHz > 0.0 ? (double) inputLatencySamples / currentSampleRateHz * 1000.0 : 0.0;
}

double AudioEngine::getOutputLatencyMs() const
{
    return currentSampleRateHz > 0.0 ? (double) outputLatencySamples / currentSampleRateHz * 1000.0 : 0.0;
}

double AudioEngine::getChainLatencyMs() const
{
    return currentSampleRateHz > 0.0
         ? (double) pluginHost.getChainLatencySamples() / currentSampleRateHz * 1000.0
         : 0.0;
}

double AudioEngine::getRoundTripLatencyMs() const
{
    return getInputLatencyMs() + getOutputLatencyMs() + getChainLatencyMs();
}
