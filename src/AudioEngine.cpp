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
}

void AudioEngine::stop()
{
    HostDebug::log("AudioEngine::stop");
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

    for (int ch = 0; ch < channelsToUse; ++ch)
    {
        if (ch < numInputChannels && inputChannelData[ch] != nullptr)
            processingBuffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);
    }

    midiBuffer.clear();
    pluginHost.processAudio(processingBuffer, midiBuffer);

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::copy(outputChannelData[ch], processingBuffer.getReadPointer(ch), numSamples);
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

    pluginHost.prepare(device->getCurrentSampleRate(),
                       device->getCurrentBufferSizeSamples(),
                       device->getActiveInputChannels().countNumberOfSetBits(),
                       device->getActiveOutputChannels().countNumberOfSetBits());
}

void AudioEngine::audioDeviceStopped()
{
    HostDebug::log("Audio device stopped");
}
