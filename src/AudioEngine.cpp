#include "AudioEngine.h"
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
    auto error = deviceManager.initialiseWithDefaultDevices(2, 2);

    if (error.isNotEmpty())
        juce::Logger::writeToLog("Audio device init error: " + error);

    deviceManager.addAudioCallback(this);
}

void AudioEngine::stop()
{
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
        juce::Logger::writeToLog("Audio restore error: " + error);

    deviceManager.addAudioCallback(this);
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                   int numInputChannels,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext&)
{
    processingBuffer.setSize(numOutputChannels, numSamples, false, false, true);
    processingBuffer.clear();

    for (int ch = 0; ch < numOutputChannels; ++ch)
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
        return;

    pluginHost.prepare(device->getCurrentSampleRate(),
                       device->getCurrentBufferSizeSamples(),
                       device->getActiveInputChannels().countNumberOfSetBits(),
                       device->getActiveOutputChannels().countNumberOfSetBits());
}

void AudioEngine::audioDeviceStopped()
{
}
